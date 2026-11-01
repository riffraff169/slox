# Classes

## Class: Color
Color class for terminal ansi colors

### Methods

#### `Color.format(template)`
Replaces tags like "{red}" or "{bold}" with ANSI codes

**Required Arguments:**
  - `String: template`

**Returns:**
  - `String`

#### `Color.hexCharVal(c)`
Same as str.parse(16), but for a byte value instead of string.

**Required Arguments:**
  - `Integer: c`

**Returns:**
  - `Integer`

#### `Color.palette(index, [isBg])`
Return palette color string for palette index

**Required Arguments:**
  - `Integer: index`

**Optional Arguments:**
  - `Bool: isBg`

#### `Color.makeColor(val, [isBg])`
Make a custom rgb color. Val can be:
* `Integer`:  24-bit rgb value
* `Array`: 3 8-bit Integers
* `Map`: `red`, `green`, `blue` keys
* `String`: `#ff00bb` style
each of r, g, and b.

**Required Arguments:**
  - `Value: val`

**Optional Arguments:**
  - `Bool: isBg`

**Returns:**
  - `String`

#### `Color.rgb(r, g, b, [isBg])`
Get rgb string for color. Shorter version of makeColor

**Required Arguments:**
  - `Integer: r`
  - `Integer: g`
  - `Integer: b`

**Optional Arguments:**
  - `Bool: isBg`

**Returns:**
  - `String`

---
