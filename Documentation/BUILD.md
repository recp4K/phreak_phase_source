# BUILD INSTRUCTIONS - Freak Phase VST3

> **Complete build guide for Freak Phase VST3 plugin**
> Target: **Windows (FL Studio)**, macOS (future support)
> Framework: **JUCE 8.x**

---

## 📋 Table of Contents

1. [Prerequisites](#-prerequisites)
2. [Quick Start - JUCE Projucer (Recommended)](#-quick-start---juce-projucer-recommended)
3. [Alternative: CMake Build](#-alternative-cmake-build)
4. [Manual Build - Visual Studio](#-manual-build---visual-studio)
5. [Build Configuration](#-build-configuration)
6. [Troubleshooting](#-troubleshooting)
7. [Output Files](#-output-files)
8. [Installation](#-installation)
9. [Testing](#-testing)

---

## 🔧 Prerequisites

### Required Software

| Component | Version | Download | Purpose |
|-----------|---------|----------|---------|
| **JUCE Framework** | 8.x | [juce.com/get-juce](https://juce.com/get-juce) | Core framework for VST3 |
| **Visual Studio** | 2022/2026 | [visualstudio.microsoft.com](https://visualstudio.microsoft.com) | C++ compiler (Windows) |
| **CMake** | 3.15+ | [cmake.org/download](https://cmake.org/download) | Build system (optional) |
| **Git** | Latest | [git-scm.com](https://git-scm.com) | Version control |

### System Requirements

- **Windows 10/11** (Primary target for FL Studio)
- **macOS 11+** (Future support)
- **8 GB RAM** minimum (16 GB recommended for development)
- **SSD** recommended for faster builds
- **VST3 Host** (FL Studio 20+, Reaper, Cubase, etc.) for testing

---

## 🚀 Quick Start - JUCE Projucer (Recommended)

This is the **easiest and most reliable** method for building Freak Phase.

### Step 1: Install JUCE

1. Download JUCE from [https://juce.com/get-juce](https://juce.com/get-juce)
2. Run the installer
3. **Important:** During installation, select:
   - ✅ **JUCE Framework** (required)
   - ✅ **Projucer** (GUI tool)
   - ✅ **JUCE Modules** (all)
   - ✅ **Visual Studio 2022/2026 Integration**

### Step 2: Open Project in Projucer

1. Navigate to project directory:
   ```bash
   cd /path/to/FreakPhase
   ```

2. Double-click `phreakphase_test.jucer` to open in JUCE Projucer

3. **Configure Export Formats:**
   - Click **"Export"** tab
   - Select **Visual Studio 2026** (or 2022)
   - Under **Formats**, enable:
     - ✅ **VST3** (primary target)
     - ❌ VST2 (disabled)
     - ❌ AU (macOS only, disabled)
     - ❌ Standalone (optional)

### Step 3: Save and Open in Visual Studio

1. Click **"Save Project & Open in IDE"**
2. Visual Studio will open with the project loaded

### Step 4: Build in Visual Studio

1. Select **Release** configuration (not Debug)
2. Select **x64** platform (not Win32)
3. Press **Ctrl+Shift+B** or click **Build > Build Solution**
4. Wait for build to complete (should take 2-5 minutes)

### Step 5: Locate Output Files

After successful build, the plugin will be created in:
```
Builds/VisualStudio2026/x64/Release/VST3/FreakPhase.vst3/
```

---

## 🏗️ Alternative: CMake Build

For automated builds, CI/CD, or Linux development.

### Step 1: Install Dependencies

#### Windows (PowerShell)
```powershell
# Install CMake
winget install -e --id Kitware.CMake

# Install Ninja (optional, faster builds)
winget install -e --id Ninja-build.Ninja
```

#### Linux (Ubuntu/Debian)
```bash
# Install build tools
sudo apt update
sudo apt install -y build-essential cmake ninja-build git

# Install JUCE (if not using system-wide)
# Download from juce.com and extract to /opt/JUCE
```

### Step 2: Configure CMake

```bash
# Create build directory
mkdir -p build
cd build

# Configure with CMake (adjust JUCE_ROOT if needed)
cmake .. -G "Visual Studio 17 2022" \
    -DCMAKE_BUILD_TYPE=Release \
    -DJUCE_ROOT=/path/to/JUCE \
    -DJUCE_PLUGINHOST_VST3=ON \
    -DBUILD_STANDALONE=OFF
```

**CMake Options:**

| Option | Description | Default |
|--------|-------------|---------|
| `JUCE_ROOT` | Path to JUCE installation | Auto-detected |
| `JUCE_PLUGINHOST_VST3` | Build VST3 format | ON |
| `BUILD_STANDALONE` | Build standalone app | OFF |
| `CMAKE_BUILD_TYPE` | Release or Debug | Release |

### Step 3: Build with CMake

```bash
# Windows (Visual Studio generator)
cmake --build . --config Release --target FreakPhase_VST3

# Linux/macOS (Makefiles)
cmake --build . --config Release -j$(nproc)

# With Ninja (faster)
cmake --build . --config Release -j$(nproc) -G Ninja
```

### Step 4: Install Output

```bash
# Install to default location
cmake --install . --prefix /path/to/install

# Or manually copy to VST3 plugins folder
cp -r Builds/VST3/FreakPhase.vst3 "C:/Program Files/Common Files/VST3/"
```

---

## 🪟 Manual Build - Visual Studio

For developers who prefer manual Visual Studio setup.

### Step 1: Create Visual Studio Project

1. Open Visual Studio 2026
2. Click **Create a new project**
3. Select **Empty Project** (C++)
4. Name it `FreakPhase` and save to project directory

### Step 2: Add Source Files

Add all files from these directories:
- `*.cpp`, `*.h` from root
- `Source/` (all subdirectories)
- `Core/`
- `DSP/`
- `Analysis/`
- `Timeline/`
- `UI/`

### Step 3: Configure Project Properties

1. Right-click project → **Properties**
2. **Configuration Properties → General**
   - Platform Toolset: **Visual Studio 2026 (v150)**
   - C++ Language Standard: **ISO C++17 Standard (/std:c++17)**
   - Character Set: **Use Unicode Character Set**

3. **Configuration Properties → C/C++ → General**
   - Additional Include Directories: 
     ```
     $(JUCE_PATH)/modules
     $(ProjectDir)
     $(ProjectDir)/Source
     $(ProjectDir)/Source/Core
     $(ProjectDir)/Source/DSP
     $(ProjectDir)/Source/Analysis
     $(ProjectDir)/Source/Timeline
     ```

4. **Configuration Properties → C/C++ → Preprocessor**
   - Preprocessor Definitions:
     ```
     JUCE_VST3=1
     JucePlugin_Build_VST3=1
     JucePlugin_VST3Category="Delay|Fx"
     JucePlugin_Manufacturer="zibi.pr0d"
     JucePlugin_ManufacturerCode="Zbpl"
     JucePlugin_PluginCode="PhaS"
     JucePlugin_PluginName="FreakPhase"
     JucePlugin_Version="1.0.0"
     _WIN32
     _WINDLL
     ```

5. **Configuration Properties → Linker → General**
   - Output File: `$(OutDir)FreakPhase.vst3\FreakPhase.dll`

### Step 4: Add JUCE Libraries

1. Right-click project → **Add → Existing Project**
2. Navigate to JUCE modules and add required `.cpp` files
3. Or link against pre-built JUCE libraries

### Step 5: Build

Press **F7** or **Ctrl+Shift+B** to build.

---

## ⚙️ Build Configuration

### VST3 Plugin Configuration

The plugin is configured with these parameters:

```cpp
// Plugin Identity
Manufacturer:     "zibi.pr0d"
Manufacturer Code: "Zbpl"
Plugin Code:      "PhaS"
Plugin Name:      "FreakPhase"
Version:          "1.0.0"

// Plugin Category
VST3 Category:   "Delay,Fx"

// Audio Configuration
Input Buses:      2 (Track A stereo, Track B stereo)
Output Buses:     1 (Stereo output)
Max Channels:     8
Sample Rates:    44.1kHz - 384kHz
Block Sizes:     64 - 8192 samples

// Features
Accepts MIDI:    Yes
Produces MIDI:   No
Has Editor:      Yes (WebView2-based)
Is Synth:        No
Is MIDI Effect:  No
```

### WebView2 Requirements (Windows)

The plugin uses **WebView2** for the UI, which requires:

1. **WebView2 Runtime** installed on the host system
   - Download: [Microsoft Edge WebView2 Runtime](https://developer.microsoft.com/en-us/microsoft-edge/webview2/)
   - Or install via: `winget install Microsoft.Edge.WebView2Runtime`

2. **WebView2Loader.dll** must be accessible
   - Located in: `Source/UI/WebUI/` or system path
   - The code uses: `File::getSpecialLocation(currentApplicationFile)`

---

## 🐛 Troubleshooting

### Common Issues and Solutions

#### Issue 1: `JuceHeader.h: No such file or directory`

**Cause:** JUCE not installed or JUCE_ROOT not set

**Solution:**
```bash
# Set JUCE_ROOT environment variable
# Windows (PowerShell)
$env:JUCE_ROOT = "C:\JUCE"

# Windows (CMD)
set JUCE_ROOT=C:\JUCE

# Linux/macOS
export JUCE_ROOT=/opt/JUCE
```

Or pass to CMake:
```bash
cmake .. -DJUCE_ROOT=/path/to/JUCE
```

#### Issue 2: `WebView2Loader.dll not found`

**Solution:**
1. Install WebView2 Runtime (see above)
2. Copy `WebView2Loader.dll` to:
   - `Builds/VisualStudio2026/x64/Release/VST3/FreakPhase.vst3/Contents/x86_64-w64/`
   - Or to project root directory

#### Issue 3: Build fails with linking errors

**Solution:**
- Ensure all JUCE modules are included
- Check that you're using **Release** configuration (not Debug)
- Clean and rebuild: `cmake --build . --clean-first`

#### Issue 4: Plugin not showing in FL Studio

**Solution:**
1. **Rescan plugins:** In FL Studio, go to Options → Manage plugins → Find more plugins
2. **Check VST3 folder:** Ensure plugin is in:
   - Windows: `C:\Program Files\Common Files\VST3\`
   - macOS: `/Library/Audio/Plug-Ins/VST3/`
3. **Restart FL Studio:** Some hosts need restart to detect new plugins
4. **Check plugin format:** Ensure you built **VST3** (not VST2)

#### Issue 5: UI not loading (blank window)

**Solution:**
1. Check WebView2 Runtime is installed
2. Verify `UI/WebUI/` directory exists in plugin bundle
3. Check browser console for errors (if running in dev mode)
4. Ensure `index.html` and assets are copied to build directory

---

## 📁 Output Files

After successful build, you'll find these files:

### VST3 Plugin Structure (Windows)
```
Builds/VisualStudio2026/x64/Release/VST3/FreakPhase.vst3/
├── Contents/
│   ├── x86_64-w64/
│   │   ├── FreakPhase.dll          # Main plugin DLL
│   │   └── WebView2Loader.dll      # WebView2 runtime
│   └── Resources/
│       ├── UI/
│       │   └── WebUI/              # Web interface files
│       │       ├── index.html
│       │       ├── assets/
│       │       │   ├── index-*.js
│       │       │   ├── index-*.css
│       │       │   └── *.woff2
│       │       └── juceBridge.ts
│       └── plugin.png              # Plugin icon (optional)
└── Info.plist                      # macOS only
```

### File Sizes (Approximate)

| File | Size |
|------|------|
| `FreakPhase.dll` | ~2-5 MB |
| `WebView2Loader.dll` | ~50-100 MB |
| WebUI assets | ~300-500 KB |
| **Total** | **~55-105 MB** |

---

## 💾 Installation

### Windows (FL Studio)

1. **Copy plugin to VST3 folder:**
   ```bash
   # From build directory
   xcopy /E /I "Builds\VisualStudio2026\x64\Release\VST3\FreakPhase.vst3" "C:\Program Files\Common Files\VST3\"
   ```

2. **Alternative VST3 locations:**
   - FL Studio specific: `C:\Program Files\Image-Line\FL Studio\Plugins\Fruity\VST3\`
   - User-specific: `%APPDATA%\Image-Line\FL Studio\Plugins\VST3\`

3. **Rescan in FL Studio:**
   - Open FL Studio
   - Go to: Options → Manage plugins
   - Click: **Find more plugins**
   - Select the VST3 folder
   - Click: **Start scan**

### macOS

1. Copy to VST3 folder:
   ```bash
   cp -r Builds/VST3/FreakPhase.vst3 /Library/Audio/Plug-Ins/VST3/
   ```

2. Rescan in your DAW

---

## 🧪 Testing

### Manual Testing

1. **Load plugin in FL Studio:**
   - Open FL Studio
   - Add new channel → More plugins...
   - Find "FreakPhase" in Effects list
   - Load it on a mixer track

2. **Basic functionality test:**
   - ✅ Plugin loads without crashes
   - ✅ UI displays correctly
   - ✅ Audio passes through (bypass mode)
   - ✅ Parameters respond to changes
   - ✅ MIDI Learn works
   - ✅ Timeline automation works
   - ✅ AI Smart Align produces results

3. **Audio test:**
   - Load a kick and bass sample
   - Enable sidechain input
   - Adjust phase rotation
   - Verify phase alignment
   - Check correlation meter

### Automated Testing

Run the included test suite:

```bash
# Navigate to test directory
cd DSP/Tests

# Compile tests (Linux/macOS)
g++ -std=c++17 -I../../ -I../../../ -o TestImplementedFixes TestImplementedFixes.cpp

# Run tests
./TestImplementedFixes
```

Expected output:
```
=================================================================
  FINAL TEST SUMMARY: 25 / 25 TESTS PASSED (100% SUCCESS)
=================================================================
```

---

## 📞 Support

### Getting Help

1. **Check this documentation** first
2. **Review CHANGELOG.md** for known issues
3. **Check GitHub Issues** for common problems
4. **Verify JUCE version** compatibility

### Reporting Issues

When reporting build issues, please include:
- JUCE version
- Operating system
- Compiler version
- Build method (Projucer/CMake/Manual)
- Full error message
- Steps to reproduce

---

## 🔗 Useful Links

- [JUCE Framework](https://juce.com)
- [JUCE Documentation](https://docs.juce.com/master/)
- [VST3 SDK](https://www.steinberg.net/en/company/developers.html)
- [FL Studio](https://www.image-line.com)
- [WebView2 Runtime](https://developer.microsoft.com/en-us/microsoft-edge/webview2/)

---

## 📝 Version History

| Version | Date | Changes |
|---------|------|---------|
| 1.0.0 | Current | Initial release |

---

**Last Updated:** September 2025  
**Author:** Freak Phase Development Team  
**License:** Proprietary (see project license)
