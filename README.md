# 🖥️ LiveWallpaper Engine for Windows

> **Turn your desktop into a living, dynamic experience with high-performance video and shader-based live wallpapers.**

[![Platform: Windows 10 | 11](https://img.shields.io/badge/Platform-Windows%2010%20%7C%2011-0078D6?style=flat&logo=windows&logoColor=white)](https://www.microsoft.com/windows)
[![Tech Stack: C++17 | Rust | DirectX 11](https://img.shields.io/badge/Tech-C%2B%2B17%20%7C%20Rust%20%7C%20Direct3D11-4E9BCD?style=flat)](#%EF%B8%8F-architecture--tech-stack)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)

**LiveWallpaper** is a lightweight, high-performance desktop wallpaper engine built with **C++ (Direct3D 11)** and **Rust**. It seamlessly injects dynamic video playback and custom shader effects directly behind your Windows desktop icons using native Explorer WorkerW integration, creating an immersive desktop environment without sacrificing system responsiveness or gaming performance.

---

## ✨ Key Features

* 🎬 **Universal Video Playback**: Hardware-accelerated video decoding via Windows Media Foundation, supporting MP4, MKV, AVI, and other major video formats with smooth, glitch-free looping.
* 🦀 **Rust Core & HLSL Shaders**: Powered by a memory-safe Rust core library (`live_wallpaper_rust.dll`) and Direct3D 11 hardware acceleration for crisp 60+ FPS rendering with minimal CPU and GPU overhead.
* ⚡ **Smart Power & Game Focus Management**: Automatically pauses wallpaper rendering whenever a full-screen application or game is active (e.g., full-screen gaming or movie streaming) to free up 100% of your hardware resources.
* 💤 **Intelligent Idle Detection**: Pauses rendering automatically when your computer is unattended or idle, conserving power and extending hardware lifespan.
* 🛡️ **Explorer Watchdog Auto-Recovery**: Built-in resilience. If Windows Explorer (`explorer.exe`) ever crashes or restarts, LiveWallpaper automatically detects the event and re-injects the wallpaper seamlessly behind your icons without needing an application restart.
* 🎛️ **System Tray & Playlist Controls**: Effortlessly control your wallpaper from the Windows system tray. Create multi-video playlists, set custom auto-rotation intervals, pause playback, or switch wallpapers on the fly.

---

## 🚀 Quick Start Guide

### 1. Running the Application
1. Download the latest release or build the executable from source.
2. Ensure both `LiveWallpaper.exe` and `live_wallpaper_rust.dll` are in the same folder.
3. Double-click **`LiveWallpaper.exe`** to start the engine. The wallpaper will immediately attach to your desktop background.
4. An icon will appear in your **System Tray** (bottom-right taskbar near the clock) for easy control.

### 2. Using System Tray Controls
Right-click the **LiveWallpaper** system tray icon to access the menu:
* **▶️ Play / Pause**: Toggle live wallpaper rendering instantly.
* **➕ Add Video...**: Select any compatible video file (`.mp4`, etc.) from your computer to set it as your immediate desktop background.
* **📑 Manage Playlist...**: Open the playlist manager dialog to queue multiple video wallpapers, reorder them, and enable automatic cycling.
* **⏱️ Rotation Interval**: Set how frequently your wallpaper cycles to the next video in your playlist (e.g., *1 Minute*, *10 Minutes*, *1 Hour*).
* **🛑 Clear Playlist**: Stop rendering and return your desktop to your default static Windows wallpaper.

---

## ⚙️ Configuration & Customization

LiveWallpaper stores all user preferences and runtime logs in your user profile directory so your settings persist across updates:

* **Configuration File:** `%APPDATA%\LiveWallpaper\config.ini`  
  *(Example: `C:\Users\<YourUsername>\AppData\Roaming\LiveWallpaper\config.ini`)*
* **Runtime Log File:** `%APPDATA%\LiveWallpaper\log.txt`

### Example `config.ini`
```ini
[Settings]
VideoPath=C:\Users\Username\Videos\MyWallpaper.mp4
Playlist=C:\Users\Username\Videos\MyWallpaper.mp4|C:\Users\Username\Videos\SecondWallpaper.mp4
Paused=0
RotationInterval=10
IdleTimeout=5
```
* **`RotationInterval`**: Time in minutes between automatic playlist video transitions.
* **`IdleTimeout`**: Time in minutes of user inactivity before the engine automatically pauses rendering to save energy.

---

## 🛠️ Building from Source

If you are a developer or want to compile LiveWallpaper yourself, follow these steps:

### Prerequisites
* **OS:** Windows 10 or Windows 11
* **Compiler:** MSVC (Visual Studio 2019 / 2022) with C++17 support
* **Build System:** [CMake](https://cmake.org/download/) (v3.20 or higher)
* **Rust Toolchain:** Installed via [rustup](https://rustup.rs/) (`cargo`, `rustc`)

### Quick Build (Automated)
Run the included batch script from the project root directory in Command Prompt or PowerShell:
```cmd
build.bat
```
This script compiles the Rust core dynamic library and builds the C++ application automatically. The resulting executable `LiveWallpaper.exe` will be generated in the root or build output directory.

### Manual CMake Build
```powershell
# 1. Create and enter the build directory
mkdir build
cd build

# 2. Configure project for Release
cmake -DCMAKE_BUILD_TYPE=Release ..

# 3. Compile executable and dependencies
cmake --build . --config Release
```

For advanced testing, pipeline diagnostics, and architectural details, please refer to:
* 📘 [Build & Test Guide](BUILD_TEST_GUIDE.md)
* 📐 [Implementation Scope & Architecture](SCOPE.md)

---

## 🏗️ Architecture & Tech Stack

* **C++ Core**: Windowing, Explorer WorkerW injection (`src/explorer_integration.cpp`), Direct3D 11 device/swapchain management (`src/device_manager.cpp`, `src/swap_chain_manager.cpp`), and Media Foundation video decoding (`src/video_decoder.cpp`).
* **Rust Core Module**: High-performance backend library (`live_wallpaper_rust.dll`) bridging custom shaders and rendering pipelines via FFI (`ffi_shader_bridge`).
* **HLSL Shaders**: Direct hardware-accelerated visual effects (`src/tunnel_wallpaper.hlsl`, `src/default_wallpaper.hlsl`).

---

## 🤝 Contributing

Contributions, bug reports, and feature requests are welcome!
1. Fork the repository.
2. Create your feature branch (`git checkout -b feature/AmazingFeature`).
3. Commit your changes (`git commit -m 'Add some AmazingFeature'`).
4. Push to the branch (`git push origin feature/AmazingFeature`).
5. Open a Pull Request.

---

## 📄 License

This project is open-source and available under the **MIT License**.
