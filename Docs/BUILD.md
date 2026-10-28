# Сборка

Нужны CMake 3.22+, C++17, JUCE **8.0.4**. Workflow закрепляет JUCE на commit `51d11a2be6d5c97ccf12b4e5e827006e19f0555a`, CLAP extensions — на `7adee3a1bd4684d4caa5601100e364abccff4b4d` (как Clip Pocket). JUCE не вложен в архив.

## Только DSP, WAV CLI и тесты

```bash
cmake -S . -B build-dsp -DCMAKE_BUILD_TYPE=Release -DDISTORTION_BUILD_PLUGIN=OFF
cmake --build build-dsp --config Release --parallel 2
ctest --test-dir build-dsp -C Release --output-on-failure
```

Для ASan/UBSan добавь `-DDISTORTION_SANITIZER=address` на GCC/Clang. Sanitizer требует отдельный build-каталог. Тесты используют явные проверки, действующие и в Release. HarmonicCalibration подключается при наличии Python 3; C++ калибровки остаются обязательными и без Python.

```bash
build-dsp/DistortionAnalyze pre.wav post.wav --output result.json
python Tools/calibrate.py --analyzer build-dsp/DistortionAnalyze --output calibration
```

CLI принимает PCM16/24/32 и float32/64, mono/stereo RIFF WAV. Требуются равные Fs и количество кадров; нормализация и ресемплинг отсутствуют. Каждый результат относится к окну, следующему с шагом 1 с. `percent: null` означает отсутствие достоверного числа.

## Windows x64

Visual Studio 2022 с Desktop development with C++, CMake, Git. Получи JUCE 8.0.4:

```powershell
git clone --depth 1 --branch 8.0.4 https://github.com/juce-framework/JUCE.git JUCE
cmake -S . -B build -A x64 -DJUCE_DIR="$PWD/JUCE" -DDISTORTION_FORMATS="VST3;AAX" -DDISTORTION_ENABLE_CLAP=ON -DDISTORTION_BUILD_UI_TESTS=ON
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

Результаты: `build/Distortion_artefacts/Release/VST3/Distortion.vst3` и `CLAP/Distortion.clap`. VST3 помещается в `C:\Program Files\Common Files\VST3`; CLAP — в `C:\Program Files\Common Files\CLAP`.

Из Clip Pocket сохранены `/utf-8`, `NOMINMAX`, `WIN32_LEAN_AND_MEAN` и проверяемое по hash исправление Windows VBlank из JUCE 8.0.5. Неизвестная версия исходника VBlank вызывает явную ошибку, а не молчаливую перезапись. OpenGL для этого маленького интерфейса не нужен.

## macOS Universal

```bash
git clone --depth 1 --branch 8.0.4 https://github.com/juce-framework/JUCE.git JUCE
cmake -S . -B build -G Xcode -DJUCE_DIR="$PWD/JUCE" -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" -DCMAKE_OSX_DEPLOYMENT_TARGET=11.0 -DDISTORTION_FORMATS="VST3;AAX;AU" -DDISTORTION_BUILD_UI_TESTS=ON
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

AU устанавливается в `~/Library/Audio/Plug-Ins/Components`, VST3 — в `~/Library/Audio/Plug-Ins/VST3`, CLAP — в `~/Library/Audio/Plug-Ins/CLAP`. Метаданные AU: `aufx DsRn RnLn`.

Workflow сохраняет режим подписи Clip Pocket и секреты `MACOS_CERTIFICATE`, `MACOS_CERTIFICATE_PASSWORD`, `MACOS_KEYCHAIN_PASSWORD`, `MACOS_SIGNING_IDENTITY`. Без сертификата — явный AdHoc; notarization отсутствует. AAX собирается с bundled SDK JUCE, либо с заданным `AAX_SDK_PATH`; Developer-пакет не PACE-подписан.

## Linux для проверки

Пакеты Ubuntu: `cmake ninja-build pkg-config libasound2-dev libx11-dev libxext-dev libxrandr-dev libxinerama-dev libxcursor-dev libfreetype-dev libfontconfig1-dev libgl1-mesa-dev xvfb`.

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DJUCE_DIR=/absolute/path/to/JUCE -DDISTORTION_BUILD_UI_TESTS=ON
cmake --build build --parallel 2
xvfb-run -a ctest --test-dir build --output-on-failure
```

## Зависимости

Inter Regular/Medium — исходные шрифты Clip Pocket, SIL OFL в `Assets/Fonts/Inter-OFL.txt`. Лицензии CLAP и extensions сохраняются в `Assets/Licenses`. JUCE и bundled SDK распространяются по собственным условиям, указанным в их исходниках; этот проект не меняет их лицензирование. Инструментальные тестовые WAV созданы математическим генератором проекта.
