# Stage 03 — Звук MIDI-трека: привязка VSTi, рендер, воспроизведение, запекание

Статус: 🟡 **MVP реализован** (2026-07-05): привязка инструмента + ручной рендер нот в клип.
Ждёт проверки GUI. Осталось: авто-ре-рендер, состояние плагина per-track, bake, реалтайм-стриминг.
Зависит от: этап 0 (подача нот в VST3Wrapper), этап 1 (модель нот), этап 2 (ноты для проигрывания).
Это «сердце» секвенсера — превращает нарисованные ноты в звук.

## Что сделано (MVP, 2026-07-05)

Архитектура: **рендер = программное применение VSTi-генератора к треку** — переиспользован
весь пайплайн эффектов (undo, прогресс, запись в клип бесплатно).

- **`au3/libraries/au3-wave-track/MidiInstrument.{h,cpp}` (НОВЫЕ)** — attachment по образцу
  MidiSequence: хранит AU4 effectId инструмента, сериализуется `<midiinstrument effectid=".."/>`.
- **`au3/libraries/au3-effects/MidiRenderQueue.{h,cpp}` (НОВЫЕ)** — статическая очередь нот
  {timeSec, durationSec, pitch, velocity}: мост между effects_base (Set) и VST3Instance (Take)
  без новых линковочных зависимостей (au3-vst3 уже линкует au3-effects).
- **`VST3Instance::ProcessInitialize`** — если `MidiRenderQueue::Take()` непуст → в
  `QueueNoteEvent` идут РЕАЛЬНЫЕ ноты (секунды → сэмплы по фактическому rate);
  иначе прежний тест-паттерн (ручной Generate работает как раньше).
- **`EffectsActionsController` (effects_base)** — два action query:
  - `action://effects/midi/set_instrument?trackId=&effectId=` — пишет MidiInstrument,
    pushHistoryState, сразу рендерит;
  - `action://effects/midi/render?trackId=` — `doRenderMidiTrack`: ноты → секунды
    (через ProjectTimeSignature::GetQuarterDuration), +0.5с хвост на release; стоп плейбека;
    выделение = наш трек, [0, endSec] (resetSelectedClips + setSelectedTracks +
    setDataSelected*Time); `MidiRenderQueue::Set` → `performEffect(effectId, "")` —
    **вторая перегрузка = kConfigured = БЕЗ диалога настроек**; очередь чистится после.
  - Новые инжекты в заголовке: IGlobalContext, ISelectionController, IProjectHistory
    (интерфейсы header-only, линковать trackedit не нужно).
- **Контекстное меню MIDI-клипа** (ClipContextMenuModel): подменю **Instrument**
  (VST3-генераторы из `effectsProvider()->effectMetaList()`, галочка на текущем; клик =
  set_instrument+рендер) и пункт **Render MIDI audio**. projectscene инжектит
  IEffectsProvider header-only (это нормальный muse-паттерн).
- **Кнопка Render в piano roll** (`PianoRollCanvas::requestRender` → dispatch render-query).

## Как пользоваться (тест)
1. Создать MIDI-трек → ПКМ по клипу → Instrument → выбрать Legend HZ → рендер прошёл,
   в клипе волна вместо тишины → Play → слышно арпеджио.
2. Открыть piano roll, поменять ноты → кнопка Render → волна и звук обновились.
3. Сохранить/открыть проект → инструмент в меню с галочкой, Render работает без перевыбора.
4. Undo после рендера откатывает аудио (история от performEffect).

## Доработки после первого теста (2026-07-05, вторая итерация)
- **Ноты стали clip-relative**: превью считает `startTime клипа + startBeats*quarterSec`
  (midinotesview), рендер целится в `[anchor, anchor+end]`, где anchor = GetPlayStartTime()
  первого клипа (effectsactionscontroller). Итог: перетаскивание клипа двигает ноты, превью
  и аудио ВМЕСТЕ, ре-рендер не нужен. Piano roll не менялся (он и так в «паттерн-координатах»).
  Допущение MVP: один клип на MIDI-трек.
- **Авто-рендер**: PianoRollCanvas — QTimer-дебаунс 1.2с после последнего жеста → requestRender;
  чекбокс «Auto-render» в тулбаре диалога (по умолчанию вкл); тихо пропускается, если
  инструмент не назначен (иначе спамил бы диалогом ошибки).

## Этап 3.5 «Живой MIDI» (запрос пользователя 2026-07-05)

Хотелки: (1) проигрывание нот транспортом БЕЗ рендера, (2) игра мышкой по клавишам piano roll,
(3) открытие UI инструмента из меню/глазика.

### (3) UI инструмента — СДЕЛАНО (итерация 3)
Action `midi-open-instrument-ui` (args: trackId) = `doRenderMidiTrack(trackId, withDialog=true)`
→ `performEffect(effectId)` ОДИН аргумент = диалог с vendor UI открывается ДО применения.
В диалоге есть **Preview** (слушать пока крутишь — частично закрывает и хотелку (2));
OK = рендер с новыми настройками. КЛЮЧЕВОЙ ФАКТ: EffectManager держит настройки per-plugin
в памяти (инициализация из CurrentSettings-пресета, после OK — SaveUserPreset туда же),
поэтому ТИХИЙ авто-рендер после этого использует те же настройки — ручки влияют на звук
навсегда, переживает рестарт. Для этого MidiRenderQueue::Take→Get (НЕ очищается чтением:
Preview+Apply = несколько ProcessInitialize; сервис чистит явно после performEffect).
Вход: пункт «Instrument settings…» в меню клипа + глазик (EYE_OPEN) в piano roll.
Ограничение: настройки per-PLUGIN, не per-track (два MIDI-трека с одним синтом делят пресет).

### (1)+(2) Реалтайм и игра мышкой — V1 РЕАЛИЗОВАН (коммит 00ebfad68), ждёт проверки
- Тумблер «Live MIDI playback» в меню MIDI-клипа: вешает инструмент realtime-эффектом
  на трек (`IRealtimeEffectService::addRealtimeEffect`; повторный клик снимает; виден в
  панели эффектов трека; сериализуется с проектом).
- Планирование: подписка на `IPlaybackController::isPlayingChanged` в EffectsActionsController;
  на старте плея для каждого MIDI-трека с live-инструментом: `ResetLiveNotes()` (сброс
  сэмпл-часов в 0 = старт стрима) + `QueueLiveNote((absSec−playStartSec)·rate, …)`.
  Сброс часов на каждый старт также чинит паузу/резюм и повторный плей.
- Развязка от VST3 SDK: интерфейс `MidiRenderQueue::LiveMidiReceiver` (au3-effects),
  VST3Instance его реализует (fan-out в реалтайм-подынстансы mProcessors);
  effects_base делает dynamic_pointer_cast интерфейса — БЕЗ инклюда VST3Instance.h
  (у effects_base нет инклюд-путей VST3 SDK — C1083 на uid.h).
- Потокобезопасность: mPendingEventsMutex в VST3Wrapper (Queue/Reset с main-треда,
  дрейн в Process на аудио-треде; mProcessedSamples тоже под мьютексом).
- Игра мышкой: клик по клавиатуре piano roll → action `midi-audition-note` →
  `QueueLiveNoteNow` (time = mProcessedSamples → ближайший блок). Слышно ТОЛЬКО при
  включённом Live и активном стриме (реалтайм-цепочка процессит только при плейбеке).
- Известные ограничения v1: сик во время плейбека не пересинхронизирует ноты (заряжаются
  на старте); при Live-режиме отрендеренный клип идёт на ВХОД синта (VSTi обычно
  игнорируют вход — если плагин пропускает вход насквозь, будет дубль звука);
  луп-регион не поддержан.

### Очередь доработок live-MIDI (фидбек 2026-07-05, сессия 3)
1. **Аудишн молчит и нет визуального отклика** (клик по клавишам/нотам при плее):
   вероятная причина — гонка в `QueueNoteEventNow`: time = mProcessedSamples, но аудио-тред
   успевает пройти этот сэмпл до дрейна → событие «в прошлом» молча выбрасывается
   (drain удаляет e.time < blockEnd без отправки). ФИКС: time = mProcessedSamples + safety
   (~2048 сэмплов, <50мс задержки). Плюс подсветка нажатой клавиши в PianoRollCanvas
   (сохранять pressed pitch, рисовать в paint, сбрасывать на release).
2. **ASIO** — воспроизведение «трудно идёт»; рассмотреть сборку portaudio с ASIO SDK
   (build/audacity-release/_deps/portaudio — пересобрать с PA_USE_ASIO; SDK качается у
   Steinberg, для собственной сборки лицензионно ок; в PortAudio ASIO хост-API включается
   флагом CMake). Альтернатива дешевле: WASAPI Exclusive в настройках устройства AU4.
3. **Хвост ревера обрывается на конце трека** (как НЕ в FL): стрим останавливается на
   totalTime проекта. ФИКС ДЁШЕВО: клип-тишина должен тянуться на 2–4с ЗА последнюю ноту
   (в newMidiTrack сид и в doRenderMidiTrack releaseTailSec 0.5→2.0+, и/или после правок
   нот в piano roll продлевать клип). Правильный фикс — не резать реалтайм-хвосты движком,
   но это глубже.

### Старый дизайн-набросок (для истории)
Путь: инструмент как REALTIME-эффект на MIDI-треке (RealtimeEffectList / IRealtimeEffectService
в effects_base) — даёт постоянный живой инстанс с аудио-путём в выход.
- Проигрывание: на старте плейбека закинуть в VST3Wrapper очередь нот со сдвигом
  −t0 стрима (noteSample = (noteSec − playStartSec)·rate). Нужен мьютекс на mPendingEvents
  (аудио-тред!). Сик/луп = перезаливка очереди. Достать wrapper: RealtimeEffectState::
  GetInstance → VST3Instance::mProcessors (реалтайм-подынстансы!).
- Игра мышкой: с живым инстансом = QueueNoteEvent(«сейчас») — но реалтайм-цепочка
  обрабатывается ТОЛЬКО при активном стриме. Вариантты: мониторинг/пустой play, либо
  оставить Preview-кнопку как аудишн. Исследовать: как AU4 включает monitoring.
- Трек должен оставаться тихим контейнером при реалтайме (иначе рендер+синт = дважды звук):
  либо мьютить клип при активном реалтайм-инструменте, либо реалтайм-инстанс заменяет
  содержимое (инструмент и так генерит поверх — вход игнорирует).

## Известные ограничения MVP
- Реалтайм-проигрывание нот без рендера НЕ реализовано (путь через
  TransportSequences.otherPlayableSequences — отложено; пока звук только из рендер-кэша).
- Настройки инструмента = дефолт (GetDefaultSettings); пресет/состояние per-track — позже.
- Выделение пользователя после рендера остаётся на MIDI-треке (не восстанавливается).
- Bake (MIDI→обычный аудио-трек) не сделан — но по сути это снять флаг IsMidi после рендера.
- Проверено пользователем (первая итерация): смена инструментов работает, включая Roland D-50
  (его «морда» не открывается — потому и не падает, см. stage_00).

## Цель

1. К MIDI-треку привязывается VST-инструмент (VSTi).
2. Ноты клипа рендерятся инструментом в аудио (кэш) — трек звучит в общем миксе.
3. Кнопка «запечь в аудио-трек» (bake) — превратить MIDI-трек в обычный wave.

## Ключевая опора (этап 0)

`au3/libraries/au3-vst3/VST3Wrapper` уже умеет: активные event-входы, `QueueNoteEvent(sampleTime,
sampleDuration, pitch, velocity)`, заполнение `data.inputEvents` в `Process()`. На этапе 0 это
демонстрировал `VST3Instance::QueueDefaultNotePattern` (тест-арпеджио). Здесь заглушку заменяем
подачей РЕАЛЬНЫХ нот из `MidiClip`.

## План (рекомендуемый: render-on-edit в аудио-кэш)

Самый простой и надёжный для MVP путь — рендерить клип инструментом в аудио заранее, а играть
как обычный wave (переиспользует весь транспорт/микшер):

1. **Привязка инструмента к треку**:
   - Хранить на треке идентификатор VSTi (effectId/pluginPath) + его состояние (пресет/параметры).
     Место: attachment на WaveTrack (по образцу флага IsMidi / WaveTrackData) или в trackedit-domain.
   - UI: в шапке MIDI-трека — селектор инструмента (список из `known_audio_plugins.json`, тип Generator/Instrument),
     кнопка открыть «морду» (переиспользовать существующий launcher, см. этап 0; учесть, что у
     некоторых плагинов, напр. Roland D-50, морда падает — обернуть в защиту).

2. **Рендер нот → аудио**:
   - Инстанцировать VSTi (`VST3Instance`/`VST3Wrapper`), `Initialize(offline, rate, block)`.
   - Ноты `MidiClip` (в долях) → в секунды/сэмплы через `ProjectTimeSignature`
     (`GetBeatDuration()` × BPM) → `QueueNoteEvent(sampleTime, sampleDuration, pitch, velocity)`.
   - Прогонять `Process()` блоками на всю длину, собирать выход в буфер, писать в WaveClip трека
     (по образцу генераторов: `au3/libraries/au3-builtin-effects/Generator.cpp`,
     `PerTrackEffect::DoProcess` — как EffectTypeGenerate пишет в трек).
   - Кэшировать: перерендеривать при изменении нот/инструмента (render-on-edit). Можно рендерить
     в отдельный «скрытый» wave, чтобы не путать с bake.

3. **Воспроизведение**: раз звук уже в wave-клипе трека, он играет штатно (ничего доп. не нужно).
   - Альтернатива (позже): realtime-стриминг синта через `TransportSequences.otherPlayableSequences`
     (`au3/libraries/au3-mixer/AudioIOSequences.h`) — сложнее, оставить на будущее.

4. **Запекание (bake)**: команда «MIDI → Audio»: снять флаг IsMidi с трека (или создать новый
   wave-трек с отрендеренным аудио), убрать модель нот/привязку инструмента. По сути это уже
   готовый результат рендера, просто зафиксировать его как обычный аудио-трек. Аналог
   `Tracks > Mix and Render` (`au3/libraries/au3-effects/MixAndRender.h`).

## Точки интеграции
- `QueueNoteEvent` — уже есть (этап 0).
- Паттерн генератора, пишущего в трек — `au3-builtin-effects/Generator.cpp`, `PerTrackEffect.cpp`.
- Темп/доли → секунды — `au3-numeric-formats/ProjectTimeSignature`.
- Привязка инструмента — новый attachment (образец: WaveTrackData/IsMidi из этапа 1).

## Тесты
- Привязать Legend HZ к MIDI-треку, нарисовать ноты (этап 2), нажать play → слышны именно эти ноты.
- Изменить ноту → звук обновился (render-on-edit).
- Bake → трек стал обычным аудио с тем же звуком; проект сохраняется/открывается.

## Заметки/риски
- Не открывать «морду» проблемных плагинов без защиты (см. этап 0, D-50 SIGSEGV).
- velocity 0..1 в модели → в VST3 note-on velocity (0..1). pitch = MIDI note number.
- Длительность нот в долях → сэмплы зависят от BPM; при смене BPM нужен ре-рендер.
