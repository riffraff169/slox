# Classes

## Class: KittyCanvas
Manages drawing operations on the terminal canvas
Supports RGB color rending

**Required Arguments:**
  - `Integer: width`
  - `Integer: height`

### Methods

#### `KittyCanvas.setPixel(x, y, r, [g], [b])`
Set a pixel at (x, y) with RGB values (0-255). r can be rgb array, or
values can be specified separately.

**Required Arguments:**
  - `Integer: x`
  - `Integer: y`
  - `Integer|Array: r`

**Optional Arguments:**
  - `Integer: g`
  - `Integer: b`

#### `KittyCanvas.drawLine(x0, y0, x1, y1, r, [g], [b])`
draw a line

**Required Arguments:**
  - `Integer: x0`
  - `Integer: y0`
  - `Integer: x1`
  - `Integer: y1`
  - `Integer|Array: r`

**Optional Arguments:**
  - `Integer: g`
  - `Integer: b`

#### `KittyCanvas.drawAxes(xMin, xMax, yMin, yMax)`
draw x and y axes

**Required Arguments:**
  - `Integer: xMin`
  - `Integer: xMax`
  - `Integer: yMin`
  - `Integer: yMax`

#### `KittyCanvas.fillRect(x, y, w, h, r, g, b)`
Fill a rectangle

**Required Arguments:**
  - `Integer: x`
  - `Integer: y`
  - `Integer: w`
  - `Integer: h`
  - `Integer: r`
  - `Integer: g`
  - `Integer: b`

#### `KittyCanvas.drawCircle(cx, cy, radius, Integer;, g, b)`
Draw a simple circle

**Required Arguments:**
  - `Integer: cx`
  - `Integer: cy`
  - `Integer: radius`
  - `Integer; r`
  - `Integer: g`
  - `Integer: b`

#### `KittyCanvas.render()`
Render the canvas to stdout using Kitty Protocol

---
