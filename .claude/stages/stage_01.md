# Stage 01 — Тип трека MIDI + меню + модель нот + сериализация

Статус: ✅ **Готово и проверено пользователем** (2026-07-05). E2e-прогон: T1–T13 зелёные,
T15–T20 зелёные, undo/удаление MIDI-трека работают. Ограничения T21/T22 подтверждены (см. ниже).

### Известная проблема (не блокер): плавающее зависание undo
2026-07-05 ~00:14 один раз зависло на втором Ctrl+Z после создания MIDI-трека
(лог обрывается на `action://trackedit/undo`). НЕ воспроизводится ни чистым сценарием,
ни точным повтором сессии (стерео+Legend HZ+play/seek+realtime Super VHS с открытым окном+MIDI+2×Ctrl+Z).
Подозрение: гонка undo-восстановления RealtimeEffectList (спинлок) на фоне аудио-движка,
MIDI скорее всего ни при чём. Если повторится: НЕ закрывать приложение, снять дамп
(`scratchpad/capture_hang_dump.ps1` этой сессии; comsvcs MiniDump + WinDbg), вытащить стеки.
- ✅ Тип трека MIDI, пункт меню, флаг + сериализация флага, иконка/имя, отрисовка как аудио.
- ✅ Модель нот `MidiSequence`/`MidiNote` (в четвертных долях) + сериализация в .aup4.
- ✅ Превью нот в клипе (`MidiNotesView`, мини-piano-roll поверх волны).
- ✅ Временный сид тестовых нот при создании MIDI-трека (до piano roll этапа 2).

Зависит от: —. Нужен для: этапа 2 (piano roll рисует в эту модель) и этапа 3 (звук из этой модели).

## Архитектурное решение (повтор из SPEC)

MIDI-трек = обычный моно `WaveTrack` с флагом `IsMidi`. Ноты хранятся в долях (beats).
Звук — рендер нот инструментом в аудио этого трека (этап 3). Причина выбора — в SPEC.md §2.

## Что уже сделано (ГОТОВО)

### au3-ядро: флаг IsMidi + его сериализация
`au3/libraries/au3-wave-track/WaveTrack.cpp`:
- В `struct WaveTrackData` добавлено поле `bool mIsMidi{false}`; копируется в copy-ctor;
  геттер/сеттер `GetIsMidi()/SetIsMidi()`.
- `WaveTrack::IsMidi()/SetIsMidi()` делегируют в `WaveTrackData::Get(*this)`.
- XML-атрибут: `static constexpr auto IsMidi_attr = "ismidi";`
  - Чтение в `WaveTrack::HandleXMLTag`: `else if (attr == IsMidi_attr && value.TryGet(nValue)) SetIsMidi(nValue != 0);`
  - Запись в `WriteXMLAttributes`: `if (track.IsMidi()) xmlFile.WriteAttr(IsMidi_attr, 1);`
  → флаг переживает сохранение/открытие .aup4.

`au3/libraries/au3-wave-track/WaveTrack.h`: объявлены `bool IsMidi() const; void SetIsMidi(bool);`.

### trackedit: тип + цепочка создания
- `src/trackedit/dom/track.h`: в `enum class TrackType` добавлен `Midi` (в конец).
- `src/au3wrap/internal/domconverter.cpp` (`trackType()`): перед switch по каналам —
  если `WaveTrack::IsMidi()` → вернуть `TrackType::Midi`.
- Action `new-midi-track` проброшен через ВСЕ слои (ловушка: интерфейсов ДВА!):
  - `src/trackedit/itrackeditinteraction.h` (фасад) — `virtual bool newMidiTrack() = 0;`
  - `src/trackedit/itracksinteraction.h` (au3-бэкенд) — `virtual bool newMidiTrack() = 0;`
  - `src/trackedit/internal/trackeditinteraction.{h,cpp}` (обёртка) — `newMidiTrack()` через `withPlaybackStop`.
  - `src/trackedit/internal/trackeditoperationcontroller.{h,cpp}` (мост фасад→бэкенд) —
    `newMidiTrack()`: `tracksInteraction()->newMidiTrack()` + `pushHistoryState("Created new MIDI track","New MIDI track")`.
  - `src/trackedit/internal/au3/au3tracksinteraction.{h,cpp}` — реализация:
    `appendWaveTrack(tracks,1)` → `SetIsMidi(true)` → `SetName(MakeUniqueTrackName("MIDI Track"))`
    → `notifyAboutTrackAdded(DomConverter::track(track))` → select+focus → pushHistoryState.
    ВАЖНО: `SetIsMidi(true)` ДО `notifyAboutTrackAdded`, т.к. DomConverter читает флаг.
  - `src/trackedit/internal/trackeditactionscontroller.{h,cpp}` — `NEW_MIDI_TRACK` const,
    добавлен в список подписок, `dispatcher()->reg(..., &TrackeditActionsController::newMidiTrack)`, метод-делегат.
  - `src/trackedit/internal/trackedituiactions.cpp` — `UiAction("new-midi-track", …, "New MIDI track")`.
  - `src/trackedit/tests/mocks/trackeditinteractionmock.h` — `MOCK_METHOD(bool, newMidiTrack, (), (override));`.

### UI
- `src/appshell/qml/Audacity/AppShell/appmenumodel.cpp` — `makeMenuItem("new-midi-track")` в меню Tracks.
- `src/projectscene/types/projectscenetypes.h` — `TrackTypes::Type::MIDI` (синхрон с trackedit::TrackType).
- `src/projectscene/view/trackspanel/paneltrackslistmodel.cpp`:
  - `addTrack()` — ветка `MIDI` → dispatch `new-midi-track`.
  - `buildTrackItem()` — `case Midi: new WaveTrackItem(this)` (КРИТИЧНО: без этого item=null → трек не строится).
- `src/projectscene/view/trackspanel/wavetrackitem.cpp` — `channelCount()` `case Midi: return 1`.
- `src/projectscene/view/trackspanel/trackitem.cpp` — `iconFromTrackType()` `case Midi: MUSIC_NOTES` (иконка ♪).
- QML само отправляет non-LABEL в waveform/clips (Loader-тернары в `TracksPanel.qml:214`,
  `TracksItemsView.qml:869`, `VerticalRulersPanel.qml:86`) — отдельный QML-компонент пока не нужен.

Проверено пользователем: MIDI-трек создаётся (иконка ♪, имя «MIDI Track»), ничего не падает,
моно/стерео/label работают как раньше.

## Модель нот + сериализация (СДЕЛАНО во 2-й сессии 2026-07-04)

Выбран вариант A (au3-attachment). Реализация:

### au3: `au3/libraries/au3-wave-track/MidiSequence.{h,cpp}` (НОВЫЕ файлы)
- `struct MidiNote { double startBeats; double lengthBeats; int pitch; float velocity; }`.
  **Единица времени — четвертная нота** (quarter). Обоснование: в au3
  `ProjectTimeSignature::GetQuarterDuration() = 60/tempo`, т.е. темп задан в четвертях/мин.
  Секунды = `startBeats * GetQuarterDuration()`.
- `class MidiSequence : ClientData::Cloneable<>, XMLTagHandler` — attachment на
  `ChannelGroup::Attachments` (точно по образцу `WaveTrackData`): `MidiSequence::Get(track)`,
  `Notes()/SetNotes()/AddNote()/Clear()/EndBeats()`.
  Клонируется вместе с треком → undo/redo/дубликация бесплатно.
- Сериализация: `<midisequence><note start=".." length=".." pitch=".." vel=".."/>…</midisequence>`
  как child `<wavetrack>` через `WaveTrackIORegistry::ObjectReaderEntry/ObjectWriterEntry`
  (образец — RealtimeEffectList в `au3/libraries/au3-effects/MixAndRender.cpp:230`).
  Пустой список нот НЕ пишется (обычные треки не замусориваются).
  Чтение: `HandleXMLChild("note")` возвращает `this`, парсинг атрибутов в `HandleXMLTag`
  (паттерн LabelTrack). Значения клампятся (pitch 0..127, vel 0..1, время ≥0).
- Добавлены в `au3/libraries/au3-wave-track/CMakeLists.txt`.
- ⚠️ Статические регистрации в MidiSequence.cpp выживают линковку потому, что TU
  используется из au3tracksinteraction.cpp (сид нот). Не удалять все ссылки разом.

### Сид тестовых нот (ВРЕМЕННО, до piano roll)
`Au3TracksInteraction::newMidiTrack()` (`src/trackedit/internal/au3/au3tracksinteraction.cpp`):
- засеивает C-мажорное арпеджио 8 четвертей (60,64,67,72,67,64,60,64), velocity 0.8;
- создаёт клип-контейнер: `track->InsertSilence(0, EndBeats()*quarterSec)` —
  на пустом треке InsertSilence сам создаёт клип (`WaveTrack.cpp:2142`);
- всё ДО `notifyAboutTrackAdded` (модель клипов читает состояние в этот момент).
Когда появится piano roll — сид убрать, создавать пустой клип фиксированной длины (напр. 4 такта).

### Превью нот: `src/projectscene/view/tracksitemsview/midinotesview.{h,cpp}` (НОВЫЕ файлы)
- `MidiNotesView : QQuickPaintedItem` по образцу соседнего `WaveView`: свойства
  `context` (TimelineContext), `clipKey`, `clipTime`, `noteColor`, readonly `isMidi`.
- paint(): находит au3-трек через `DomAccessor::findWaveTrack`, если `IsMidi()` — рисует
  ноты прямоугольниками. X: `(startBeats*quarterSec − clipTime.itemStartTime) * context.zoom()`.
  Y: диапазон питчей нот ±2 полутона, минимум октава; velocity → альфа.
- Обновление: connect к `TimelineContext::frameTimeChanged/zoomChanged` (скролл/зум).
- Зарегистрирован `qmlRegisterType<MidiNotesView>` в `projectscenemodule.cpp` (рядом с WaveView);
  добавлен в `src/projectscene/CMakeLists.txt`.
- Встроен в `ClipItem.qml` как ребёнок WaveView (anchors.fill, visible: isMidi,
  noteColor: ui.theme.fontPrimaryColor) — поверх плоской линии тишины.

## Тесты этапа 1 (проверить вручную в GUI)
- Создать MIDI-трек → на таймлайне клип ~4 сек (при 120 BPM) с 8 нотами-полосками (арпеджио вверх-вниз).
- Сохранить .aup4, закрыть, открыть → иконка ♪, клип и ноты на месте (в XML — `<midisequence>`).
- Зум/скролл таймлайна → ноты остаются приклеенными к клипу.
- Undo/Redo создания трека — не падает, трек исчезает/появляется с нотами.
- Обычные моно/стерео треки выглядят как раньше (превью не влезает: isMidi=false).

## Ловушки/заметки
- Двойной pushHistoryState: и `Au3TracksInteraction::newMidiTrack`, и `TrackeditOperationController::
  newMidiTrack` пушат историю — это ПОВТОРЯЕТ существующий паттерн mono/stereo (не мой баг).
  Если решим убрать дубли — править для всех типов сразу.
- `TrackTypes::Type` (projectscene) и `trackedit::TrackType` — ДВА enum, держать синхронными.
- Switch'и по TrackType с `default` компилируются, но проверять семантику для Midi
  (напр. dropcontroller.cpp `isAudioTrack` = Mono||Stereo — MIDI туда не входит; для этапа 1 ок).
