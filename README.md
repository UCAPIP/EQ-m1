# EQ-m1

[English](#english) | [Русский](#русский)

---

## English

A parametric equalizer in VST3 format (plus a Standalone app for quick testing without a DAW). Written in C++17 on top of JUCE 8; the DSP core does not depend on JUCE and is covered by separate tests.

Inspired by FabFilter Pro-Q: draggable nodes on a graph for boosting and cutting frequencies, and solo listening of the selected spectrum segment, which is the main feature of this plugin.

### Prebuilt Windows version

A ready-to-use Windows build is in the [`dist`](dist) folder. To install, copy the `EQ-m1.vst3` bundle folder into `C:\Program Files\Common Files\VST3\` and restart your DAW. The Standalone app can be run directly.

### Features

16 bands, each independently configurable:

| Parameter | Values |
|---|---|
| Filter type | Bell, Low Shelf, High Shelf, Low Pass, High Pass, Notch, Band Pass, All Pass |
| LP/HP slope | 6 / 12 / 24 / 48 / 96 dB/oct (Butterworth cascades, exactly −3 dB at the cutoff) |
| Frequency | 10 Hz … 30 kHz, logarithmic scale |
| Gain | ±30 dB |
| Q | 0.05 … 24 |
| Channel | Stereo / L only / R only / Mid only / Side only |
| On/off | per-band bypass with smooth crossfade |
| Solo | listen to this band only |

**Segment solo.** Every node has a solo button: the signal goes through an isolating 4th-order band-pass filter around the node frequency, and then through the band's own filter, so you hear exactly what this node does to its part of the spectrum. The width of the listening window is set by the global **Solo width** parameter (0.05…8 octaves). Solo of several bands is summed, so you can listen to two segments at once. Solo switching uses a ~30 ms crossfade, with no clicks.

**Other**

- Spectrum analyzer: input (blue) and output (amber), multi-resolution with four decimated stages (×1, ×4, ×16, ×64) with FIR anti-aliasing, merged into 600 log bands, so the low end is as detailed as the high end. Peak-hold with adjustable decay, Freeze/Slow/Medium/Fast modes, 12/24/48/96 dB range.
- Auto gain: overall loudness compensation using a log-averaged (pink) curve, with a smooth ramp-in; in solo mode the compensation is smoothly disabled so you hear the real level of the band.
- Global bypass with crossfade, output gain ±24 dB.
- Zero latency, float and double precision, stereo and mono buses.
- 9 factory presets (VST3 programs) + state saving by the host.

### Mouse and keyboard controls

| Action | Result |
|---|---|
| Double-click on the graph | create a band at this frequency (first free slot is used) |
| Drag a node | frequency (X) and gain (Y); for filters without gain: frequency and Q |
| Ctrl/Cmd + drag | Q |
| Wheel over a node | Q |
| Ctrl/Cmd + wheel over background | vertical scale (±6…±30 dB) |
| Shift + drag | fine adjustment (0.25×) |
| Double-click on a node | enable/disable the band |
| Right-click on a node | type, channel, slope, solo, resets, delete |
| Right-click on background | add band, analyzer, solo width, vertical scale, reset all bands |
| ← / → | selected band frequency by a semitone (Shift: 1/10 semitone) |
| ↑ / ↓ | gain by 0.5 dB (Shift: 0.1 dB); for LP/HP: Q |
| S | toggle solo of the selected band |
| Delete / Backspace | disable the band |
| Esc | clear selection |

### Project structure

```
Source/
  DSP/                 core, no JUCE includes: builds and tests anywhere
    DspTypes.h         band types, ranges, comparison operators, octave <-> Q conversion
    Biquad.h/.cpp      RBJ biquad designs, Butterworth cascades, TDF2, frequency response
    Curve.h/.cpp       cascade response, auto gain, log binning of the spectrum
    EqEngine.h/.cpp    parameter smoothing, M/S, bypass, solo path, sub-block processing
  Processor/
    Params.h/.cpp      parameter IDs/ranges/formats, factory presets
    SpectrumAnalyser.* FFT (juce::dsp) + lock-free FIFO of frames to the GUI
    EqM1AudioProcessor.*
  Editor/
    EqView.*           graph: grid, spectra, curve, nodes, mouse/keyboard, context menus
    ControlBar.*       bottom panel: selected band + global parameters
    EqM1AudioProcessorEditor.*
  PluginMain.cpp       createPluginFilter()
Tests/DspTests.cpp     208 core checks, no external dependencies
Tests/IntegrationTests.cpp  82 JUCE-layer checks, headless (no windows opened)
docker/Dockerfile.build  image with JUCE dependencies for Linux builds
scripts/               build.sh, test.sh
dist/                  prebuilt Windows version
```

### CMake targets

| Target | Description |
|---|---|
| `EQ_M1_VST3`, `EQ_M1_Standalone` | plugin and standalone app |
| `EQM1Tests` | DSP core tests, JUCE not needed (`-DEQM1_BUILD_PLUGIN=OFF` builds only these) |
| `EQM1Integration` | headless JUCE-layer tests: parameters, presets, state, processBlock, editor rendering |

### Build

Requires CMake ≥ 3.22 and a C++17 compiler. JUCE 8.0.16 is downloaded automatically via FetchContent (access to github.com is required).

#### Windows (VST3)

MSVC is required: JUCE 8 does not support MinGW (`juce_TargetPlatform.h` explicitly raises `#error "MinGW is not supported"`), so cross-building from Linux is not possible.

Install Visual Studio 2022 (full IDE with the "Desktop development with C++" workload, or just Build Tools). If the system drive is low on space, the installation can be moved to another drive:

```bat
winget install Microsoft.VisualStudio.2022.BuildTools --override "--quiet --wait --installPath D:\VSBuildTools --path cache=D:\VSCache --path shared=D:\VSShared --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended"
```

Note: the Windows SDK (~2.5 GB) is still installed on C: and cannot be redirected.

Build (verified option: MSVC + Ninja, both included in the workload):

```bat
call "D:\VSBuildTools\VC\Auxiliary\Build\vcvars64.bat"
cmake -S . -B build-win -G Ninja -DCMAKE_BUILD_TYPE=Release -DEQM1_BUILD_TESTS=ON
cmake --build build-win
```

Or with the Visual Studio generator: `cmake -S . -B build -G "Visual Studio 17 2022" -A x64` and `cmake --build build --config Release`.

In `cmd`, do not write `set "PATH=...;%PATH%"` on the same line as `call vcvars64.bat`: `%PATH%` is expanded before the line runs and overwrites the MSVC path that vcvars has just set.

Output:

- `build-win\EQ_M1_artefacts\Release\VST3\EQ-m1.vst3\Contents\x86_64-win\EQ-m1.vst3`: the plugin
- `build-win\EQ_M1_artefacts\Release\Standalone\EQ-m1.exe`: can be launched and checked right away

Installing the plugin: copy the whole `EQ-m1.vst3` bundle folder into `C:\Program Files\Common Files\VST3\` and restart your DAW.

Run the tests (DSP core and headless JUCE layer):

```bat
build-win\EQM1Tests.exe
build-win\EQM1Integration_artefacts\Release\EQM1Integration.exe
```

The DSP core tests can also be built without JUCE:

```bat
cmake -S . -B build-test -DEQM1_BUILD_PLUGIN=OFF
cmake --build build-test --config Release
build-test\Release\EQM1Tests.exe
```

If JUCE is already available locally, you can skip downloading it: `cmake -S . -B build -DEQM1_JUCE_DIR=C:\path\to\JUCE`.

#### Linux / WSL

```bash
sudo apt-get install -y build-essential cmake ninja-build git pkg-config \
  libasound2-dev libjack-jackd2-dev libcurl4-openssl-dev libfreetype-dev \
  libx11-dev libxcomposite-dev libxcursor-dev libxext-dev libxi-dev libxinerama-dev \
  libxrandr-dev libxrender-dev libxkbcommon-dev libglu1-mesa-dev mesa-common-dev \
  libwebkit2gtk-4.1-dev
./scripts/build.sh          # configure + build into build/
./scripts/test.sh           # DSP core tests only
```

#### macOS

```bash
cmake -S . -B build -G Xcode
cmake --build build --config Release
```

### Verification

**EQM1Tests** (208 checks): frequency response of all filter types, including exact match with the Butterworth formula corrected for bilinear-transform warping; 6…96 dB/oct slopes; band cascading; auto gain; analyzer log binning; all channel modes (including Mid/Side); solo monitoring (own band passes, other bands are suppressed, window width effect, sum of several solos); no clicks when automating gain/frequency/band enable/filter type change, verified against a reference slew-rate static run; stability at block sizes 1…2048.

**EQM1Integration** (82 checks): JUCE layer without opening windows: parameter count and names, round-trip of logarithmic ranges (`convertTo0to1(convertFrom0to1(x)) == x`), accuracy of extreme values, transparency of a fresh instance, +6 dB gain through a real `processBlock`, solo through the whole parameter chain, mono/stereo bus support and rejection of unsuitable layouts, all 9 presets, state round-trip and ignoring of a corrupted blob, extreme values of all 135 parameters, 192 kHz and frequency above Nyquist, analyzer pipeline (peak lands at 1 kHz), editor creation and rendering into `juce::Image` at different sizes, with 16 enabled bands, with the solo badge, in bypass and with no selected band.

Results on the test machine (JUCE 8.0.16):

| Platform | Compiler | Build | DSP tests | Integration |
|---|---|---|---|---|
| Windows x64 | MSVC 14.44 (VS Build Tools 17.14) + Ninja | 0 errors, 0 warnings | 208 / 208 | 82 / 82 |
| Linux x64 | g++ 15.2 (Ubuntu 26.04, WSL2) + Ninja | 0 errors, 0 warnings | 208 / 208 | 82 / 82 |

```
DSP core:        passed: 208   failed: 0   RESULT: OK
JUCE layer:      passed:  82   failed: 0   RESULT: OK
```

Windows bundle: `EQ-m1.vst3\Contents\x86_64-win\EQ-m1.vst3` (PE32+ x64, exports `GetPluginFactory`) + `moduleinfo.json`, category `Fx | Equalizer`, VST 3.8.0. Linux bundle: `EQ-m1.vst3/Contents/x86_64-linux/EQ-m1.so`.

Cross-building for Windows from Linux does not work: JUCE 8 does not support MinGW.

### Intentionally not implemented

Compared to Pro-Q 2/3, there is no: linear phase / natural phase mode (zero latency only), dynamic EQ, automatic resonance detection, M/S decoder for the whole bus, A/B comparison, or "frozen" snapshot analyzer. All of this can be added on top of the current core: `EqEngine` already works with a ready parameter snapshot `EngineInput`.

### Threading notes

- The audio thread reads parameters via `std::atomic<float>`, allocates nothing and never blocks. Coefficients are recalculated only while parameters are actually moving (sub-blocks of 32 samples).
- Auto gain is recalculated on the audio thread only when parameters change (~50 µs).
- The GUI and the audio thread exchange spectrum frames through `juce::AbstractFifo` (single producer, single consumer, lock-free). The frequency response curve in the GUI is computed independently from parameter values, so there is no shared mutable data between threads.

---

## Русский

Параметрический эквалайзер в формате VST3 (плюс Standalone-приложение для быстрой проверки без DAW). Написан на C++17 поверх JUCE 8; DSP-ядро не зависит от JUCE и покрыто отдельными тестами.

Прототип — FabFilter Pro-Q: узлы-точки на графике, которыми поднимают и опускают частоты, и сольное прослушивание выбранного участка спектра (solo) — главная фича этого плагина.

### Готовая версия для Windows

Готовая собранная версия для Windows лежит в папке [`dist`](dist). Для установки скопируйте папку-бандл `EQ-m1.vst3` целиком в `C:\Program Files\Common Files\VST3\` и перезапустите DAW. Standalone-приложение можно запускать напрямую.

### Возможности

16 полос, у каждой независимо:

| Параметр | Значения |
|---|---|
| Тип фильтра | Bell, Low Shelf, High Shelf, Low Pass, High Pass, Notch, Band Pass, All Pass |
| Наклон LP/HP | 6 / 12 / 24 / 48 / 96 дБ/окт (каскады Баттерворта, точный −3 дБ на срезе) |
| Частота | 10 Гц … 30 кГц, логарифмическая шкала |
| Gain | ±30 дБ |
| Q | 0.05 … 24 |
| Канал | Stereo / L only / R only / Mid only / Side only |
| Вкл/выкл | отдельный bypass полосы с плавным кроссфейдом |
| Solo | прослушивание только этой полосы |

**Сольное прослушивание сегмента.** У каждой точки есть solo: сигнал пропускается через изолирующий полосовой фильтр 4-го порядка вокруг частоты узла, а затем через сам фильтр полосы, поэтому слышно именно то, что этот узел делает со своим участком. Ширину окна слушаемого диапазона задаёт глобальный параметр **Solo width** (0.05…8 октав). Solo нескольких полос суммируется — можно слушать два участка сразу. Переключение solo делается кроссфейдом ~30 мс, без щелчков.

**Прочее**

- Анализатор спектра: вход (синий) и выход (янтарный), мультиразрешение — четыре декадированные ступени (×1, ×4, ×16, ×64) с FIR-антиалиасингом, слитые в 600 лог-полос, поэтому низ читается так же детально, как верх; peak-hold с регулируемым спадом, режимы Freeze/Slow/Medium/Fast, диапазон 12/24/48/96 дБ.
- Auto gain — компенсация общей громкости по лог-усреднённой (розовой) кривой, с плавным входом; в режиме solo компенсация плавно отключается, чтобы слышать реальный уровень полосы.
- Общий bypass с кроссфейдом, выходной гейн ±24 дБ.
- Нулевая задержка (zero latency), float и double precision, стерео и моно шины.
- 9 заводских пресетов (программы VST3) + сохранение состояния хостом.

### Управление мышью и клавиатурой

| Действие | Результат |
|---|---|
| Двойной клик по графику | создать полосу в этой частоте (берётся первый свободный слот) |
| Перетаскивание узла | частота (X) и gain (Y); у фильтров без gain — частота и Q |
| Ctrl/Cmd + перетаскивание | Q |
| Колесо над узлом | Q |
| Ctrl/Cmd + колесо над фоном | вертикальный масштаб (±6…±30 дБ) |
| Shift + перетаскивание | точная подстройка (0.25×) |
| Двойной клик по узлу | включить/выключить полосу |
| Правый клик по узлу | тип, канал, наклон, solo, сбросы, удаление |
| Правый клик по фону | добавить полосу, анализатор, solo width, вертикальный масштаб, сброс всех полос |
| ← / → | частота выбранной полосы на полутон (Shift — на 1/10 полутона) |
| ↑ / ↓ | gain на 0.5 дБ (Shift — 0.1 дБ); у LP/HP — Q |
| S | вкл/выкл solo выбранной полосы |
| Delete / Backspace | выключить полосу |
| Esc | снять выделение |

### Структура проекта

```
Source/
  DSP/                 ядро, без единого include'а JUCE — компилируется и тестируется где угодно
    DspTypes.h         типы полос, диапазоны, операторы сравнения, перевод октавы <-> Q
    Biquad.h/.cpp      RBJ-дизайны биквадов, каскады Баттерворта, TDF2, АЧХ
    Curve.h/.cpp       АЧХ каскада, auto gain, лог-биннинг спектра
    EqEngine.h/.cpp    сглаживание параметров, M/S, bypass, solo-тракт, процессинг по подблокам
  Processor/
    Params.h/.cpp      ID/диапазоны/форматы параметров, заводские пресеты
    SpectrumAnalyser.* FFT (juce::dsp) + lock-free FIFO кадров в GUI
    EqM1AudioProcessor.*
  Editor/
    EqView.*           график: сетка, спектры, кривая, узлы, мышь/клавиатура, контекстные меню
    ControlBar.*       нижняя панель: выбранная полоса + глобальные параметры
    EqM1AudioProcessorEditor.*
  PluginMain.cpp       createPluginFilter()
Tests/DspTests.cpp     208 проверок ядра, без внешних зависимостей
Tests/IntegrationTests.cpp  82 проверки JUCE-слоя, headless (без открытия окон)
docker/Dockerfile.build  образ с зависимостями JUCE для Linux-сборки
scripts/               build.sh, test.sh
dist/                  готовая собранная версия для Windows
```

### Цели CMake

| Цель | Что это |
|---|---|
| `EQ_M1_VST3`, `EQ_M1_Standalone` | плагин и автономное приложение |
| `EQM1Tests` | тесты DSP-ядра, JUCE не нужен (`-DEQM1_BUILD_PLUGIN=OFF` собирает только их) |
| `EQM1Integration` | headless-тесты JUCE-слоя: параметры, пресеты, состояние, processBlock, отрисовка редактора |

### Сборка

Нужны CMake ≥ 3.22 и компилятор C++17. JUCE 8.0.16 скачивается сам через FetchContent (нужен доступ к github.com).

#### Windows (VST3)

Нужен MSVC: JUCE 8 не поддерживает MinGW (`juce_TargetPlatform.h` прямо выдаёт `#error "MinGW is not supported"`), поэтому кросс-сборка из Linux невозможна.

Поставьте Visual Studio 2022 — полноценную IDE с workload «Desktop development with C++» или только Build Tools. Если на системном диске мало места, установку можно увести на другой диск:

```bat
winget install Microsoft.VisualStudio.2022.BuildTools --override "--quiet --wait --installPath D:\VSBuildTools --path cache=D:\VSCache --path shared=D:\VSShared --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended"
```

Учтите: Windows SDK (~2.5 ГБ) всё равно ставится на C: и не перенаправляется.

Сборка (проверенный вариант — MSVC + Ninja, оба входят в workload):

```bat
call "D:\VSBuildTools\VC\Auxiliary\Build\vcvars64.bat"
cmake -S . -B build-win -G Ninja -DCMAKE_BUILD_TYPE=Release -DEQM1_BUILD_TESTS=ON
cmake --build build-win
```

Либо через генератор Visual Studio: `cmake -S . -B build -G "Visual Studio 17 2022" -A x64` и `cmake --build build --config Release`.

В `cmd` нельзя писать `set "PATH=...;%PATH%"` в одной строке с `call vcvars64.bat`: `%PATH%` раскрывается до выполнения строки и затирает путь к MSVC, который только что выставил vcvars.

Результат:

- `build-win\EQ_M1_artefacts\Release\VST3\EQ-m1.vst3\Contents\x86_64-win\EQ-m1.vst3` — плагин
- `build-win\EQ_M1_artefacts\Release\Standalone\EQ-m1.exe` — можно запустить и проверить сразу

Установка плагина: скопируйте папку-бандл `EQ-m1.vst3` целиком в `C:\Program Files\Common Files\VST3\` и перезапустите DAW.

Проверить тестами (DSP-ядро и JUCE-слой headless):

```bat
build-win\EQM1Tests.exe
build-win\EQM1Integration_artefacts\Release\EQM1Integration.exe
```

Тесты DSP-ядра можно собрать и без JUCE:

```bat
cmake -S . -B build-test -DEQM1_BUILD_PLUGIN=OFF
cmake --build build-test --config Release
build-test\Release\EQM1Tests.exe
```

Если JUCE уже лежит локально, можно не качать её заново: `cmake -S . -B build -DEQM1_JUCE_DIR=C:\path\to\JUCE`.

#### Linux / WSL

```bash
sudo apt-get install -y build-essential cmake ninja-build git pkg-config \
  libasound2-dev libjack-jackd2-dev libcurl4-openssl-dev libfreetype-dev \
  libx11-dev libxcomposite-dev libxcursor-dev libxext-dev libxi-dev libxinerama-dev \
  libxrandr-dev libxrender-dev libxkbcommon-dev libglu1-mesa-dev mesa-common-dev \
  libwebkit2gtk-4.1-dev
./scripts/build.sh          # configure + build в build/
./scripts/test.sh           # только тесты DSP-ядра
```

#### macOS

```bash
cmake -S . -B build -G Xcode
cmake --build build --config Release
```

### Проверка

**EQM1Tests** (208 проверок) — АЧХ всех типов фильтров, включая точное совпадение с формулой Баттерворта с поправкой на варпинг билинейного преобразования; наклоны 6…96 дБ/окт; каскадирование полос; auto gain; лог-биннинг анализатора; все канальные режимы (в том числе Mid/Side); solo-мониторинг (прохождение своей полосы, подавление чужой, влияние ширины окна, сумма нескольких solo); отсутствие щелчков при автоматизации gain/частоты/включения полосы/смены типа фильтра — сравнением с эталонным slew-rate статического прогона; устойчивость на размерах блока 1…2048.

**EQM1Integration** (82 проверки) — JUCE-слой без открытия окон: количество и имена параметров, round-trip логарифмических диапазонов (`convertTo0to1(convertFrom0to1(x)) == x`), точность крайних значений, прозрачность свежего экземпляра, усиление +6 дБ через реальный `processBlock`, solo через всю цепочку параметров, поддержка mono/stereo шин и отклонение неподходящих раскладок, все 9 пресетов, round-trip состояния и игнорирование повреждённого blob, экстремальные значения всех 135 параметров, 192 кГц и частота выше Найквиста, конвейер анализатора (пик попадает на 1 кГц), создание редактора и его отрисовка в `juce::Image` на разных размерах, с 16 включёнными полосами, с solo-бейджем, в bypass и без выделенной полосы.

Результат на машине проверки (JUCE 8.0.16):

| Платформа | Компилятор | Сборка | DSP-тесты | Интеграционные |
|---|---|---|---|---|
| Windows x64 | MSVC 14.44 (VS Build Tools 17.14) + Ninja | 0 ошибок, 0 предупреждений | 208 / 208 | 82 / 82 |
| Linux x64 | g++ 15.2 (Ubuntu 26.04, WSL2) + Ninja | 0 ошибок, 0 предупреждений | 208 / 208 | 82 / 82 |

```
DSP core:        passed: 208   failed: 0   RESULT: OK
JUCE layer:      passed:  82   failed: 0   RESULT: OK
```

Windows-бандл: `EQ-m1.vst3\Contents\x86_64-win\EQ-m1.vst3` (PE32+ x64, экспортирует `GetPluginFactory`) + `moduleinfo.json`, категория `Fx | Equalizer`, VST 3.8.0. Linux-бандл: `EQ-m1.vst3/Contents/x86_64-linux/EQ-m1.so`.

Кросс-сборка Windows из Linux не работает: JUCE 8 не поддерживает MinGW.

### Что осознанно не реализовано

По сравнению с Pro-Q 2/3 здесь нет: linear phase / natural phase режимов (только zero latency), динамической эквализации, автоматического поиска резонансов, M/S-декодера для всей шины, A/B-сравнения и анализатора в виде «замороженного» снимка. Всё это можно добавить поверх текущего ядра: `EqEngine` уже работает с готовым снимком параметров `EngineInput`.

### Замечания по потокам

- Аудиопоток читает параметры через `std::atomic<float>`, ничего не выделяет и не блокируется. Коэффициенты пересчитываются только пока параметры реально движутся (подблоки по 32 отсчёта).
- Auto gain пересчитывается на аудиопотоке только в момент изменения параметров (~50 мкс).
- GUI и аудиопоток обмениваются кадрами спектра через `juce::AbstractFifo` (один производитель, один потребитель, без локов). Кривая АЧХ в GUI считается независимо из значений параметров, поэтому общих изменяемых данных между потоками нет.