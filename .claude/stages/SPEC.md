# Audacity 4 → DAW: MIDI-секвенсер. Мастер-спецификация

> Цель форка: превратить Audacity 4 в мини-DAW — добавить MIDI-дорожки,
> piano roll (рисование нот), степ-секвенсер (драм-машина как в FL Studio),
> извлечение звука из VST-инструментов (VSTi) и запекание MIDI в аудио.
>
> Этот файл — точка входа. Детали по каждому этапу — в `stage_00.md` … `stage_05.md`.
> Документ писался как хэндофф между сессиями: читать перед продолжением работы.

Дата последнего обновления: 2026-07-04.
Ветка: `master`. Базовый коммит форка: `96bf131b8`.
Все изменения ниже пока **НЕ закоммичены** (лежат в рабочем дереве).

---

## 1. Ключевой контекст о кодовой базе

Это **Audacity 4** — переписанный на фреймворке **muse** (от MuseScore) UI поверх
старого движка **Audacity 3**, который вендорится внутри репозитория:

- `muse/` — фреймворк MuseScore (UI на Qt6/QML, IoC, actions, VST-хостинг, MIDI, аудио-движок).
- `au3/` — вендоренный движок Audacity 3 (WaveTrack, эффекты, VST3 SDK-хостинг, проект .aup4).
- `src/` — «клей» Audacity 4: appshell, trackedit, projectscene, effects, playback, au3wrap.
- `muse_deps/` — сабмодуль с пребилд-зависимостями muse.

### Что уже есть в коробке (важно — многое НЕ надо писать с нуля)

- **Ритм готов на 100%**: BPM + тайм-сигнатура (`src/trackedit/trackedittypes.h` → `TimeSignature{tempo,upper,lower}`),
  линейка Beats & Measures (`src/projectscene/view/timeline/beatsmeasuresformat.h`),
  снаппинг Bar/Half/Quarter/Eighth/… включая триоли (`src/projectscene/types/projectscenetypes.h` → `SnapType`).
  → Сетку для piano roll и степ-секвенсера НЕ надо изобретать.
- **Транспорт умеет не-wave источники**: `TransportSequences.otherPlayableSequences`
  (`au3/libraries/au3-mixer/AudioIOSequences.h`) — механизм, которым Audacity 3 играл MIDI. Жив.
- **VST3-хостинг**: обработка звука идёт через au3-шный `VST3Wrapper`
  (`au3/libraries/au3-vst3/`); muse-VST-модуль (`muse/framework/vst/`) используется
  ТОЛЬКО для показа окна («морды») плагина. Это важно: VSTi чиним в au3, а UI получаем бесплатно.
- **muse умеет больше, чем включено**: в `muse/framework/` есть полноценный MIDI-модуль
  (события MIDI 1.0/2.0, устройства ввода/вывода) и аудио-движок с FluidSynth (SoundFont sf2/sf3)
  и `VstSynthesiser`. Но `MUSE_MODULE_MIDI` и `MUSE_MODULE_AUDIO` **выключены** в корневом
  `CMakeLists.txt` (строки ~73 и ~85). Audacity играет через свой au3-движок, поэтому просто
  включить их недостаточно (muse-синт привязан к muse-аудио-движку).

### Мёртвый код, который НЕ трогаем

- au3 MIDI-наследие: `au3/libraries/au3-note-track/` (`NoteTrack`, `MIDIPlay`) и
  `au3/modules/track-ui/mod-midi-import-export/` (`ImportMIDI`/`ExportMIDI`).
  Всё под `#if defined(USE_MIDI)`, но `USE_MIDI` **нигде не определяется**, зависимости
  (portSMF/Allegro, portmidi) **не вендорены**, `au3-note-track` закомментирован в
  `au3/libraries/CMakeLists.txt` («not yet used in AU4»). Реанимировать НЕ будем.

---

## 2. Главное архитектурное решение

**MIDI-трек = обычный моно `WaveTrack`, помеченный флагом `IsMidi`, + список нот в долях + привязанный инструмент. Звук = рендер нот инструментом в аудио этого же трека (кэш/запекание).**

Почему так, а не новый тип трека уровня au3:

- Переиспользуется ВСЯ существующая инфраструктура: сериализация .aup4, микширование,
  отрисовка, undo/redo, копипаст. Новый au3-тип трека потребовал бы правок сериализации
  au3, TrackList, микшера — огромный объём и риск.
- Совпадает с планом «запекания MIDI в аудио» (этап 3): рендерим ноты инструментом в кэш
  → играем как обычный wave. Побочно даёт и мгновенное воспроизведение, и «bake to audio».
- Ноты храним **в долях** (beats), не в секундах — тогда смена темпа двигает ноты бесплатно.

Степ-секвенсер (драм-машина) — это НЕ отдельная сущность, а второй вид отображения того же
MIDI-клипа (строки-инструменты × колонки-шаги).

---

## 3. Окружение сборки (КРИТИЧНО — читать перед сборкой)

Проверено на машине пользователя 2026-07-04.

- **Компилятор**: MSVC (Visual Studio 2022 Community 17.14).
- **CMake 3.31.6 + Ninja 1.12.1** — идут в комплекте VS, но НЕ в PATH. Пути:
  - `C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe`
  - `C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe`
- **Qt 6.10.1** msvc2022_64 в `C:\Qt\6.10.1\msvc2022_64`.
  - Официальный `download.qt.io` отдаёт **403** (гео-блок). Ставилось через `aqtinstall` с зеркала
    `https://mirror.yandex.ru/mirrors/qt.io`. На зеркалах нет .sha256 → нужен
    `INSECURE_NOT_FOR_PRODUCTION_ignore_hash = True` в settings.ini aqt (пользователь одобрил,
    целостность сверена кросс-зеркально Яндекс==FAU).
  - Модули: `qt5compat qtnetworkauth qtshadertools qtwebsockets qtgraphs qtquick3d`.
- **Сабмодули**: `git submodule update --init` (muse, muse_deps — инициализированы).

### Команда конфигурации + сборки (рабочая, проверена)

Пресеты в `CMakePresets.json`: `audacity-debug` (build/audacity-debug) и `audacity-release`
(build/audacity-release, RelWithDebInfo). Из PowerShell:

```
cd C:\Users\dmitr\Documents\audacity
$vs = "C:\Program Files\Microsoft Visual Studio\2022\Community"
cmd /c "set PATH=$vs\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin;$vs\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja;%PATH% && call `"$vs\VC\Auxiliary\Build\vcvars64.bat`" >nul && cmake --preset audacity-release -DCMAKE_PREFIX_PATH=C:\Qt\6.10.1\msvc2022_64 -DCMAKE_C_COMPILER=cl -DCMAKE_CXX_COMPILER=cl && ninja -C build\audacity-release && cmake --install build\audacity-release"
```

Инкрементальная пересборка после правок (быстрее):
```
cmd /c "set PATH=<те же пути>;%PATH% && call `"$vs\VC\Auxiliary\Build\vcvars64.bat`" >nul && ninja -C build\audacity-release && cmake --install build\audacity-release"
```

### Ловушки сборки (наступали на них)

1. В PATH есть **gcc от Perl** (`C:\perl\c\bin\gcc.exe`). Без явных `-DCMAKE_C_COMPILER=cl
   -DCMAKE_CXX_COMPILER=cl` CMake берёт его и падает (`CXX17 … GNU does not support`). Всегда указывать cl.
2. В `cmd` `%PATH%` разворачивается при парсинге строки → `set PATH=...` ставить **ДО**
   `call vcvars64.bat`, иначе vcvars-изменения затираются, и `cl` не находится.
3. Первая конфигурация ~5 мин (собирает пребилд-зависимости), полная сборка ~13 мин,
   инкрементальная — минуты. Правка тяжёлых заголовков (напр. `WaveTrack.h`) тянет пересбор au3.

### Запуск бинарника

- exe: `C:\Users\dmitr\Documents\audacity\src\app\bin\Audacity4.exe`.
  Ярлык на рабочем столе: **«Audacity4 (наша сборка)»**.
- Debug ~99 МБ (`Qt6Cored.dll` рядом), Release ~70 МБ (`Qt6Core.dll`).
- **Обязательно** `cmake --install` после `ninja` — он деплоит Qt/зависимые DLL рядом с exe.
  Без install прямой запуск падает `0xC0000135` (DLL not found). Модули статически слинкованы в exe.
- **Использовать Release (RelWithDebInfo)**, не Debug: debug-сборка сыпет CRT-ассертами
  (диалог «Прервать/Повтор/Пропустить», часто от printf в коде плагинов) и медленная.
- AppData AU4: `C:\Users\dmitr\AppData\Local\audacity\Audacity4Development\`
  (`known_audio_plugins.json`, `logs\`). Au3-сторона отдельно: `AppData\Roaming\audacity\`.
- **Логи**: `…\Audacity4Development\logs\Audacity_YYMMDD_HHMMSS.log`. При краше в конце строка
  `crashCallback | Oops! Application crashed with signal: [11] SIGSEGV` (crashpad в dev выключен,
  минидампов нет).

### Headless-проверка плагинов (без GUI)

QML-диалоги НЕ автоматизируются (SendKeys/UIAutomation их не видят). Чтобы проверить
классификацию плагина без GUI:
```
Audacity4.exe --register-audio-plugin "C:\Program Files\Common Files\VST3\<plugin>.vst3"
```
→ пишет запись в `known_audio_plugins.json` (поле `"type"`: Generator/Effect/…), exit 0.

---

## 4. Статус этапов

| Этап | Тема | Статус |
|------|------|--------|
| [stage_00](stage_00.md) | Разблокировка VSTi (звук из инструментов) | ✅ Готово и проверено |
| [stage_01](stage_01.md) | Тип трека MIDI + меню + модель нот + сериализация | ✅ Готово и проверено (e2e 2026-07-05) |
| [stage_02](stage_02.md) | Piano roll (рисование нот) | 🟡 V1 собран (2026-07-05), ждёт проверки GUI |
| [stage_03](stage_03.md) | Звук MIDI-трека (VSTi рендер, воспроизведение, запекание) | 🟡 MVP собран (2026-07-05): инструмент + рендер; ждёт проверки |
| [stage_04](stage_04.md) | Степ-секвенсер (драм-машина) + простой сэмплер | ⬜ Не начато |
| [stage_05](stage_05.md) | Импорт/экспорт .mid, pitch bend/slide, живой MIDI-ввод | ⬜ Не начато |

Быстрый пересказ сделанного:
- **Этап 0**: VSTi теперь классифицируются как Generator, видны в меню Generate, играют
  (пока фиксированный тест-паттерн — C-мажорное арпеджио). Проверено: Synapse Legend HZ, Roland D-50
  генерируют звук. Известная проблема: у D-50 (Roland Cloud) падает открытие его «морды» —
  плагино-специфично, не архитектурно, отложено (см. stage_00 § «Известные проблемы»).
- **Этап 1**: `Tracks → New MIDI track` создаёт трек с иконкой ♪ и именем «MIDI Track»,
  тип пишется в .aup4. Во 2-й сессии 2026-07-04 доделано: модель нот
  `MidiSequence`/`MidiNote` (au3-attachment на WaveTrack, ноты в четвертных долях),
  сериализация `<midisequence>` в .aup4, превью нот в клипе (`MidiNotesView` в projectscene),
  временный сид тест-арпеджио + клип-тишина при создании трека. Детали — в stage_01.md.

---

## 5. Полный список изменённых файлов (все этапы, 2026-07-04, не закоммичено)

Этап 0 (VSTi):
- `au3/libraries/au3-vst3/VST3EffectBase.cpp` — kInstrument → EffectTypeGenerate.
- `au3/libraries/au3-vst3/VST3Wrapper.h` / `.cpp` — event-шины + очередь нот + IEventList в Process.
- `au3/libraries/au3-vst3/VST3Instance.h` / `.cpp` — тест-паттерн нот для генератора-инструмента.
- `src/effects/effects_base/view/pluginmanagertableviewmodel.cpp` — комментарий фильтра Unknown.

Этап 1 (MIDI-трек):
- `au3/libraries/au3-wave-track/MidiSequence.h` / `.cpp` — **НОВЫЕ**: модель нот + XML-сериализация (2-я сессия).
- `au3/libraries/au3-wave-track/CMakeLists.txt` — добавлены MidiSequence.{h,cpp}.
- `src/projectscene/view/tracksitemsview/midinotesview.h` / `.cpp` — **НОВЫЕ**: превью нот в клипе.
- `src/projectscene/CMakeLists.txt` — добавлены midinotesview.{h,cpp}.
- `src/projectscene/projectscenemodule.cpp` — qmlRegisterType<MidiNotesView>.
- `src/projectscene/qml/Audacity/ProjectScene/tracksitemsview/ClipItem.qml` — MidiNotesView внутри WaveView.
- `au3/libraries/au3-wave-track/WaveTrack.h` / `.cpp` — `IsMidi()/SetIsMidi()`, поле `mIsMidi`, XML-атрибут `ismidi`.
- `src/trackedit/dom/track.h` — `TrackType::Midi`.
- `src/au3wrap/internal/domconverter.cpp` — WaveTrack::IsMidi() → Midi.
- `src/trackedit/itrackeditinteraction.h`, `itracksinteraction.h` — `newMidiTrack()`.
- `src/trackedit/internal/trackeditinteraction.{h,cpp}` — обёртка.
- `src/trackedit/internal/trackeditoperationcontroller.{h,cpp}` — мост (делегирует + history).
- `src/trackedit/internal/au3/au3tracksinteraction.{h,cpp}` — реализация (appendWaveTrack+SetIsMidi+имя).
- `src/trackedit/internal/trackeditactionscontroller.{h,cpp}` — action `new-midi-track`.
- `src/trackedit/internal/trackedituiactions.cpp` — UiAction.
- `src/appshell/qml/Audacity/AppShell/appmenumodel.cpp` — пункт меню.
- `src/projectscene/types/projectscenetypes.h` — `TrackTypes::MIDI`.
- `src/projectscene/view/trackspanel/paneltrackslistmodel.cpp` — addTrack MIDI + buildTrackItem Midi→WaveTrackItem.
- `src/projectscene/view/trackspanel/wavetrackitem.cpp` — channelCount Midi→1.
- `src/projectscene/view/trackspanel/trackitem.cpp` — иконка MUSIC_NOTES.
- `src/trackedit/tests/mocks/trackeditinteractionmock.h` — mock newMidiTrack.

---

## 6. Как не потерять прогресс

Сборочные артефакты (`src/app/bin`, `build/`) — НЕ коммитить (это выхлоп сборки).
Исходные правки (27 файлов выше) стоит закоммитить в отдельную ветку, например:
```
git checkout -b feature/midi-sequencer
git add au3/ src/            # только исходники, не src/app/bin
git commit -m "Stage 0-1: VSTi unlock + MIDI track type"
```
`.claude/stages/` (эта документация) тоже стоит закоммитить.
