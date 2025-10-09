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
Precompiled `.exe` builds are provided for both CUDA (NVIDIA only) and DirectML (all GPUs).

---

### DirectML (DML) Build — Universal (All GPUs)

* **Works on:**

  * Any modern GPU (NVIDIA, AMD, Intel, including integrated graphics)
  * Windows 10/11 (x64)
  * No need for CUDA or special drivers!
* **Recommended for:**

  * GTX 10xx/9xx/7xx series (old NVIDIA)
  * Any AMD Radeon or Intel Iris/Xe GPU
  * Laptops and office PCs with integrated graphics
* **Download DML build:**
  [DirectML Release](https://disk.yandex.ru/d/jAVoMDfCuloNzw)

### CUDA + TensorRT Build — High Performance (NVIDIA Only)

* **Works on:**

  * NVIDIA GPUs **GTX 1660, RTX 2000/3000/4000 or newer**
  * **Requires:** CUDA 12.8, TensorRT 10.8 (included in build)
  * Windows 10/11 (x64)
* **Not supported:** GTX 10xx/Pascal and older (TensorRT 10 limitation)
* **Includes both CUDA+TensorRT and DML support (switchable in settings)**
* **Download CUDA build:**
  [DML + CUDA + TensorRT Release](https://disk.yandex.ru/d/ltSkG0DadLBqcA)

**Both versions are ready-to-use: just download, unpack, run `ai.exe` and follow instructions in the overlay.**

---

## How to Run (For Precompiled Builds)

1. **Download and unpack your chosen version (see links above).**
2. For CUDA build, install [CUDA 12.8](https://developer.nvidia.com/cuda-12-8-0-download-archive) if not already installed.
3. For DML build, no extra software is needed.
4. **Run `ai.exe`.**
   On first launch, the model will be exported (may take up to 5 minutes).
5. Place your `.onnx` model in the `models` folder and select it in the overlay (HOME key).
6. All settings are available in the overlay.
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
* **OpenCV 4.10.0** (restored automatically via NuGet; see below for CUDA notes)
* **\[For CUDA version]**

  * [CUDA Toolkit 12.8](https://developer.nvidia.com/cuda-12-8-0-download-archive)
  * [cuDNN 9.7.1](https://developer.nvidia.com/cudnn-downloads)
  * [TensorRT 10.8.0.43](https://developer.nvidia.com/tensorrt/download/10x)
* **\[For DML version]**

  * You can use [pre-built OpenCV DLLs](https://github.com/opencv/opencv/releases/tag/4.10.0) (just copy `opencv_world4100.dll` to your exe folder)
* Other dependencies:

  * [simpleIni](https://github.com/brofield/simpleini/blob/master/SimpleIni.h)
  * [serial](https://github.com/wjwwood/serial)
  * [GLFW](https://www.glfw.org/download.html)
  * [ImGui](https://github.com/ocornut/imgui)

---

## 2. Restore NuGet packages

Run a NuGet restore before opening the solution so the prebuilt dependencies are available:

```
nuget restore exodus.sln
```

This downloads the OpenCV 4.10.0 SDK and the TensorRT ONNX parser headers/libs into the `packages/` directory. Visual Studio also restores these automatically when you open the solution, but running the command manually ensures the dependencies are present for command-line builds.

If you maintain your own CUDA-enabled OpenCV or custom TensorRT builds, point the build at them by defining the environment variables below before compiling:

* `EXODUS_OPENCV_ROOT` → folder that contains `include/` and `lib/` for your OpenCV build.
* `EXODUS_TENSORRT_ROOT` → folder that contains `include/` and `lib/` for your TensorRT SDK.

Both variables should end with a trailing slash (e.g. `X:\deps\opencv\`). When set, the build system uses them instead of the NuGet packages.

---

## 3. Choose Build Target in Visual Studio

* **DML (DirectML):**
  Select `Release | x64 | DML` (works on any modern GPU)
* **CUDA (TensorRT):**
  Select `Release | x64 | CUDA` (requires supported NVIDIA GPU, see above)

---

## 4. Placement of Third-Party Modules and Libraries

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
| TensorRT  | `packages/TensorRT-Parser.10.8.0.43/` *(auto via NuGet)* |
| GLFW      | `exodus/exodus/modules/glfw-3.4.bin.WIN64/` |
| cuDNN     | `exodus/exodus/modules/cudnn/`              |

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

* **TensorRT:**
  The ONNX parser headers and import libraries restore automatically into `packages/TensorRT-Parser.10.8.0.43/` after running `nuget restore`. If you use NVIDIA's full TensorRT SDK (for CUDA builds), either extract it to a folder referenced by `EXODUS_TENSORRT_ROOT` or copy the contents into the `packages` directory so the include and lib folders sit alongside the NuGet package.

* **GLFW:**
  Download [GLFW Windows binaries](https://www.glfw.org/download.html)
  Place the folder as shown above.

* **OpenCV:**
  Headers/libs for the stock CPU build live under `packages/opencv.4.10.0.84/` after a NuGet restore. Copy `opencv_world4100.dll` next to `exodus.exe` for runtime use. If you have a CUDA-enabled build, set `EXODUS_OPENCV_ROOT` to its install folder so the compiler picks up your custom binaries instead of the NuGet package.

* **cuDNN:**
  Place cuDNN files here (for CUDA build):
  `exodus/exodus/modules/cudnn/`

**Example structure after setup:**

```
exodus/
├── packages/                     # populated by `nuget restore`
│   ├── opencv.4.10.0.84/
│   └── TensorRT-Parser.10.8.0.43/
└── exodus/
    └── modules/
        ├── SimpleIni.h
        ├── serial/
        ├── glfw-3.4.bin.WIN64/
        └── cudnn/
```

---

## 5. How to Build OpenCV 4.10.0 with CUDA Support (For CUDA Version Only)

> This section is **only required** if you want to use the CUDA (TensorRT) version and need OpenCV with CUDA support.
> For DML build, skip this step — you can use the pre-built OpenCV DLL.

**Step-by-step instructions:**

1. **Download Sources**

   * [OpenCV 4.10.0](https://github.com/opencv/opencv/releases/tag/4.10.0)
   * [OpenCV Contrib 4.10.0](https://github.com/opencv/opencv_contrib/releases/tag/4.10.0)
   * [CMake](https://cmake.org/download/)
   * [CUDA Toolkit 12.8](https://developer.nvidia.com/cuda-12-8-0-download-archive)
   * [cuDNN 9.7.1](https://developer.nvidia.com/cudnn-downloads)

2. **Prepare Directories**

   * Create a working directory for the build, for example:
     `exodus/deps/opencv/`
     `exodus/deps/opencv/build`
   * Extract `opencv-4.10.0` into
     `exodus/deps/opencv/opencv-4.10.0`
   * Extract `opencv_contrib-4.10.0` into
     `exodus/deps/opencv/opencv_contrib-4.10.0`
   * Extract cuDNN to
     `exodus/exodus/modules/cudnn`

3. **Configure with CMake**

   * Open CMake GUI
   * Source code:
     `exodus/deps/opencv/opencv-4.10.0`
   * Build directory:
     `exodus/deps/opencv/build`
   * Click **Configure**
     (Choose "Visual Studio 17 2022", x64)

4. **Enable CUDA Options**

   * After first configure, set the following:

     * `WITH_CUDA` = ON
     * `WITH_CUBLAS` = ON
     * `ENABLE_FAST_MATH` = ON
     * `CUDA_FAST_MATH` = ON
     * `WITH_CUDNN` = ON
     * `CUDNN_LIBRARY` =
       `full_path_to/exodus/exodus/modules/cudnn/lib/x64/cudnn.lib`
     * `CUDNN_INCLUDE_DIR` =
       `full_path_to/exodus/exodus/modules/cudnn/include`
     * `CUDA_ARCH_BIN` =
       See [CUDA Wikipedia](https://en.wikipedia.org/wiki/CUDA) for your GPU.
       Example for RTX 3080-Ti: `8.6`
     * `OPENCV_DNN_CUDA` = ON
     * `OPENCV_EXTRA_MODULES_PATH` =
       `full_path_to/exodus/deps/opencv/opencv_contrib-4.10.0/modules`
     * `BUILD_opencv_world` = ON
   * Uncheck:

     * `WITH_NVCUVENC`
     * `WITH_NVCUVID`
   * Click **Configure** again
     (make sure nothing is reset)
   * Click **Generate**

5. **Build in Visual Studio**

   * Open `exodus/deps/opencv/build/OpenCV.sln`
     or click "Open Project" in CMake
   * Set build config: **x64 | Release**
   * Build `ALL_BUILD` target (can take up to 2 hours)
   * Then build `INSTALL` target

6. **Register the build with Exodus**

   * Install output:
     `exodus/deps/opencv/build/install/x64/vc16/bin/`
     `exodus/deps/opencv/build/install/x64/vc16/lib/`
     `exodus/deps/opencv/build/install/include/opencv2`
   * Copy the required runtime DLLs (for example `opencv_world4100.dll`) next to `exodus.exe`.
   * Set the environment variable before compiling Exodus:

     ```powershell
     setx EXODUS_OPENCV_ROOT "X:\full\path\to\exodus\deps\opencv\build\install\"
     ```

     Remember the trailing slash. After restarting your developer command prompt, the Visual Studio project will automatically prefer your CUDA-enabled build over the NuGet package.

---

## 6. Notes on OpenCV for CUDA/DML

* **For CUDA build (TensorRT backend):**

  * You **must** build OpenCV with CUDA support (see the guide above).
  * Set `EXODUS_OPENCV_ROOT` to the `install/` directory from your build so the project can find the CUDA-enabled headers/libs.
  * Place the runtime DLLs (e.g., `opencv_world4100.dll`) next to your executable.
* **For DML build (DirectML backend):**

  * You can rely on the NuGet-provided OpenCV package (`packages/opencv.4.10.0.84/`).
  * If you want to ship a single executable that supports both CUDA and DML, prefer your CUDA-enabled build and reuse it for DML by pointing `EXODUS_OPENCV_ROOT` at the same install directory.
* **Note:**
  If you run the CUDA backend with non-CUDA OpenCV DLLs, the program will not work and may crash due to missing symbols.

---

## 7. Build and Run

1. Open the solution in Visual Studio 2022.
2. Choose your configuration (`Release | x64 | DML` or `Release | x64 | CUDA`).
3. Build the solution.
4. Run `ai.exe` from the output folder.

---

## 🔄 Exporting AI Models

* Convert PyTorch `.pt` models to ONNX:

  ```bash
  pip install ultralytics -U
  yolo export model=sunxds_0.5.6.pt format=onnx dynamic=true simplify=true
  ```
* To convert `.onnx` to `.engine` for TensorRT, use the overlay export tab (open overlay with HOME).

---

## 🗂️ Old Releases

* [Legacy and old versions](https://disk.yandex.ru/d/m0jbkiLEFvnZKg)

---

## 📋 Configuration

* See all configuration options and documentation here:
  [config\_cpp.md](https://github.com/ExodusTeam/exodus-docs/blob/main/config/config_cpp.md)

---

## 📚 References & Useful Links

* [TensorRT Documentation](https://docs.nvidia.com/deeplearning/tensorrt/)
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