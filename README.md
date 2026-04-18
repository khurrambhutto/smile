<p align="center">
  <img src="public/logo.svg" alt="Smile" width="160" height="160" />
</p>

<h1 align="center">Smile</h1>

<p align="center">
  A lightweight, native-feeling camera app for Linux.
</p>

---

Smile is a simple, fast camera app for Ubuntu and other Linux
desktops. It opens instantly, feels at home next to native GNOME
apps, and stays out of your way so you can just point and shoot.

## Features

- Smooth, high-resolution live preview
- Native GNOME-style dark UI
- Lightweight and quick to launch
- Works out of the box with most USB and built-in webcams

## Requirements

- Linux with a working camera (tested on Ubuntu)

## Install

Grab the latest build for your distro from the
[Releases](https://github.com/khurrambhutto/smile/releases) page
(`.deb`, AppImage, etc.) and install it the usual way.

## Build from source

If you want to build it yourself:

```bash
npm install
npm run tauri build
```

Bundles land in `src-tauri/target/release/bundle/`.

## Status

Work in progress. Live preview and photo capture are working; video capture
is on the way.

## Credits

Developed by [Khurram Bhutto](https://github.com/khurrambhutto).
