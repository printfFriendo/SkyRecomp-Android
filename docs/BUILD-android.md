# Building Sky Recomp for Android (arm64-v8a)

This guide details how to build, package, sign, and install **Sky Recomp** for Android devices.

---

## 1. System Requirements

### Target Device Requirements
- **Architecture**: `arm64-v8a` (64-bit ARM CPU).
- **Android Version**: Android 10.0+ (API Level 29 or higher). Fully compliant with Android 15+ 16KB page size devices.
- **GPU**: Vulkan 1.3 capable mobile GPU:
  - **Qualcomm Snapdragon**: Adreno 600, 700, and 800 series (Turnip or modern proprietary drivers).
  - **ARM Mali**: Bifrost, Valhall, and 5th Gen (Mali-G76, G77, G78, G710, Immortalis-G715/G720).
  - **MediaTek / Samsung Exynos**: Xclipse (RDNA2/RDNA3) or Mali.

### Host Development Requirements
- **Operating System**: Windows 10/11 x64, Linux x86_64, or macOS ARM64.
- **JDK**: Java Development Kit 17 or 21 (Eclipse Adoptium OpenJDK recommended).
- **Android SDK**: Build-Tools 36.0.0+ and SDK Platform API 34.
- **Android NDK**: NDK r28b (28.2.13676358).
- **CMake & Ninja**: CMake 3.22.1+ and Ninja build system.

---

## 2. Project Architecture & Dependencies

The Android port integrates the core recompilation engine into an Android package (`.apk`) using:
- **SDL3**: Native Android windowing, audio backend (AAudio/OpenSL ES), lifecycle, and multi-touch handling.
- **Static shaderc**: Compiles GLSL/HLSL shaders directly to SPIR-V on-device without runtime `.so` dependencies.
- **Bionic Compatibility Layer**: Virtual memory mapping (`MAP_FIXED` / TBI pointer masking), unwinding via `<unwind.h>`, and Android logcat integration.
- **On-Screen Touch Overlay**: Hardware-accelerated multi-touch virtual gamepad rendered via ImGui.

---

## 3. Building the Project

### Option A: One-Click Build Script (Windows)

From the project root:

```bat
:: Build both Debug and Signed Release APKs
build_android.bat

:: Build Signed Release APK only
build_android.bat release

:: Build Debug APK only
build_android.bat debug
```

### Option B: Gradle CLI (Cross-Platform)

Navigate to the `android/` directory:

```bash
cd android

# Linux / macOS:
./gradlew assembleRelease

# Windows:
gradlew.bat assembleRelease
```

Generated packages:
- **Release APK (Signed & Aligned)**:
  `android/app/build/outputs/apk/release/app-release.apk`
- **Debug APK**:
  `android/app/build/outputs/apk/debug/app-debug.apk`

---

## 4. Installation & Deployment

### Sideloading via ADB
Connect your Android device with USB Debugging enabled:

```bash
# Install the signed release APK
adb install -r android/app/build/outputs/apk/release/app-release.apk
```

Or run directly with the helper script:
```bat
build_android.bat install
```

---

## 5. Game Files & ROM Setup

Sky Recomp requires the extracted game assets from a legitimate copy of *The Legend of Zelda: Skyward Sword HD* (v1.0.0 or v1.0.1).

On Android, application data is located in the external app-specific storage:
```
/sdcard/Android/data/com.apolinario.skyrecomp/files/
```

### Directory Layout
```
/sdcard/Android/data/com.apolinario.skyrecomp/files/
├── romfs/                  # Extracted RomFS game assets
│   ├── Common/
│   ├── Object/
│   └── Stage/
├── saves/                  # User save files and configuration
├── skyrecomp.log           # Runtime stdout/stderr log
└── skyrecomp_crash.log     # Crash dump and native stack trace (if any)
```

Push game assets to your device:
```bash
adb push path/to/romfs /sdcard/Android/data/com.apolinario.skyrecomp/files/
```

---

## 6. Controls & On-Screen Overlay

The Android port includes a custom touch overlay:
- **Left Virtual Stick**: Movement / Free Flight.
- **Right Virtual Stick**: Camera control / Sword swinging.
- **D-Pad & Face Buttons (A, B, X, Y)**: Actions, jump, attack, interaction.
- **Shoulder Buttons (L, ZL, R, ZR)**: Targeting, shield, sword sheath.
- **Utility (+, -, Menu)**: Inventory, map, settings.
- **Android Back Gesture / Button**: Opens or toggles the in-game settings menu.
- **Physical Gamepads**: Bluetooth or USB controllers (Xbox, DualShock/DualSense, Switch Pro Controller) are recognized automatically via SDL3.

---

## 7. Diagnostics & Troubleshooting

### Real-Time Logcat Monitoring
To view logs, warnings, and shader compilation messages in real-time:

```bash
adb logcat -s SkyRecomp:V SDL:V Vulkan:V AndroidRuntime:E
```

Or via helper script:
```bat
build_android.bat logcat
```

### Crash Reports
If an unhandled exception or native signal occurs, SkyRecomp captures the registers and stack trace using Bionic's `<unwind.h>` and writes them to:
- `/sdcard/Android/data/com.apolinario.skyrecomp/files/skyrecomp_crash.log`
- Android Logcat tag `SkyRecomp` with log level `FATAL`.
