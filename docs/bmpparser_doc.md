# Classes

## Class: BMPImage
BMP image class.

**Required Arguments:**
  - `Integer: width`
  - `Integer: height`
  - `Array: pixels`

### Methods

#### `BMPImage.loadFromBytes(bytes)`
Create rgba data from bmp byte string.

**Required Arguments:**
  - `String: bytes`

#### `BMPImage.renderToCanvas(canvas, targetWidth, targetHeight)`
Render to a KittyCanvas

**Required Arguments:**
  - `KittyCanvas: canvas`
  - `Integer: targetWidth`
  - `Integer: targetHeight`

#### `BMPImage.drawToCanvasAt(canvas, offsetx, offsetY)`
Draw the current pixel data to a KittyCanvas at an offset

**Required Arguments:**
  - `KittyCanvas: canvas`
  - `Integer: offsetx`
  - `Integer: offsetY`

---
