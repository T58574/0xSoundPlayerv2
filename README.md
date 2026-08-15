# 🎧 0xSoundPlayerv2 (Legacy V1 Prototype)

<div align="center">

[![Go](https://img.shields.io/badge/Go-1.21%2B-00ADD8?style=flat-square&logo=go&logoColor=white)](https://golang.org/)
[![C++11](https://img.shields.io/badge/C%2B%2B-11%20%2F%20miniaudio-00599C?style=flat-square&logo=c%2B%2B&logoColor=white)](https://isocpp.org/)
[![Status: Superseded](https://img.shields.io/badge/Status-Superseded%20by%200xPlay-orange?style=flat-square)](https://github.com/T58574/0xPlay)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg?style=flat-square)](LICENSE)

**Initial prototype and proof-of-concept audio player engine with miniaudio C++ decoding and Go Wails v2 bindings.**

</div>

---

## ⚠️ Important Note: Superseded by 0xPlay

> [!IMPORTANT]
> **This repository is the initial V1 architecture prototype of 0xSoundPlayer.**
> Active development, full DSP harmonic mixing, Camelot key detection, dynamic tempo ramping, and WebGL fluid visualizers are actively maintained in the flagship repository:
> 👉 **[github.com/T58574/0xPlay](https://github.com/T58574/0xPlay)** 👈

---

## 📖 Overview

**0xSoundPlayerv2** was built as the foundational proof-of-concept for testing low-latency C/C++ audio I/O using `miniaudio.h` and `Signalsmith Stretch` inside a Go Wails v2 desktop environment. It proved sub-millisecond audio decoding, non-blocking background metadata indexing, and basic waveform visualization on HTML5 Canvas.

---

## 🛠 Tech Stack

- **Audio Engine**: C++11, `miniaudio.h`, `signalsmith-stretch.h`
- **Backend Host**: Go 1.21+, Cgo, Wails v2
- **Frontend**: React 18, TypeScript, Tailwind CSS

---

## 📜 License

Distributed under the **MIT License**. See [LICENSE](LICENSE) for details.
