# Smile

Smile is a fast, lightweight native Linux camera application built with Qt 6, QML, and a native V4L2 camera backend.

## Current Phase

Smile is currently in **Phase 2: Basic Image Capture**.

Implemented so far:

- V4L2 camera discovery from `/dev/video*`
- MJPEG/YUYV format enumeration
- Live preview through QML `VideoOutput`
- Camera and format selectors
- JPEG still capture saved under `~/Pictures/Smile/`

## Requirements

- Linux with V4L2 camera support
- CMake 3.21+
- C++20 compiler
- Qt 6 development packages:
  - Qt Core
  - Qt Gui
  - Qt QML
  - Qt Quick
  - Qt Quick Controls 2
  - Qt Multimedia

On Ubuntu/Debian, install the Qt 6 QML runtime plugins if preview UI fails to load:

```bash
sudo apt update
sudo apt install qml6-module-qtquick qml6-module-qtquick-window qml6-module-qtquick-controls qml6-module-qtquick-templates qml6-module-qtquick-layouts qml6-module-qtmultimedia qml6-module-qtqml-workerscript
```

## Build and Run

From the project root:

```bash
cd /home/khurram/Projects/smile/native
cmake -S . -B build
cmake --build build -j 4
./build/smile
```

When the app opens, it should:

1. Detect compatible `/dev/video*` cameras.
2. Select the preferred MJPEG/YUYV format.
3. Start the live preview automatically.
4. Show camera and format selectors in the header.
5. Let you click **Take Photo** to save a JPEG.

Captured photos are saved to:

```text
~/Pictures/Smile/
```

## Clean Rebuild

If the build directory gets stale, run:

```bash
cd /home/khurram/Projects/smile/native
rm -rf build
cmake -S . -B build
cmake --build build -j 4
./build/smile
```

## Troubleshooting

### `QtQuick.Controls` plugin not found

If running the app prints:

```text
module "QtQuick.Controls" plugin "qtquickcontrols2plugin" not found
```

install the Qt 6 Quick Controls and related QML runtime packages:

```bash
sudo apt update
sudo apt install qml6-module-qtquick qml6-module-qtquick-window qml6-module-qtquick-controls qml6-module-qtquick-templates qml6-module-qtquick-layouts qml6-module-qtmultimedia qml6-module-qtqml-workerscript
```

Then run again:

```bash
./build/smile
```

### Camera preview does not start

Check available camera devices:

```bash
ls -l /dev/video*
```

Check your user groups:

```bash
groups
```

On many distributions, your user must be in the `video` group:

```bash
sudo usermod -aG video $USER
```

After adding the group, log out and log back in.

You can also launch the app from a terminal to see camera/backend errors:

```bash
cd /home/khurram/Projects/smile/native
./build/smile
```

## Development Notes

The documented target is Qt 6.5+, but the current code also avoids APIs unavailable on Qt 6.4 so it can build on systems with Qt 6.4.2 installed.
