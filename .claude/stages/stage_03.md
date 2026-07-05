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

## Известные ограничения MVP
- Рендер ручной (кнопка/меню), НЕ авто после каждой правки нот (VSTi инстанцируется секунды).
- Настройки инструмента = дефолт (GetDefaultSettings); пресет/состояние per-track — позже.
- Рендер начинается с t=0 и накрывает [0, конец нот+0.5с] — куски клипа правее НЕ трогает.
- Выделение пользователя после рендера остаётся на MIDI-треке (не восстанавливается).
- Bake (MIDI→обычный аудио-трек) не сделан — но по сути это снять флаг IsMidi после рендера.

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
