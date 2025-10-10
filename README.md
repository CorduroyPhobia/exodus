<div align="center">

# Exodus

[![C++](https://img.shields.io/badge/C%2B%2B-17-blue)](https://github.com/ExodusTeam/exodus)
[![License MIT](https://badgen.net/github/license/ExodusTeam/exodus)](https://github.com/ExodusTeam/exodus/blob/main/LICENSE)
[![GitHub stars](https://img.shields.io/github/stars/ExodusTeam/exodus?color=ffb500)](https://github.com/ExodusTeam/exodus)
[![Discord server](https://badgen.net/discord/online-members/exodus)](https://discord.gg/37WVp6sNEh)

  <p>
    <a href="https://github.com/ExodusTeam/exodus/releases" target="_blank">
      <strong>Download the latest release</strong>
    </a>
  </p>
</div>

---

# Ready-to-Use Builds (Recommended)

**You do NOT need to compile anything if you just want to use the aimbot!**
Precompiled `.exe` builds are provided with DirectML (works on every modern GPU).

---

### DirectML (DML) Build — Universal (All GPUs)

* **Works on:**

  * Any modern GPU (NVIDIA, AMD, Intel, including integrated graphics)
  * Windows 10/11 (x64)
  * No special drivers required
* **Recommended for:**

  * GTX 10xx/9xx/7xx series (older NVIDIA)
  * Any AMD Radeon or Intel Iris/Xe GPU
  * Laptops and office PCs with integrated graphics
* **Download DML build:**
  [DirectML Release](https://disk.yandex.ru/d/jAVoMDfCuloNzw)

**The DirectML build is ready-to-use: just download, unpack, run `ai.exe` and follow instructions in the overlay.**

---

## How to Run (For Precompiled Builds)

1. **Download and unpack the DirectML release.**
2. **Run `ai.exe`.**
   On first launch, the model will be exported (may take up to 5 minutes).
3. Place your `.onnx` model in the `models` folder and select it in the overlay (HOME key).
4. All settings are available in the overlay.
   Use the HOME key to open/close overlay.

### Controls

* **Right Mouse Button:** Aim at the detected target
* **F2:** Exit
* **F3:** Pause aiming
* **F4:** Reload config
* **Home:** Open/close overlay and settings

---

# Build From Source (Advanced Users)

If you want to compile the project yourself or modify code, follow these instructions.

## 1. Requirements

* **Visual Studio 2022 Community** ([Download](https://visualstudio.microsoft.com/vs/community/))
* **Windows 10 or 11 (x64)**
* **Windows SDK 10.0.26100.0** or newer
* **CMake** ([Download](https://cmake.org/))
* **OpenCV 4.10.0** (pre-built binaries work great for DirectML)
* Other dependencies:

  * [simpleIni](https://github.com/brofield/simpleini/blob/master/SimpleIni.h)
  * [serial](https://github.com/wjwwood/serial)
  * [GLFW](https://www.glfw.org/download.html)
  * [ImGui](https://github.com/ocornut/imgui)

---

## 2. Choose Build Target in Visual Studio

* Select `Release | x64 | DML` (works on any modern GPU)

---

## 3. Placement of Third-Party Modules and Libraries

Before building the project, **download and place all third-party dependencies** in the following directories inside your project structure:

**Required folders inside your repository:**

```
exodus/
└── exodus/
    └── modules/
```

**Place each dependency as follows:**

| Library   | Path                                                              |
| --------- | ----------------------------------------------------------------- |
| SimpleIni | `exodus/exodus/modules/SimpleIni.h`         |
| serial    | `exodus/exodus/modules/serial/`             |
| GLFW      | `exodus/exodus/modules/glfw-3.4.bin.WIN64/` |
| OpenCV    | `exodus/exodus/modules/opencv/`             |

* **SimpleIni:**
  Download [`SimpleIni.h`](https://github.com/brofield/simpleini/blob/master/SimpleIni.h)
  Place in `modules/`.

* **serial:**
  Download the [`serial`](https://github.com/wjwwood/serial) library (whole folder).
  To build, open

  ```
  exodus/exodus/modules/serial/visual_studio/visual_studio.sln
  ```

  * Set **C/C++ > Code Generation > Runtime Library** to **Multi-threaded (/MT)**
  * Build in **Release x64**
  * Use the built DLL/LIB with your project.

* **GLFW:**
  Download [GLFW Windows binaries](https://www.glfw.org/download.html)
  Place the folder as shown above.

* **OpenCV:**
  Use your custom build or official DLLs.
  Place DLLs either next to your exe or in `modules/opencv/`.

**Example structure after setup:**

```
exodus/
└── exodus/
    └── modules/
        ├── SimpleIni.h
        ├── serial/
        ├── glfw-3.4.bin.WIN64/
        ├── opencv/
        └── (other optional libs)
```

---

## 4. Build and Run

1. Open the solution in Visual Studio 2022.
2. Choose the `Release | x64 | DML` configuration.
3. Build the solution.
4. Run `ai.exe` from the output folder.

---

## 🔄 Exporting AI Models

* Convert PyTorch `.pt` models to ONNX:

  ```bash
  pip install ultralytics -U
  yolo export model=sunxds_0.5.6.pt format=onnx dynamic=true simplify=true
  ```

---

## 🗂️ Old Releases

* [Legacy and old versions](https://disk.yandex.ru/d/m0jbkiLEFvnZKg)

---

## 📋 Configuration

* See all configuration options and documentation here:
  [config\_cpp.md](https://github.com/ExodusTeam/exodus-docs/blob/main/config/config_cpp.md)

---

## 📚 References & Useful Links

* [OpenCV Documentation](https://docs.opencv.org/4.x/d1/dfb/intro.html)
* [ImGui](https://github.com/ocornut/imgui)
* [CppWinRT](https://github.com/microsoft/cppwinrt)
* [GLFW](https://www.glfw.org/)
* [WindMouse](https://ben.land/post/2021/04/25/windmouse-human-mouse-movement/)
* [KMBOX](https://www.kmbox.top/)
* [Python AI Version](https://github.com/ExodusTeam/exodus_aimbot)

---

## 📄 Licenses

### OpenCV

* **License:** [Apache License 2.0](https://opencv.org/license.html)

### ImGui

* **License:** [MIT License](https://github.com/ocornut/imgui/blob/master/LICENSE)

---
## ❤️ Support the Project & Get Better AI Models

This project is actively developed thanks to the people who support it on [Boosty](https://boosty.to/exodus) and [Patreon](https://www.patreon.com/c/exodus).  
**By supporting the project, you get access to improved and better-trained AI models!**

---

**Need help or want to contribute? Join our [Discord server](https://discord.gg/37WVp6sNEh) or open an issue on GitHub!**