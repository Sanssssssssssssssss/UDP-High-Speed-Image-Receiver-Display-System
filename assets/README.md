# Demo Reference Asset

Place the exact demo reference image here if you want the built-in UDP demo to reproduce it pixel-by-pixel.

Supported filenames:
- `demo_reference.png`
- `demo_reference.jpg`
- `demo_reference.jpeg`
- `demo_reference.bmp`

The app will:
1. load the reference image,
2. resize it to `400x400`,
3. pulse brightness over time,
4. convert each pixel to RGB565,
5. packetize it through the unchanged UDP demo path.

If no file is present, the app falls back to the built-in procedural demo scene.
