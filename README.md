[![BAD_APPLE.EXE｜Bad Apple!! rendered with 150 real Windows at 60 FPS](https://img.youtube.com/vi/puNlEDc_Gow/0.jpg)](https://www.youtube.com/watch?v=puNlEDc_Gow)

---

# BAD_APPLE.EXE
Bad Apple!! rendered with 150 real Windows at 60 FPS

This program plays Bad Apple!! in real time at 60 FPS
by creating up to 150 real Windows simultaneously.

Each white block is an independent top-level window.

---

## Repository Structure

badapple-windows/
├─ assets/ ← bad_apple.wav, video_layout.zip  
├─ resources/ ← resource.rc, icon.ico  
├─ src/ ← bad_apple.cpp  
└─ README.md

- **assets/**  
  - `bad_apple.wav` : Background music (can be embedded in the EXE)  
  - `video_layout.zip` : Compressed video data (~75 MB). Extract to get `video_layout.bin` (for development purposes only).  

- **resources/**  
  - `resource.rc` : Resource file for building  
  - `icon.ico` : Icon for the executable  

- **src/**  
  - `bad_apple.cpp` : Main source code  

- **README.md** : This file  

---

## User Setup

1. Download `bad_apple.exe` from the GitHub Releases page.  
2. Run `bad_apple.exe`.  

> The executable contains all resources (video + audio), so no extra files are required for playback.

---

## For Developers (Optional)

- Build from `src/bad_apple.cpp` using a C++ compiler (Visual Studio 2019+ recommended)  
- `resources/resource.rc` references icon, WAV, and video BIN files  
- Large video data (`video_layout.bin`) is optional for embedding; the EXE runs standalone.

---

## Source Attribution

The video data used in this project is based on the following
fan-made "Bad Apple!!" arrangement (4K / 60 FPS):

https://www.youtube.com/watch?v=vg0PbuV8-ao

This project does not claim ownership of the original music or
the derivative video content.
All rights belong to their respective creators.
