# Install Murmur

## Windows

1. Download `murmur-windows-experimental.zip` from the [Windows preview release](https://github.com/deltadasher/murmur/releases/tag/windows-preview-0.3.0) and extract it.
2. Install [VB-CABLE](https://vb-audio.com/Cable/) and restart Windows when prompted. You can close its setup and control-panel windows afterward.
3. Open `murmur.exe`, choose your microphone, and press **Start microphone**.
4. In Discord, OBS, or your game, choose **CABLE Output** as the microphone.

Keep the extracted files together. If Windows reports missing runtime files, install the x64 [Visual C++ Redistributable](https://learn.microsoft.com/en-us/cpp/windows/latest-supported-vc-redist). Don't use CABLE Input as your default speakers: other audio sent to it can be heard through CABLE Output.

### Build on Windows

Install Visual Studio with C++ support, CMake, and Qt 6.7+ with Qt Multimedia. In a terminal with Qt available:

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

Run `build/Release/murmur.exe` from the Qt development environment. To move it to another PC, deploy the Qt dependencies with `windeployqt` for both executables.

## Linux

Murmur needs PipeWire and WirePlumber. On Arch, install the dependencies:

```sh
sudo pacman -S --needed base-devel cmake ninja pkgconf qt6-base dbus pipewire wireplumber pipewire-pulse
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
./build/murmur
```

Choose your microphone, press **Start microphone**, then select **Murmur Virtual Microphone** in your calling app. To install Murmur for your user, run `python3 install-user.py` after building. For Flatpak instructions, see [PACKAGING.md](PACKAGING.md).
