# Stage 00 — Разблокировка VSTi (звук из VST-инструментов)

Статус: ✅ **Готово и проверено** (2026-07-04).
Зависит от: —. Разблокирует: этап 3 (звук MIDI-трека использует этот же путь).

## Цель

Audacity 4 сканирует VST3-инструменты, но не извлекает из них звук. Нужно, чтобы
VSTi (синтезаторы/сэмплеры) распознавались, показывались в UI, инстанцировались и
играли ноты (получали MIDI-события и выдавали аудио).

## Диагноз (было): 4 обрыва пути «инструмент → звук»

1. **Типизация**: `VST3EffectBase::GetType()` — у инструмента (subcategory `kInstrument`,
   без `kFx`) возвращался `EffectTypeNone`.
2. **Фильтр UI**: менеджер плагинов выбрасывал всё с типом `Unknown` (в коде был комментарий
   «VSTi … we don't support them yet»).
3. **MIDI-входы выключены**: при инициализации event-шины плагина деактивировались
   (`activateBus(kEvent, kInput, i, 0)`).
4. **Ноты не подаются**: `VST3Wrapper::Process()` никогда не заполнял `data.inputEvents`.

Важно: обработка звука VST идёт через au3-шный `VST3Wrapper`; muse-VST-модуль — только для
окна плагина. Поэтому чинили в `au3/libraries/au3-vst3/`.

## Что сделано (файлы и суть правок)

### 1. `au3/libraries/au3-vst3/VST3EffectBase.cpp` — `GetType()`
После проверки `kFx` добавлено: если среди подкатегорий есть `kInstrument` → вернуть
`EffectTypeGenerate`. Так инструмент попадает в пайплайн генераторов (рендер в выделение).
Цепочка типа: `GetType()` → `GetClassification()` (наследуется, `EffectInterface.cpp`) →
au3 `VST3EffectsModule::DiscoverPluginsAtPath` → meta reader (`src/effects/vst/internal/
vst3pluginsmetareader.cpp` → `Au3AudioPluginMetaReader`) → `toAu4EffectType` (EffectTypeGenerate
→ `EffectType::Generator`).

### 2. `src/effects/effects_base/view/pluginmanagertableviewmodel.cpp`
Фильтр остаётся `meta.type == EffectType::Unknown` (это правильно — режет реально нераспознанное).
Инструменты теперь `Generator`, а не `Unknown`, поэтому проходят. Обновлён только комментарий.

### 3. `au3/libraries/au3-vst3/VST3Wrapper.cpp` — `ActivateMainAudioBuses()`
Event-**входы** активируются: `activateBus(Vst::kEvent, Vst::kInput, i, 1)` (было `0`).
Event-выходы оставлены выключенными (хост их не читает). Комментарии функции обновлены.

### 4. `au3/libraries/au3-vst3/VST3Wrapper.h` / `.cpp` — подача нот
Добавлено (в `.h`): инклуды `ivstevents.h`, `hosting/eventlist.h`; публичные методы
`bool HasEventInputBus() const;` и
`void QueueNoteEvent(int64 sampleTime, int64 sampleDuration, int16 pitch, float velocity);`.
Приватные члены: `std::vector<PendingEvent> mPendingEvents;` (PendingEvent = {int64 time; Vst::Event event}),
`Steinberg::Vst::EventList mInputEvents;`, `int64 mProcessedSamples{0};`.

В `.cpp`:
- `Initialize()`: перед активацией — `mPendingEvents.clear(); mInputEvents.setMaxSize(512);
  mProcessedSamples = 0;`. В `mProcessContext` выставлены флаги `kPlaying | kTempoValid |
  kTimeSigValid`, tempo=120, timeSig 4/4, `projectTimeSamples=0` (некоторые инструменты
  требуют валидный контекст).
- `Process()`: в начале `mInputEvents.clear();` — из `mPendingEvents` в `mInputEvents`
  переносятся события, попадающие в текущий блок `[mProcessedSamples, mProcessedSamples+numSamples)`
  с пересчётом `sampleOffset`; отыгранные удаляются. Затем `data.inputEvents = &mInputEvents;`
  и `mProcessContext.projectTimeSamples = mProcessedSamples;`. В конце при успехе
  `mProcessedSamples += data.numSamples;`.
- `QueueNoteEvent()`: формирует пару `kNoteOnEvent`/`kNoteOffEvent` (busIndex=0, channel=0,
  noteId=-1) и кладёт в `mPendingEvents` по времени `sampleTime` и `sampleTime+sampleDuration`.

### 5. `au3/libraries/au3-vst3/VST3Instance.h` / `.cpp` — тест-паттерн
Приватный метод `void QueueDefaultNotePattern(double duration, double sampleRate);`.
В `ProcessInitialize()`: если `effect.GetType() == EffectTypeGenerate && mWrapper->HasEventInputBus()`
→ вызвать `QueueDefaultNotePattern(settings.extra.GetDuration(), sampleRate)`.
`QueueDefaultNotePattern` ставит C-мажорное арпеджио (pitches 60/64/67/72), четвертями при 120 BPM
(нота = 0.5 c), на всю длину выделения; если длительность меньше одной ноты — держит одну ноту.

> Это ВРЕМЕННАЯ заглушка, доказывающая извлечение звука. На этапе 3 её заменит подача реальных
> нот из MIDI-клипа (через тот же `QueueNoteEvent`).

## Как проверялось

Headless: `Audacity4.exe --register-audio-plugin "<...>.vst3"` → в `known_audio_plugins.json`:
- Roland **D-50**, Synapse **The Legend HZ** → `"type": "Generator"` (subcats Instrument). ✅
- **TDR Kotelnikov** (эффект) → `"type": "Effect"` (без регресса). ✅

GUI (пользователь): Generate → инструмент → на пустом выделении рендерится арпеджио.
Legend HZ: генерация + редактор + ручки — всё работает. D-50: звук есть.

## Известные проблемы / отложено

- **D-50 (Roland Cloud) роняет приложение при открытии его «морды»** (SIGSEGV в
  `VstView::init`/`attached`, muse `qml/Muse/Vst/vstview.cpp`). Проверено: и в Release.
  **Плагино-специфично, НЕ архитектурно**: тот же UI-путь работает у Legend HZ и у эффектов;
  connection points component↔controller соединяются корректно (`VST3Wrapper.cpp:469-477`);
  звук D-50 при этом извлекается. Причина — капризный кастомный UI Roland Cloud + молодой
  VST-хост AU4. FL это тянет за счёт зрелого хоста.
  → **Отложено.** Тестировать на Legend HZ / Kontakt. Позже (полировка) — обернуть открытие
  редактора в защиту, чтобы кривой плагин не ронял всё приложение (локально, ~десяток строк).
- Debug-сборка сыпет CRT-ассертами из кода плагинов (printf). Использовать Release.

## Возможные доработки этапа 0 (не обязательны)
- Ввести отдельный `EffectType`/`EffectFamily` для инструментов вместо переиспользования
  `Generator` (чище для UI: отдельная категория «Instruments»). Пока Generator достаточно.
