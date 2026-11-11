# Classes

## Class: ArgOption
Class to just hold arguments for ArgParser

---

## Class: ParsedArgs
Returned from ArgParser as the parsed arguments.

### Methods

#### `ParsedArgs.set(key, val)`
Set key to val

**Required Arguments:**
  - `String: key`
  - `Value: val`

**Returns:**
  - `this`

#### `ParsedArgs.get(key)`
Get value of key

**Required Arguments:**
  - `String: key`

**Returns:**
  - `Value`

---

## Class: ArgParser
Argument parser class.
See examples/test_argparser.lox

### Methods

#### `ArgParser.addFlag(name, shortname, help)`
Add a flag option.

**Example:**
```
parser.addOption("verbose", "v", "Enable debug logging");
```

**Required Arguments:**
  - `String: name`
  - `String: shortname`
  - `String: help`

#### `ArgParser.addOption(name, shortName, deffaultValue, help, [optional])`
Add a variable option

**Example:**
```
parser.addOption("count", "c", "10", "Number of iterations");
```

**Required Arguments:**
  - `String: name`
  - `String: shortName`
  - `String: deffaultValue`
  - `String: help`

**Optional Arguments:**
  - `Bool: optional`

#### `ArgParser.parse(args)`
Parse the provided args to create the options. Generally will be System.ARGV

**Required Arguments:**
  - `Array: args`

**Returns:**
  - `ParsedArgs`

---

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

## Class: Cairo
Cairo canvas ffi bindings

---

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

## Class: Complex
Complex class and associated methods.
Supports overloads of relational operators, basic math.
Complex vars are immutable. Every operator returns a new instance, and trying
to modify individual parts fails.

**Required Arguments:**
  - `Number: real`
  - `Number: imag`

### Methods

#### `Complex.fromReal(r)`
Create a Complex value from a real

**Required Arguments:**
  - `Number: r`

**Returns:**
  - `Complex`

#### `Complex.fromPolar(r, theta)`
Create a Complex value from a polar r, theta

**Required Arguments:**
  - `Number: r`
  - `Number: theta`

**Returns:**
  - `Complex`

#### `Complex.abs()`
Get magnitude/abs

**Returns:**
  - `Number`

#### `Complex.arg()`
Get atan2 of imag and real

**Returns:**
  - `Number`

#### `Complex.conj()`
Get complex conjugate

**Returns:**
  - `Complex`

#### `Complex.neg()`
Get negative -a - bi

**Returns:**
  - `Complex`

#### `Complex.add(other)`
Add a value to this, either another Complex, or a Number

**Required Arguments:**
  - `Number: other`

**Returns:**
  - `Complex`

#### `Complex.sub(other)`
Subtract a value

**Required Arguments:**
  - `Number: other`

**Returns:**
  - `Complex`

#### `Complex.mul(other)`
Multiply

**Required Arguments:**
  - `Number: other`

**Returns:**
  - `Complex`

#### `Complex.div(other)`
Divide

**Required Arguments:**
  - `Number: other`

**Returns:**
  - `Complex`

#### `Complex.exp()`
Get e^this

**Returns:**
  - `Complex`

#### `Complex.to_string()`
Format as a string

**Returns:**
  - `String`

---

## Class: CSV
CSV parser. Creates an array of fields.

### Methods

#### `CSV.parse(text)`
Parse text into csv array

**Required Arguments:**
  - `String: text`

**Returns:**
  - `Array`

#### `CSV.read(filepath)`
Read a file and return csv array.

**Required Arguments:**
  - `String: filepath`

**Returns:**
  - `Array`

#### `CSV.to_maps(rows)`
Creates an array of maps, mapping header names to values.
Assumes row 0 is header row.

**Required Arguments:**
  - `Array: rows`

**Returns:**
  - `Array`

---

## Class: MethodDoc
Class for method documentation

---

## Class: ClassDoc
Class for class documentation

---

## Class: FunctionDoc
Class for function documentation

---

## Class: DocParser
Implements a doc parser for code files. Parses comment lines before functions,
classes, and methods.  Supports parameters with Requires and Optional.

### Methods

#### `DocParser.addAliases(rawStr)`
Add aliases for this method/function.

**Required Arguments:**
  - `String: rawStr`

#### `DocParser.extractArgName(raw)`
Get the name of the argument

**Required Arguments:**
  - `String: raw`

**Returns:**
  - `String`

#### `DocParser.buildSignature(className, target)`
Creates the function signature

**Required Arguments:**
  - `String: className`
  - `Class: target`

**Returns:**
  - `String`

#### `DocParser.findClass(name)`
Find a class from current parsed list

**Required Arguments:**
  - `String: name`

**Returns:**
  - `Class`

#### `DocParser.parseLines(lines)`
Parse all the lines of a file

**Required Arguments:**
  - `Array: lines`

**Returns:**
  - `this`

#### `DocParser.processLine(line)`
Processes a line. Scans for document comments, adding to specific class, method,
or function.

**Required Arguments:**
  - `String: line`

#### `DocParser.closeTarget()`
Closes current item, preparing for next one.

#### `DocParser.generateMarkdown()`
Given the file as parsed earlier, creates markdown output for it.

#### `DocParser.appendDocBodyLines(target, lines)`
Part of generateMarkdown,

**Required Arguments:**
  - `Class: target`
  - `Array: lines`

---

## Class: ELFParser
Parse elf header

**Required Arguments:**
  - `String: bytes`

### Methods

#### `ELFParser.readU16LE(offset)`
Read 16 bit unsigned integer (little endian)

**Required Arguments:**
  - `Integer: offset`

#### `ELFParser.readU32LE(offset)`
Read 32 bit unsigned integer le

**Required Arguments:**
  - `Integer: offset`

#### `ELFParser.readU64LE(offset)`
Read 64 bit unsigned char

**Required Arguments:**
  - `Integer: offset`

#### `ELFParser.readHex64LE(offset)`
Read 64 bits, return as hex string.

**Required Arguments:**
  - `Integer: offset`

**Returns:**
  - `String`

#### `ELFParser.readString(offset)`
Convert raw byte offset to zero-terminated ascii string

**Required Arguments:**
  - `Integer: offset`

**Returns:**
  - `String`

#### `ELFParser.formatU64(val64)`
Helper to format [lo, hi] 64-bit values for display

**Required Arguments:**
  - `Array: val64`

**Returns:**
  - `String`

#### `ELFParser.toOffset32(val64)`
Returns the lo byte of a 64 bit value array

**Required Arguments:**
  - `Array: val64`

**Returns:**
  - `Integer`

#### `ELFParser.parse()`
Parse elf header

#### `ELFParser.parseProgramHeaders(phoff, phnum, phentsize, width)`
Print program headers

**Required Arguments:**
  - `Integer: phoff`
  - `Integer: phnum`
  - `Integer: phentsize`
  - `Integer: width`

#### `ELFParser.parseSectionHeaders(shoff, shnum, shentsize, shstrndx, width)`
Print section headers

**Required Arguments:**
  - `Integer: shoff`
  - `Integer: shnum`
  - `Integer: shentsize`
  - `Integer: shstrndx`
  - `Integer: width`

#### `ELFParser.getSegmentType(type)`
Return the segment type

**Required Arguments:**
  - `Integer: type`

**Returns:**
  - `String`

#### `ELFParser.getSegmentFlags(flags)`
Return the segment flags

**Required Arguments:**
  - `Integer: flags`

**Returns:**
  - `String`

#### `ELFParser.getSectionType(type)`
Returns the segment type as a string

**Required Arguments:**
  - `Integer: type`

**Returns:**
  - `String`

#### `ELFParser.getSectionFlags(flags64)`
Get the section flags

**Required Arguments:**
  - `Array: flags64`

**Returns:**
  - `String`

#### `ELFParser.getOSABI(abi)`
Get the OS ABI

**Required Arguments:**
  - `Integer: abi`

**Returns:**
  - `String`

#### `ELFParser.getType(typeVal)`
Get the type of file

**Required Arguments:**
  - `Integer: typeVal`

**Returns:**
  - `String`

#### `ELFParser.getMachine(mach)`
Get the machine type

**Required Arguments:**
  - `Integer: mach`

**Returns:**
  - `String`

---

## Class: Event
Event class. Similar to gtk signals.
Optionnal:
Value: value

### Methods

#### `Event.get()`
Get current value.

**Returns:**
  - `Value`

#### `Event.set(newValue)`
Set a new value. Notifies any listeners of the change.

**Required Arguments:**
  - `Value: newValue`

#### `Event.subscribe(callback)`
Subscribe to this event. Returns the callback.

**Required Arguments:**
  - `Value: callback`

**Returns:**
  - `Value: callback`

#### `Event.unsubscribe(callback)`
Remove a callback from the listeners.

**Required Arguments:**
  - `Value: callback`

#### `Event.notify(oldVal, newVal)`
Notify any listeners. Calls the callback (listener) with newVal, oldVal.

**Required Arguments:**
  - `Value: oldVal`
  - `Value: newVal`

#### `Event.on(cb)`
Set callback for this event.

**Required Arguments:**
  - `Value: cb`

#### `Event.off(callback)`
Revmove this callback.

**Required Arguments:**
  - `Value: callback`

#### `Event.emit(data)`
Call the callbacks with data.

**Required Arguments:**
  - `Value: data`

---

## Class: Json
Json class. Supports most json data.

### Methods

#### `Json.parse(jsonstr)`
Parse json into native data structures.

**Required Arguments:**
  - `String: jsonstr`

**Returns:**
  - `String`

#### `Json.to_string(obj)`
Return printable json string.

**Required Arguments:**
  - `Value: obj`

---

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

## Class: Token
Simple class to hold a token

---

## Class: SloxLexer
Class to parse slox code into tokens.

**Required Arguments:**
  - `String: source`

### Methods

#### `SloxLexer.scanTokens()`
Entrypoint. Scan source and separate into tokens.

**Returns:**
  - `Array: tokens`

---

## Class: Logger
Logger class. Supports color for terminal if enabled.

**Optional Arguments:**
  - `options`

### Methods

#### `Logger.set_level([level])`
Set min level

**Optional Arguments:**
  - `Integer: level`

#### `Logger.set_app_name([name])`
App name for log output

**Optional Arguments:**
  - `String: name`

#### `Logger.set_file(path, [truncate])`
Set file path for file output.

**Required Arguments:**
  - `String: path`

**Optional Arguments:**
  - `Bool: truncate`

#### `Logger.register_tag(tag, [level], [color])`
Create custome tag

**Required Arguments:**
  - `String: tag`

**Optional Arguments:**
  - `Integer: level`
  - `String: color`

#### `Logger.dispatch(tag, level, msg, [context])`
Log a message.

**Required Arguments:**
  - `String: tag`
  - `Integer: level`
  - `String: msg`

**Optional Arguments:**
  - `String|Number: context`

#### `Logger.get_color_for(tag, level)`
Get the color for a tag and level

**Required Arguments:**
  - `String: tag`
  - `Integer: level`

#### `Logger.trace(msg, [ctx])`
Send a trace message

**Required Arguments:**
  - `String: msg`

**Optional Arguments:**
  - `String: ctx`

#### `Logger.debug(msg, [ctx])`
Send a debug message

**Required Arguments:**
  - `String: msg`

**Optional Arguments:**
  - `String: ctx`

#### `Logger.info(msg, [ctx])`
Send an info message

**Required Arguments:**
  - `String: msg`

**Optional Arguments:**
  - `String: ctx`

#### `Logger.warn(msg, [ctx])`
Send a warn message

**Required Arguments:**
  - `String: msg`

**Optional Arguments:**
  - `String: ctx`

#### `Logger.error(msg, [ctx])`
Send an error message

**Required Arguments:**
  - `String: msg`

**Optional Arguments:**
  - `String: ctx`

#### `Logger.fatal(msg, [ctx])`
Send a fatal message

**Required Arguments:**
  - `String: msg`

**Optional Arguments:**
  - `String: ctx`

#### `Logger.custom(tag, msg, [ctx])`
Send a custom message

**Required Arguments:**
  - `String: tag`
  - `String: msg`

**Optional Arguments:**
  - `String: ctx`

#### `Logger.error_count()`
Get number of errors or fatals

**Returns:**
  - `Integer`

#### `Logger.warn_count()`
Get number of wanrs

**Returns:**
  - `Integer`

#### `Logger.summary()`
Log a summary

---

## Class: MarkdownPrinter
Convert markdown to strings with color

### Methods

#### `MarkdownPrinter.reset()`
Reset

#### `MarkdownPrinter.emit(template)`
Add template line to lines array

**Required Arguments:**
  - `String: template`

#### `MarkdownPrinter.formatInline(text)`
Wraps Markdown syntax (**bold**, *italic*, `code`) into Color format tags, if use_color is true

**Required Arguments:**
  - `String: text`

**Returns:**
  - `String`

#### `MarkdownPrinter.processLine(rawLine)`
Line Processing

**Required Arguments:**
  - `String: rawLine`

#### `MarkdownPrinter.render(markdownText)`
Main Entry Point

**Required Arguments:**
  - `String: markdownText`

**Returns:**
  - `String`

---

## Class: Matrix
Matrix class.  Internally represented as a flat list.

**Required Arguments:**
  - `Integer: rows`
  - `Integer: cols`
  - `Array: data`

### Methods

#### `Matrix.zeros(rows, cols)`
Create a new matrix of all zeros.

**Required Arguments:**
  - `Integer: rows`
  - `Integer: cols`

**Returns:**
  - `Matrix`

#### `Matrix.identity(n)`
Create an identity matrix of n x n.

**Required Arguments:**
  - `Integer: n`

**Returns:**
  - `Matrix`

#### `Matrix.get(r, c)`
Return value at r, c.

**Required Arguments:**
  - `Integer: r`
  - `Integer: c`

**Returns:**
  - `Number`

#### `Matrix.set(r, c, val)`
Set value at r, c

**Required Arguments:**
  - `Integer: r`
  - `Integer: c`
  - `Integer: val`

#### `Matrix.transpose()`
Return a transposed version of this matrix.

**Returns:**
  - `Matrix`

#### `Matrix.add(other)`
Return a new matrix that is this one plus another one

**Required Arguments:**
  - `Matrix: other`

**Returns:**
  - `Matrix`

#### `Matrix.mul(other)`
Multiply either a number, or another matrix.  Returns a new matrix.

**Required Arguments:**
  - `Matrix|Number: other`

**Returns:**
  - `Matrix`

#### `Matrix.trace()`
Get trace value.

**Returns:**
  - `Number`

#### `Matrix.det()`
Get determinant

**Returns:**
  - `Number`

#### `Matrix.eigen2x2()`
Get eigenvalue of a 2x2 matrix

**Returns:**
  - `Array|Map`

#### `Matrix.eigenvectorForValue(lambda)`
Get eigenvector for a value

**Required Arguments:**
  - `Number: lambda`

**Returns:**
  - `Matrix`

#### `Matrix.getCol(m, colIdx)`
Extract a column vector as a 1d array

**Required Arguments:**
  - `Matrix: m`
  - `Integer: colIdx`

**Returns:**
  - `Array`

#### `Matrix.dot(u, v)`
Dot product

**Required Arguments:**
  - `Matrix: u`
  - `Matrix: v`

**Returns:**
  - `Number`

#### `Matrix.norm(v)`
Get normal

**Required Arguments:**
  - `Matrix: v`

**Returns:**
  - `Number`

#### `Matrix.qrDecompose()`
qr decomposition using gram-schmidt process: A = Q * R

**Returns:**
  - `Map`

#### `Matrix.eigenvaluesQR(maxIter)`
compute all real eigenvalues for n x n matrix using qr iteration. Broken

**Required Arguments:**
  - `Integer: maxIter`

---

## Class: Dir
Collection of methods dealing with directories.

### Methods

#### `Dir.mkdir_p(path)`
Creates full path of directories. Unix only.

**Required Arguments:**
  - `String: path`

#### `Dir.mkdir(path, [mode])`
Standard mkdir method. Default mode is 0755 if not specified.

**Required Arguments:**
  - `String: path`

**Optional Arguments:**
  - `Integer: mode`

**Returns:**
  - `Bool`

#### `Dir.list([directory])`
Get list of files in directory. Default "." (current directory) if omitted.

**Optional Arguments:**
  - `String: directory`

**Returns:**
  - `Result: filelist`

#### `Dir.chdir(pathname, [callback])`
Changes current directory. If passed a callback, will change the directory,
execute the callback, then return to previous directory.

**Required Arguments:**
  - `String: pathname`

**Optional Arguments:**
  - `Value: callback`

**Returns:**
  - `Bool`

#### `Dir.getcwd()`
Get the current directory.

**Aliases:** `pwd`

**Returns:**
  - `String`

#### `Dir.rmdir(pathname)`
Attempts to remove a directory. Will fail if directory is not empty. Sets global variable
errno to error code.

**Required Arguments:**
  - `String: pathname`

**Returns:**
  - `Nil`

#### `Dir.exists(pathname)`
Checks if path exists and is a directory

**Required Arguments:**
  - `String: pathname`

**Returns:**
  - `Bool`

#### `Dir.home()`
Gets home directory of current user. Returns $HOME if set, otherwise /.

**Returns:**
  - `String`

#### `Dir.tempdir()`
Gets the currently set temp directory, where files created by mktemp would
be created. Returns the first environment variable set in the following
order: TMPDIR, TMP, TEMP, or /tmp if none set.

**Returns:**
  - `String`

#### `Dir.mktemp(template)`
Creates a temp directory based on a template.

**Required Arguments:**
  - `String: template`

**Returns:**
  - `Result: String`

#### `Dir.each(pathname, callback)`
Run a callback on each file in a directory. Returns nothing as the callback
is run for its side effects.

**Required Arguments:**
  - `String: pathname`
  - `Value: callback`

**Returns:**
  - `Nil`

#### `Dir.glob(pattern)`
Get files in the current directory matching a shell glob.

**Required Arguments:**
  - `String: pattern`

#### `Dir.isempty(pathname)`
Check if a directory is empty

**Required Arguments:**
  - `String: pathname`

**Returns:**
  - `Bool`

#### `Dir.basename(pathname)`
Get the basename of a path, that is the far right portion of a path string, the last
component. Could be a file or a directory.

**Required Arguments:**
  - `String: pathname`

**Returns:**
  - `String`

#### `Dir.dirname(pathname)`
Get the dirname of a path. Nearly always a directory, will be the second to last
component of a path string.

**Required Arguments:**
  - `String: pathname`

**Returns:**
  - `String`

#### `Dir.isdir(pathname)`
Checks if a path is a directory.

**Required Arguments:**
  - `String: pathname`

**Returns:**
  - `Bool`

#### `Dir.realpath(pathname)`
Gets the canonicalized real path name, expanding all symbolic links and resolves references
like ./../..

**Required Arguments:**
  - `String: pathname`

**Returns:**
  - `String`

---

## Class: MultiFFI
Load multiple ffi libraries. Doesn't present skip libraries that aren't found.

**Required Arguments:**
  - `Array: paths`

### Methods

#### `MultiFFI.bind()`
Binds a symbol. Searches through all loaded libraries to find the first one that matches.

---

## Class: PixelBuffer
A simple pixel buffer for notcurses boxes. Stored as 32-bit rgba. Could be scaled to implement MxN pixels.

### Methods

#### `PixelBuffer.clear(rgba)`
Set all bytes to a value. Can be either a single byte value, where all bytes (r, g, b, a)
are set to the same value (as noticed, including the alpha channel). Or use a 32-bit value,
and the bytes are set in rgba order.

**Required Arguments:**
  - `Integer: rgba`

#### `PixelBuffer.setPixel(rgba)`
Set pixel x,y to value rgba. rgba can be an array of [r,g,b,a], or a 32-bit value.

**Required Arguments:**
  - `Array|Integer: rgba`

#### `PixelBuffer.getBuffer()`
Return the buffer as a string

**Returns:**
  - `String`

---

## Class: Visual
A Visual is a represents a notcurses visual.

**Required Arguments:**
  - `PixelBuffer: pixelBuffer`
  - `Notcurses: nc`

### Methods

#### `Visual.resize(rows, cols)`
Resize visual

**Required Arguments:**
  - `Integer: rows`
  - `Integer: cols`

#### `Visual.pixelGeom()`
Get the current geometry

**Returns:**
  - `Notcurses.pixelGeom`

#### `Visual.resizeCells(plane, cellRows, cellCols)`
Resize cell sizes

**Required Arguments:**
  - `Plane: plane`
  - `Integer: cellRows`
  - `Integer: cellCols`

**Returns:**
  - `Integer`

#### `Visual.blit(plane, [blitMode], [scale], [y], [x], [leny], [lenx])`
Copy data over to plane

**Required Arguments:**
  - `Plane: plane`

**Optional Arguments:**
  - `Integer: blitMode`
  - `Integer: scale`
  - `Integer: y`
  - `Integer: x`
  - `Integer: leny`
  - `Integer: lenx`

**Returns:**
  - `Plane`

#### `Visual.destroy()`
Destroy this visual

---

## Class: Plane
Plane class for managing planes.

**Required Arguments:**
  - `Notcurses: ptr`
  - `FFI|MultiFFI: ffiLib`

### Methods

#### `Plane.pixelGeom()`
Get geometry of plane.

**Returns:**
  - `Array`

#### `Plane.erase()`
Erase plane

#### `Plane.move_top()`
Move this plane to top of stack

#### `Plane.move_bottom()`
Move this plane to bottom of stack

#### `Plane.set_opaque(r, g, b)`
Set plane to opaque by setting base and erasing.

**Required Arguments:**
  - `Integer: r`
  - `Integer: g`
  - `Integer: b`

#### `Plane.move_yx(y, x)`
Move this plane to x, y coordinates of terminal.

**Required Arguments:**
  - `Integer: y`
  - `Integer: x`

#### `Plane.create_child(y, x, rows, cols, [flags])`
Create a child plane of this plane.

**Required Arguments:**
  - `Integer: y`
  - `Integer: x`
  - `Integer: rows`
  - `Integer: cols`

**Optional Arguments:**
  - `Integer: flags`

**Returns:**
  - `Plane`

#### `Plane.put_str()`
Write a string to a specific location, or at current position if y,x not specified.
```

**Example:**
```

#### `Plane.put_str_aligned(y, align, str)`
Write a string at the current y, aligned. Align is either "center",
"left", or "right".

**Required Arguments:**
  - `Integer: y`
  - `String: align`
  - `String: str`

**Returns:**
  - `Integer`

#### `Plane.set_fg(r, [g], [b])`
Set foreground color for future writes. r can be array of [r, g, b], or values
can be provided separately.

**Aliases:** `set_fgrgb`

**Required Arguments:**
  - `Array|Integer: r`

**Optional Arguments:**
  - `Integer: g`
  - `Integer: b`

#### `Plane.set_bg(r, [g], [b])`
Set background color for future writes. r can be array of [r, g, b], or values
can be provided separately.

**Aliases:** `set_bgrgb`

**Required Arguments:**
  - `Array|Integer: r`

**Optional Arguments:**
  - `Integer: g`
  - `Integer: b`

#### `Plane.destroy()`
Destroy this plane

**Returns:**
  - `Integer`

#### `Plane.createCell(egc, channels)`
Create a cell for box. Internal method only.

**Required Arguments:**
  - `String: egc`
  - `Integer: channels`

#### `Plane.box([ystop], [xstop], [rounded], [edges])`
Create a box around the plane.

**Optional Arguments:**
  - `Integer: ystop`
  - `Integer: xstop`
  - `Integer: rounded`
  - `Integer: edges`

#### `Plane.get_dim()`
Get dimensions of plane.

**Returns:**
  - `Array`

#### `Plane.get_pos()`
Get current position

**Returns:**
  - `Array`

#### `Plane.contains_point(screenY, screenX)`
Check if plane contains point at current position.

**Required Arguments:**
  - `Integer: screenY`
  - `Integer: screenX`

**Returns:**
  - `Bool`

#### `Plane.to_local(screenY, screenX)`
Convert screen position to local plane position

**Required Arguments:**
  - `Integer: screenY`
  - `Integer: screenX`

**Returns:**
  - `Array`

---

## Class: DropdownMenu
A dropdown menu using notcurses primitives.

**Example:**
```
```

**Required Arguments:**
  - `Plane: std`
  - `Integer: cols`
  - `Array: menuData`

### Methods

#### `DropdownMenu.draw_text_with_hotkey()`
Internal method, used by draw_barj

#### `DropdownMenu.draw_bar()`
Draw the menu bar

#### `DropdownMenu.open_dropdown()`
Internal method, used handle_event

#### `DropdownMenu.draw_dropdown()`
#### `DropdownMenu.close_dropdown()`
#### `DropdownMenu.handle_event(ev)`
Main loop, takes parameter from Notcurses.get_event().

**Required Arguments:**
  - `Event: ev`

---

## Class: Notcurses
Main notcurses library. FFI bindings to notcurses api.

**Optional Arguments:**
  - `Integer: flags`

### Methods

#### `Notcurses.enable_mouse([enable], [allow_motion])`
Enable mouse events

**Optional Arguments:**
  - `Bool: enable`
  - `Bool: allow_motion`

#### `Notcurses.translate_key(k)`
Translate notcurses keycode to string names

**Required Arguments:**
  - `Integer: k`

**Returns:**
  - `String`

#### `Notcurses.get_event()`
Return an event mapping

**Returns:**
  - `Mapping`

#### `Notcurses.render()`
Render output

#### `Notcurses.get_stdplane()`
Get the standard default plane.

**Returns:**
  - `Plane`

#### `Notcurses.get_key_code()`
Get the current key code

**Returns:**
  - `Integer`

#### `Notcurses.get_key()`
Calls get_event() to get a key press.

#### `Notcurses.stop()`
Turns off notcurses display, restoring original display parameters. It does not clear the screen, so current
output on the screen remains.

---

## Class: Postgres
### Methods

#### `Postgres.with(connStr, callback)`
Run a callback against a postgres database. Closes connection after callback is complete.

**Required Arguments:**
  - `String: connStr`
  - `Value: callback`

---

## Class: Polynomial
Class to manipulate polynomials with matrices and possible complex numbers.

**Required Arguments:**
  - `Array: coeffs`

### Methods

#### `Polynomial.eval(x)`
Evaluate P(x) using horner's method

**Required Arguments:**
  - `Number|Complex: x`

#### `Polynomial.companionMatrix()`
Construct the companion matrix

**Returns:**
  - `Matrix`

#### `Polynomial.roots()`
Solve roots via companion matrix eigenvalues

**Returns:**
  - `Array`

---

## Class: Process
Class for managing processes.  Includes pipes, fork/exec, system.

### Methods

#### `Process.captureShell(cmd)`
Simple method to run a command with `bash -c` and return the output.

**Required Arguments:**
  - `String: cmd`

**Returns:**
  - `String`

#### `Process.run(command)`
Run a process, waiting for it to finish. Only captures exit code. Any output
is lost unless redirected to a file. If output is needed to process, capture()
may be better.

**Required Arguments:**
  - `String: command`

**Returns:**
  - `Integer`

#### `Process.capture(command)`
Run a process from a shell, capturing stdout.

**Required Arguments:**
  - `String: command`

**Returns:**
  - `Result: String`

#### `Process.fork(processname)`
Spawn a new process, returns pid as a result.

**Required Arguments:**
  - `String: processname`

**Returns:**
  - `Result: pid`

#### `Process.exec(processname, [args], [env])`
Replace current process with a new one, passing arguments. Only returns if it failed.

**Required Arguments:**
  - `String: processname`

**Optional Arguments:**
  - `Array: args`
  - `Map: env`

**Returns:**
  - `Nil`

#### `Process.wait(pid)`
Wait on a process to exit

**Required Arguments:**
  - `Integer: pid`

**Returns:**
  - `Result: exitcode`

#### `Process.pipe()`
Create a pipe for communication between processes. Returns a map, {"read": fd, "write": fd}.

**Returns:**
  - `Result: pipemap`

#### `Process.read(fd)`
Read from a file descriptor. Mainly used for reading from pipes.

**Required Arguments:**
  - `Integer: fd`

**Returns:**
  - `Result: bytes`

#### `Process.close(fd)`
Close a process descriptor. Mainly used for pipes.

**Required Arguments:**
  - `Integer: fd`

**Returns:**
  - `Result`

#### `Process.signal(fd)`
Send a signal to a process

**Required Arguments:**
  - `Integer: fd`

**Returns:**
  - `Bool`

#### `Process.write(fd, message)`
Send data to a file descriptor. Mainly used for pipes.

**Required Arguments:**
  - `Integer: fd`
  - `String: message`

**Returns:**
  - `Result: byteswritten`

#### `Process.popen(command)`
Open a process with a pipe. The pipe is one-way, either reading from process, or writing to process,
specified by second argument, either "r" or "w", default read. Returns a File instance

**Required Arguments:**
  - `String: command`

**Returns:**
  - `File`

---

## Class: PriorityQueue
Implementation of a priority queue. Defaults to standard (a < b) check if comparator callback
not provided.

**Optional Arguments:**
  - `Value: comparator`

### Methods

#### `PriorityQueue.isEmpty()`
Checks if heap is empty.

**Returns:**
  - `Bool`

#### `PriorityQueue.size()`
Returns size of heap

**Returns:**
  - `Integer`

#### `PriorityQueue.peek()`
Get top of heap, or nil

**Returns:**
  - `Value`

#### `PriorityQueue.push(val)`
Adds value to heap, then restores heap order..

**Required Arguments:**
  - `Value: val`

#### `PriorityQueue.pop()`
Removes top entry nd restores heap order.

---

## Class: AVLNode
Node of AVL tree

**Required Arguments:**
  - `Value: val`

---

## Class: AVLTree
AVL Tree implementation. Uses AVLnode as nodes.

**Optional Arguments:**
  - `Value: comparator`

### Methods

#### `AVLTree.insert(val)`
Add item to tree.

**Required Arguments:**
  - `Value: val`

#### `AVLTree.contains(val)`
Checks if this branch contains the item

**Required Arguments:**
  - `Value: val`

**Returns:**
  - `Bool`

#### `AVLTree.inOrder(callback)`
Visit tree in order

**Required Arguments:**
  - `Value: callback`

#### `AVLTree.preOrder(callback)`
Visit tree pre-order

**Required Arguments:**
  - `Value: callback`

#### `AVLTree.postOrder(callback)`
Visit tree post-order

**Required Arguments:**
  - `Value: callback`

---

## Class: Readline
Readline class for adding readline support. Uses FFI.

### Methods

#### `Readline.prompt(p)`
Read a line with a prompt

**Required Arguments:**
  - `String: p`

**Returns:**
  - `String`

---

## Class: Scrollbox
Scrollbox class
Creates a box for scrolling lists

**Required Arguments:**
  - `Notcurses: nc`
  - `Plane: parentPlane`
  - `Integer: y`
  - `Integer: x`
  - `Integer: width`
  - `Integer: height`
  - `Array: items`

### Methods

#### `Scrollbox.onKey(key, callback)`
Set callback for specific keys

**Required Arguments:**
  - `String: key`
  - `Value: callback`

#### `Scrollbox.destroy()`
Destroy scrollbox

#### `Scrollbox.getVisibleItems()`
Get list of items that have been selected

**Returns:**
  - `Array`

#### `Scrollbox.selectItem(item)`
Select an item

**Required Arguments:**
  - `String: item`

#### `Scrollbox.deselectItem(item)`
Unselect an item

**Required Arguments:**
  - `String: item`

#### `Scrollbox.clearSelection()`
Clear selection list

#### `Scrollbox.setFocus()`
Change focus status of this scrollbox
Required:
Bool: isFocused

#### `Scrollbox.selectAll(state)`
Select all items. Change state of all items.

**Required Arguments:**
  - `Bool: state`

#### `Scrollbox.getSelectedItems()`
Get list of items that have been selected

**Returns:**
  - `Array`

#### `Scrollbox.render()`
Render this box

#### `Scrollbox.update_scroll()`
Update scroll list

#### `Scrollbox.handleInput(key)`
Handle input
Dispatch callbacks on keys

**Required Arguments:**
  - `String: key`

**Returns:**
  - `Bool`

---

## Class: SloxFormatter
Class to format slox code. Backend to sfmt command.

**Optional Arguments:**
  - `Integer: maxLineLength`

### Methods

#### `SloxFormatter.format(text)`
Format the text according to standard slox practices. FIrst parses with SloxLexer, then writes tokens
in standard slox format.

**Required Arguments:**
  - `String: text`

---

## Class: SignalManager
Class to implement something similar to a signal bus.

### Methods

#### `SignalManager.connect(target, signalName, callback)`
Connect a callback to an object's signal

**Required Arguments:**
  - `Value: target`
  - `String: signalName`
  - `Value: callback`

**Returns:**
  - `Value: callback`

#### `SignalManager.disconnect(target, signalName, callback)`
Disconnect a callback (or all callbacks for a target/signal)

**Required Arguments:**
  - `Value: target`
  - `String: signalName`
  - `Value: callback`

#### `SignalManager.emit(target, signalName, [data])`
Emit a signal on a target object, passing optional data

**Required Arguments:**
  - `Value: target`
  - `String: signalName`

**Optional Arguments:**
  - `Value: data`

---

## Class: SloxColorizer
Colorize slox code for either html or terminal ansi.

### Methods

#### `SloxColorizer.escapeHtml(text)`
Escape some characters for html.

**Required Arguments:**
  - `String: text`

#### `SloxColorizer.toTerminal(source)`
Convert source to text with ansi color codes.

**Required Arguments:**
  - `String: source`

**Returns:**
  - `String`

#### `SloxColorizer.toHtml(source)`
Convert source to text with html colors.

**Required Arguments:**
  - `String: source`

**Returns:**
  - `String`

---

## Class: Sqlite
### Methods

#### `Sqlite.with(path, callback)`
Block oriented database call. Opens database, runs callback, then closes database.

**Required Arguments:**
  - `String: path`
  - `Value: callback`

#### `Sqlite.transaction(callback)`
Run a callback as a transaction. If it fails, it rolls back the transaction automatically.

**Required Arguments:**
  - `Value: callback`

---

## Class: Stats
Statistics library
Most functions do not modify the original array, but return a new one.

### Methods

#### `Stats.sum(arr)`
Get sum of array

**Required Arguments:**
  - `Array: arr`

**Returns:**
  - `Array`

#### `Stats.mean(arr)`
Get mean of array

**Required Arguments:**
  - `Array: arr`

**Returns:**
  - `Array`

#### `Stats.variance(arr, [isSample])`
Get variance of array

**Required Arguments:**
  - `Array: arr`

**Optional Arguments:**
  - `Bool: isSample`

**Returns:**
  - `Number`

#### `Stats.stdDev(arr, [isSample])`
Standard Deviation

**Required Arguments:**
  - `Array: arr`

**Optional Arguments:**
  - `Bool: isSample`

**Returns:**
  - `Number`

#### `Stats.min(arr)`
Get smallest value of array

**Required Arguments:**
  - `Array: arr`

**Returns:**
  - `Number`

#### `Stats.max(arr)`
Get largest value of array

**Required Arguments:**
  - `Array: arr`

**Returns:**
  - `Number`

#### `Stats.standardize(arr)`
Z-score

**Required Arguments:**
  - `Array: arr`

**Returns:**
  - `Array`

#### `Stats.dotProduct(a, b)`
Compute dot product of 2 arrays

**Required Arguments:**
  - `Array: a`
  - `Array: b`

**Returns:**
  - `Number`

#### `Stats.covariance(a, b)`
Covariance

**Required Arguments:**
  - `Array: a`
  - `Array: b`

**Returns:**
  - `Number`

#### `Stats.correlation(a, b)`
Correlation of 2 arrays

**Required Arguments:**
  - `Array: a`
  - `Array: b`

**Returns:**
  - `Number`

#### `Stats.slope(x, y)`
Linear Regression Slope (m in y = mx + b)

**Required Arguments:**
  - `Array: x`
  - `Array: y`

**Returns:**
  - `Number`

#### `Stats.intercept(x, y)`
Linear Regression Y-Intercept (b in y = mx + b)

**Required Arguments:**
  - `Array: x`
  - `Array: y`

**Returns:**
  - `Number`

#### `Stats.predict(x, y, targetX)`
Predicts y value for a given x using simple linear regression

**Required Arguments:**
  - `Array: x`
  - `Array: y`
  - `Number: targetX`

**Returns:**
  - `Number`

#### `Stats.sma(arr, k)`
Simple Moving Average with window size k

**Required Arguments:**
  - `Array: arr`
  - `Integer: k`

**Returns:**
  - `Array`

#### `Stats.ema(arr, k, [alpha])`
Exponential Moving Average with smooting multiplier alpha (0 < alpha <= 1)
If alpha is nil/omitted, defaults to standard formula: 2 / (k + 1)

**Required Arguments:**
  - `Array: arr`
  - `Number: k`

**Optional Arguments:**
  - `Number: alpha`

**Returns:**
  - `Array`

#### `Stats.percentile(arr, [p])`
Linear interpolation percentile (p from 0 to 100)

**Required Arguments:**
  - `Array: arr`

**Optional Arguments:**
  - `Number: p`

**Returns:**
  - `Number`

#### `Stats.p90(arr)`
90th percentil

**Required Arguments:**
  - `Array: arr`

**Returns:**
  - `Array`

#### `Stats.p95(arr)`
95th percentil

**Required Arguments:**
  - `Array: arr`

**Returns:**
  - `Array`

#### `Stats.p99(arr)`
99th percentil

**Required Arguments:**
  - `Array: arr`

**Returns:**
  - `Array`

#### `Stats.iqr(arr)`
Interquartile Range (Q3 - Q1)

**Required Arguments:**
  - `Array: arr`

**Returns:**
  - `Number`

#### `Stats.outliers(arr, [k])`
Detect outliers using IQR (Tukey's Fences)
Returns array of outlier values. k defaults to 1.5

**Required Arguments:**
  - `Array: arr`

**Optional Arguments:**
  - `Number: k`

**Returns:**
  - `Array`

#### `Stats.outliersZScore(arr, [threshold])`
Detect outliers using Z-Score threshold
Returns array of outlier values. threshold defaults to 3.0

**Required Arguments:**
  - `Array: arr`

**Optional Arguments:**
  - `Number: threshold`

**Returns:**
  - `Array`

#### `Stats.isOutlier(arr, [k])`
Returns array of booleans [true, false, ...] matching input array indices

**Required Arguments:**
  - `Array: arr`

**Optional Arguments:**
  - `Number: k`

**Returns:**
  - `Array`

#### `Stats.frequency(arr)`
Counts occurrences of each distinct element

**Required Arguments:**
  - `Array: arr`

**Returns:**
  - `Set`

#### `Stats.mode(arr)`
Returns array of modes (handles unimodal, multimodal, or no mode)

**Required Arguments:**
  - `Array: arr`

**Returns:**
  - `Array`

#### `Stats.linearRegression(xArr, yArr)`
Calculate linearRegression of two arrays

**Required Arguments:**
  - `Array: xArr`
  - `Array: yArr`

**Returns:**
  - `Map`

#### `Stats.predict(x, regResult)`
Takes result of linearRegression to predict y intercept

**Required Arguments:**
  - `Array: x`
  - `Map: regResult`

**Returns:**
  - `Number`

#### `Stats.spearman(xArr, yArr)`
Spearman

**Required Arguments:**
  - `Array: xArr`
  - `Array: yArr`

**Returns:**
  - `Number`

---

## Class: Array
Most Stats methods can be used on an array as in arra.sum().
Array class type for native array type.
An array can be created with Array(1, 2, 3)
or [1, 2, 3].
Arrays can also be filled and with a preset length
using [0; 25].
Arrays can be accessed with an index like a[5], both
get and set. You can only access an index that already exists,
otherwise it will cause an out of bounds error.  Arrays can be
expanded using .push(), and the last item can be removed/retrieved
with .pop().
Array native type. An array holds a list of values. Can be created with [] or
Array(). Can be initialized with a values in the initializer: [1,2,3,4] or
Array(1,2,3,4). Also can be filled with a specific value: [0; 256].
An array is a specific size; you can't access an index that doesn't exist.
You can extend an array using .push().

**Optional Arguments:**
  - `Comma separated list of values`

**Returns:**
  - `Array`

### Methods

#### `Array.iter()`
Returns an iterator so foreach can traverse the array.

**Returns:**
  - `ArrayIterator`

#### `Array.zipWith(other, transformFn)`
Combines 2 arrays, transformed by a function like f(array1[i], array2[i]).

**Required Arguments:**
  - `Array: other`
  - `Value: transformFn`

**Returns:**
  - `Array`

#### `Array.zip(o)`
zipWIth with no transform function.

**Required Arguments:**
  - `Array: o ther`

**Returns:**
  - `Array`

#### `Array.chunk(size)`
Create an array of chunks of an array. Takes a chunk size, and returns a new array
split by the chunk size. The last element will be the remainder.

**Required Arguments:**
  - `Integer: size`

**Returns:**
  - `Array`

#### `Array.partition(predicateFn)`
Companion to filter/select. Items that resolve true end up in the first set, false in the second. Creates
new array containing true and false sets.

**Required Arguments:**
  - `Value: predicateFn`

**Returns:**
  - `Array`

#### `Array.push(val)`
Add value to end of array

**Required Arguments:**
  - `Value: val`

**Returns:**
  - `Array`

#### `Array.pop()`
Remove value from end of array

**Returns:**
  - `Value: removed value`

#### `Array.length()`
Get length of string. This is the internal string length, not necessarily the printable string length.

**Returns:**
  - `Value`

#### `Array.map(callback)`
Run a callback on each element. The function should return a new value. The final result
is a new array with new items. Callback should accept one value, which would be the current item.

**Required Arguments:**
  - `Value: callback`

**Returns:**
  - `Array`

#### `Array.dup()`
Get a new copy of this array. This is a shallow copy, it only duplicates the first level elements of the array.

**Returns:**
  - `Array`

#### `Array.is_empty()`
Check if array is empty.

**Returns:**
  - `Bool`

#### `Array.filter(callback)`
Run a callback on each element. The function should return true or false. If true, the item will
be included in the new array. If false, item will be excluded. Callback should accept one value,
which would be the current item.

**Required Arguments:**
  - `Value: callback`

**Returns:**
  - `Array`

#### `Array.reduce(acc, callback)`
Run a callback on each element. The function should return a new value, dependent on current
value of acc and the current element.

**Required Arguments:**
  - `Value: acc`
  - `Value: callback`

**Returns:**
  - `Value`

#### `Array.join(sep)`
Combine array elements into a string. Array elements should be either a string, or something
that could be converted to a string. Items will be joined with sep between them.

**Required Arguments:**
  - `String: sep`

**Returns:**
  - `String`

#### `Array.each(callback)`
Runs callback on each element of array. Returns current array so operations can be chained.
Mainly used for side effects.

**Required Arguments:**
  - `Value: callback`

**Returns:**
  - `Array`

#### `Array.swap_delete_at(index)`
Delete item at index by swapping with last item. Does not maintain order.

**Required Arguments:**
  - `Number: index`

**Returns:**
  - `Value: deleted item`

#### `Array.delete_at(pos)`
Delete an array item at index. Moves everything after up one item.

**Required Arguments:**
  - `Integer: pos`

**Returns:**
  - `Array`

#### `Array.index_of(target)`
Gets index of an element of array.

**Required Arguments:**
  - `Value: target`

**Returns:**
  - `Integer`

#### `Array.find_index(callback)`
Find the index of an item satisfying a predicate. Callback should return true or false
based on whether the item satisfies it.  Returns index of first entry that matches.

**Required Arguments:**
  - `Value: callback`

**Returns:**
  - `Integer: index`

#### `Array.find(callback)`
Find the first item satisfying a predicate. Callback should return true or false
based on whether the item satisfies it.

**Required Arguments:**
  - `Value: callback`

**Returns:**
  - `Value`

#### `Array.has(val)`
Checks if array has a value.

**Required Arguments:**
  - `Value: val`

**Returns:**
  - `Bool`

#### `Array.slice(startoffset, endoffset)`
Gets a substring of a string.

**Required Arguments:**
  - `Integer: startoffset`
  - `Integer: endoffset`

**Returns:**
  - `String`

#### `Array.sort(callback)`
Sort array. If a callback is provided, will sort based on the callback.
Callback requires two arguments that are compared to each other, returning
-1, 0, -1 for less than, equal, or more than. This sorts in place. To get a new
array, duplicate it first.

**Required Arguments:**
  - `Value: callback`

**Returns:**
  - `Array`

#### `Array.sort_slice(startoffset, endoffset, callback)`
Sorts a slice of an array. This is not the same as slice(x,y).sort(), as that
would create a new array.  This sorts a portion of the current array. Same
requirements as sort().

**Required Arguments:**
  - `Integer: startoffset`
  - `Integer: endoffset`
  - `Value: callback`

**Returns:**
  - `Array`

#### `Array.reverse()`
Reverses an array in place.

**Returns:**
  - `Array`

#### `Array.flatten()`
Recursively flattens an array to make it 1-dimensional

**Returns:**
  - `Array`

#### `Array.to_string()`
Convert an array to string of bytes. All items must be an integer. Floats must be an exact integer.
Integers more than 255 will be truncated.

**Returns:**
  - `String`

#### `Array.first()`
Get first item of an array

**Returns:**
  - `Value`

#### `Array.rest()`
Gets all items after the first as a new array

**Returns:**
  - `Array`

#### `Array.split()`
Divide an array into first and rest. Combination of .first and .rest.

**Returns:**
  - `Array`

#### `Array.unwrap_or(default)`
If ok, get okval, otherwise return default value.

**Required Arguments:**
  - `Value: default`

---

## Class: System
Part of main core libraries
Various system methods.
Also initializes ARGS and ENV, and sets include paths

### Methods

#### `System.stats()`
Get system statistics

**Returns:**
  - `Map`

#### `System.get_times()`
Get timings like from time command

**Returns:**
  - `Map`

#### `System.get_memory_mb()`
Get memory usage.

**Returns:**
  - `Float`

#### `System.time()`
Get the current value of time()

**Returns:**
  - `Integer`

#### `System.realtime()`
Gets the realtime.

#### `System.sysstats()`
Gets system stats, mostly from rusage

**Returns:**
  - `Map`

#### `System.exit()`
Exit from program

**Returns:**
  - `Integer`

#### `System.mem()`
Get memory stats from statm

**Returns:**
  - `Map`

#### `System.backtrace()`
Get the current backtrace

**Returns:**
  - `Array`

#### `System.reset_stack()`
Clears from top of stack to end, for diagnostic purposes only.

#### `System.show_stack()`
Prints out the size of the stack

#### `System.get_precision()`
Get the floating point precision

**Returns:**
  - `Integer: current precision`

#### `System.set_precision(precision)`
Set the floating point precision

**Required Arguments:**
  - `Integer: precision`

**Returns:**
  - `Integer: precision`

#### `System.set_notation(type)`
Set notation style: 0 = %g, 1 = %e, 2 = %f

**Required Arguments:**
  - `Integer: type`

**Returns:**
  - `Integer: type`

#### `System.debug_print(mode, Returns;, mode)`
Set debug print mode, print code generation

**Required Arguments:**
  - `Bool: mode`
  - `Returns;`
  - `Bool: mode`

#### `System.trace(mode)`
Set trace mode

**Required Arguments:**
  - `Bool: mode`

**Returns:**
  - `Bool: mode`

#### `System.sleep(duration)`
Pause for some time, can be Int or Float

**Required Arguments:**
  - `Number: duration`

#### `System.get_includes()`
Get include path list

**Returns:**
  - `Array: list of include paths`

#### `System.add_include(path)`
Add aa path to include list

**Required Arguments:**
  - `String: path`

**Returns:**
  - `Bool: true if successful`

#### `System.remove_include(path)`
Remove path from include list

**Required Arguments:**
  - `String: path`

**Returns:**
  - `Array: current include path list`

---

## Class: Error
Error class. Used for throwing errors from so that `catch` can deal with it.
Error class for throwing errors and raising exceptions

**Required Arguments:**
  - `None`

**Optional Arguments:**
  - `message: String`

---

## Class: Iterable
Mixin class for implementing iterators. Classes include this mixin, then return an iterator.
This class uses the Iterator class to provide the map and each methods.
```
class Settings {
 include Iterable;

 init() {
   this.config = {};
 }

 set(key, value) {
   this.config[key] = value;
 }

 iter() {
   return MapIterator(this.config);
 }
}

Full example in examples/settings.lox
```

### Methods

#### `Iterable.each()`
Implements each for the iterator.

**Returns:**
  - `Value`

#### `Iterable.map()`
Implements map for the iterator.

**Returns:**
  - `Value`

---

## Class: ArrayIterator
The actual iterator for arrays. The class will implement iter(), which will return
ArrayIterator with the array.

### Methods

#### `ArrayIterator.done()`
Signifies that there is no more data for the iterator.

**Returns:**
  - `Bool`

#### `ArrayIterator.next()`
Gets next item.

**Returns:**
  - `Value`

#### `ArrayIterator.key()`
Gets the key, in this case, the index.

**Returns:**
  - `Integer`

#### `ArrayIterator.val()`
Gets the value

**Returns:**
  - `Value`

---

## Class: MapIterator
Iterator class for maps.

### Methods

#### `MapIterator.done()`
Signifies that there is no more data for the iterator.

**Returns:**
  - `Bool`

#### `MapIterator.next()`
Gets next item.

**Returns:**
  - `Array: key, value`

#### `MapIterator.key()`
Gets the key of the current entry.

**Returns:**
  - `Value`

#### `MapIterator.val()`
Gets the value of the current entry.

**Returns:**
  - `Value`

---

## Class: Map
Extends the native Map class with an iterator so foreach can traverse it.
Map/Hash table/Dictionary type.  Keys can be any type other than nil. Values can be any type.
Constructor can take a list of values that are converted to key/value pairs. Must be an
even number.
Map/Dictionary/Hash table of key/value entries.

**Optional Arguments:**
  - `Array: vals`

### Methods

#### `Map.keys()`
Get list of keys

**Returns:**
  - `Array: list of keys`

#### `Map.values()`
Get list of values

**Returns:**
  - `Array: list of values`

#### `Map.has()`
Check if map has a key

**Returns:**
  - `Bool: true if key exists`

#### `Map.remove(key)`
Remove keys from a map. Takes at least one argument.

**Required Arguments:**
  - `Value: key`

**Returns:**
  - `Map: removed keys`

#### `Map.length()`
**Returns:**
  - `Integer: number of items`

#### `Map.each()`
Executes a callback on each key/value pair.
Works on a copy of the map, so the map won't be modified by the callback.

**Returns:**
  - `Nil`

---

## Class: FieldIndex
Mixin (include) that adds custom __geti__ and __seti__ implementations.
With this, you can access fields of an instance with [], and set with []=.

**Example:**
```
```

### Methods

#### `FieldIndex.__geti__(key)`
Get value of field

**Required Arguments:**
  - `String: key`

**Returns:**
  - `Value`

#### `FieldIndex.__seti__(key, val)`
Set field to value

**Required Arguments:**
  - `String: key`
  - `Value: val`

**Returns:**
  - `Value`

---

## Class: Serializable
Add serializing features to a class, with .serialize().
deserialize() is a separate external function. Used as a mixin.
Currently broken, needs to be updated to split Integer/Float

### Methods

#### `Serializable.serialize()`
Recursively convert a Value to a String, so it can be written somewhere.

---

## Class: PP
Pretty printer class. Recursively creates a pretty printed version of the data and returns it
as a string.

### Methods

#### `PP.pp(obj)`
Main entrypoint. Called with PP.pp(obj)

**Required Arguments:**
  - `Value: obj`

**Returns:**
  - `String`

#### `PP.format(obj)`
The actual format method

**Required Arguments:**
  - `Value: obj`

**Returns:**
  - `String`

#### `PP.formatArray(arr)`
Pretty prints an array

**Required Arguments:**
  - `Array: arr`

**Returns:**
  - `String`

#### `PP.formatArray(map)`
Pretty prints a map

**Required Arguments:**
  - `Map: map`

**Returns:**
  - `String`

#### `PP.formatSet(set)`
Pretty prints a set

**Required Arguments:**
  - `Set: set`

**Returns:**
  - `STring`

#### `PP.formatInstance(instance)`
Pretty prints an instance

**Required Arguments:**
  - `Instance: instance`

**Returns:**
  - `String`

#### `PP.formatVec3(vec)`
Pretty prints a vec3

**Required Arguments:**
  - `Vec3: vec`

**Returns:**
  - `String`

---

## Class: AssertionError
Error class for assertions

---

## Class: Struct
Provides two methods, pack and unpack, for creating and splitting data.

### Methods

#### `Struct.packNative()`
Assume native ordering for pack

#### `Struct.unpackNative()`
Assume native ordering for unpack

#### `Struct.packNet()`
Assume network ordering for pack

#### `Struct.unpackNet()`
Assume network ordering for pack

#### `Struct.pack(format, values, [bigendian])`
Packs a bunch of data types into a String according to a format string. Default is
little endian mode.

**Required Arguments:**
  - `String: format`
  - `Array: values`

**Optional Arguments:**
  - `Bool: bigendian`

**Returns:**
  - `String`

#### `Struct.unpack(format, values, [bigendian])`
Unpacks a string into an array based on format. x = null, s = string, b = byte,
h = 2 bytes, i = 4 bytes, q = 8 bytes. Optional width specifier at beginning. Also can
specify endianness with < or > at the beginning, or optional argument.

**Required Arguments:**
  - `String: format`
  - `Array: values`

**Optional Arguments:**
  - `Bool: bigendian`

**Returns:**
  - `String`

---

## Class: Set
Special case of Map. Values are ignored, can keep a count of number
of times key has been added.

### Methods

#### `Set.iter()`
Returns an iterator over the set's keys so foreach can traverse it.

**Returns:**
  - `ArrayIterator`

#### `Set.multiset([Array])`
Create a new Set as a multiset. Use as an initializer like: var s = Set.multiset().

**Optional Arguments:**
  - `Array`

**Returns:**
  - `Set`

#### `Set.add()`
Add a key to the set. If a multiset, adds or increments count.

**Returns:**
  - `Set`

#### `Set.count()`
Gets count of a key, 1 if not a multiset

**Returns:**
  - `Integer: count`

#### `Set.remove()`
Removes a key from a set, regardless of whether it is a multiset or not

**Returns:**
  - `Value: removed key`

#### `Set.has()`
Checks if key is present

**Returns:**
  - `Bool`

#### `Set.length()`
Gets length of set

**Returns:**
  - `Integer: length`

#### `Set.set_multiset()`
Designates this set as multiset. Can only be changed if there are no items in set.

**Returns:**
  - `Bool: whether setting to multiset was successful`

#### `Set.keys()`
Gets list of keys from set

**Returns:**
  - `Array: list of keys`

#### `Set.to_map()`
Converts set to a regular map. Returns a new map.
Values are either count for a multiset, or 1 for a plain set.

**Returns:**
  - `Map: from original set`

---

## Class: TarEntry
Tar archive entry

**Required Arguments:**
  - `String: name`
  - `Integer: size`
  - `Integer: typeflag`

---

## Class: Timer
Timer class, runs through backend. Max 256 timers.
Timer class base

**Required Arguments:**
  - `Number: delayMs`
  - `Number: intervalMs`
  - `Value: callback`

### Methods

#### `Timer.cancel()`
Cancel this timer

#### `Timer.setTimeout(delayMs, callback)`
Create a Timer that delays for x ms before calling callback.

**Required Arguments:**
  - `Number: delayMs`
  - `Value: callback`

**Returns:**
  - `Timer`

#### `Timer.setInterval(intervalMs, callback)`
Create a timer that runs every x ms, calling a callback each time.

**Required Arguments:**
  - `Number: intervalMs`
  - `Value: callback`

**Returns:**
  - `Timer`

#### `Timer.cancelAll()`
Cancel all timers.

#### `Timer._start(delayMs, intervalMs, callback)`
Start timer

**Required Arguments:**
  - `Number: delayMs`
  - `Number: intervalMs`
  - `Value: callback`

**Returns:**
  - `Number: id`

#### `Timer._stop(handle)`
Stop timer

**Required Arguments:**
  - `Number: handle`

**Returns:**
  - `Bool: Successful or not`

#### `Timer._stopAll()`
Stop all timers

---

## Class: UUID
Library for generating Universally Unique Identifiers (UUID v4).

### Methods

#### `UUID.v4()`
Generates a pseudo-random UUID (version 4).

**Returns:**
  - `String`

#### `UUID.shortId()`
Generates a compact 8-character random hex token.

**Returns:**
  - `String`

#### `UUID.ulid()`
Generates a Universally Unique Lexicographically Sortable Identifier (ULID).

**Returns:**
  - `String`

---

## Class: Object
Base class for all objects

### Methods

#### `Object.isfloat()`
Checks if a value is a float

**Returns:**
  - `Bool: true if float`

#### `Object.isstring()`
Checks if a value is a string

**Returns:**
  - `Bool: true if string`

#### `Object.isbool()`
Checks if a value is a bool

**Returns:**
  - `Bool: true if bool`

#### `Object.isnil()`
Checks if a value is nil

**Returns:**
  - `Bool: true if nil`

#### `Object.isclass()`
Checks if a value is a class

**Returns:**
  - `Bool: true if class`

#### `Object.isinstance()`
Checks if a value is an instance

**Returns:**
  - `Bool: true if instance`

#### `Object.list_fields()`
Lists all fields of an object

**Returns:**
  - `Array`

#### `Object.get_field(key)`
Get the value of a field of an object

**Required Arguments:**
  - `String: key`

**Returns:**
  - `Value: Value of field, or nil if wrong type of object or field doesn't exist`

#### `Object.set_field(field, val)`
Set a field to a value

**Required Arguments:**
  - `String: field`
  - `Value: val`

#### `Object.alias()`
Alias a method name to another method

#### `Object.get_methods([recurse])`
Get list of methods available on this object.

**Optional Arguments:**
  - `Bool: recurse`

**Returns:**
  - `Array`

#### `Object.has_method(method)`
Check whether this object has a method.

**Aliases:** `responds_to`

**Required Arguments:**
  - `String: method`

**Returns:**
  - `Bool`

#### `Object.superclass()`
Get the superclass of this object.

**Aliases:** `get_superclass`

**Returns:**
  - `Class`

#### `Object.to_string()`
Returns a string representation of this object

**Returns:**
  - `String`

#### `Object.class()`
Get the class of this object

**Returns:**
  - `Class`

#### `Object.class_name()`
Get the class of this object as a String. Shortcut for String(x.class())

**Returns:**
  - `String`

#### `Object.freeze()`
Freeze an object so it can't be modified

**Returns:**
  - `Instance`

#### `Object.is_frozen()`
Check if an object is frozen

**Returns:**
  - `Bool`

---

## Class: String
String class. Will convert argment to string.

**Optional Arguments:**
  - `Value: val`

**Returns:**
  - `String`

### Methods

#### `String.trim()`
Trims whitespace from beginning and end of string

**Returns:**
  - `String: new string`

#### `String.contains(needle)`
Checks if a string contains another string.
If actual position is needed, use find().

**Required Arguments:**
  - `String: needle`

**Returns:**
  - `Bool: true if it does`

#### `String.find(needle)`
Checks if a string contains another string, or a character.

**Required Arguments:**
  - `String or Integer: needle`

**Returns:**
  - `Integer: index of first position of string`

#### `String.rfind(needle, [index])`
Checks if a string contains another string, searching from max index, default end

**Required Arguments:**
  - `String or Integer: needle`

**Optional Arguments:**
  - `Integer: index`

**Returns:**
  - `Integer: index of last position in string`

#### `String.to_upper()`
Convert string to upper case.

**Returns:**
  - `String: new string converted to upper case`

#### `String.to_lower()`
Convert string to lower case.

**Returns:**
  - `String: new string converted to lower case`

#### `String.length()`
Get length of string.

**Aliases:** `len`

**Returns:**
  - `Integer: length of string`

#### `String.length_utf8()`
Get length of utf8 string.
If is plain ascii, will be the same as .length().

**Returns:**
  - `Integer: length of string`

#### `String.split(sep)`
Split a string by another string, splitting by "" splits into characters

**Required Arguments:**
  - `String: sep`

**Returns:**
  - `Array`

#### `String.slice(start, [end])`
Get a slice (or substring) of a string. First parameter is start index,
second optional parameter is end index, or end of string.
Can use negative numbers to start from end of string, so str.slice(-4, -2) would be
slice from 4 characters from end of string to 2 characters from end of string.

**Required Arguments:**
  - `Value: start`

**Optional Arguments:**
  - `Value: end`

**Returns:**
  - `String: section of string`

#### `String.to_array()`
Convert string to array of integers, ascii values of characters.

**Returns:**
  - `Array`

#### `String.tokens()`
Splits a string on whitespace.
Only splits on whitespace, not on different character types. "function(" would be one token still.

**Returns:**
  - `Array`

#### `String.pad_center(width, fillchar)`
Pad a string to a specific size with a pad character, default space

**Required Arguments:**
  - `Value: width`
  - `String: fillchar`

#### `String.pad_right(width, fillchar)`
Pad a string to a specific size with a pad character, default space

**Required Arguments:**
  - `Value: width`
  - `String: fillchar`

#### `String.pad_left(width, fillchar)`
Pad a string to a specific size with a pad character, default space

**Required Arguments:**
  - `Value: width`
  - `String: fillchar`

#### `String.format(format, args)`
Get a string format similar to printf(). Format string recognizes s, d, f, b, for string, int/float, and bool.

**Required Arguments:**
  - `String: format`
  - `Args: args`

#### `String.replace(target, replacement)`
Replace a substring with another

**Required Arguments:**
  - `String: target`
  - `String: replacement`

**Returns:**
  - `String`

#### `String.tr(set1, set2)`
Similar to the tr tool, converts characters from one set of characters to another.

**Required Arguments:**
  - `String: set1`
  - `String: set2`

#### `String.parse([base])`
Parse a string into a Integer or Float, in a specific base (default 10)

**Optional Arguments:**
  - `Integer: base`

**Returns:**
  - `Number`

#### `String.from_hex()`
Converts a hex string to an Integer

**Returns:**
  - `Integer`

#### `String.from_bin()`
Converts a hex string to an Integer

**Returns:**
  - `Integer`

---

## Class: Math
Math class encompassing many math functions

### Methods

#### `Math.sqrt(val)`
Get sqrt of value

**Required Arguments:**
  - `Number: val`

**Returns:**
  - `Float: sqrt of value`

#### `Math.abs(val)`
Get absolute value of number

**Required Arguments:**
  - `Number: val`

**Returns:**
  - `Number`

#### `Math.floor(val)`
Get whole number less than current value. Returns int if it fits in int64,
otherwise float.

**Required Arguments:**
  - `Number: val`

**Returns:**
  - `Integer or Float`

#### `Math.ceil(val)`
Get whole number greater than current value. Returns int if it fits in int64,
otherwise float.

**Required Arguments:**
  - `Number: val`

**Returns:**
  - `Integer or Float`

#### `Math.random([min], [max])`
Gets a random number between [0, 1), or between [0, max), or between [min, max)

**Optional Arguments:**
  - `Number: min`
  - `Number: max`

**Returns:**
  - `Float or Integer`

#### `Math.exp(val)`
Get exp of number

**Required Arguments:**
  - `Number: val`

**Returns:**
  - `Float`

#### `Math.hex(val)`
Get hex string value of number.  Number must be integer, or float that is an exact integer value.

**Required Arguments:**
  - `Number: val`

**Returns:**
  - `String: hex value`

#### `Math.oct(val)`
Get octal string value of number.  Number must be integer, or float that is an exact integer value.

**Required Arguments:**
  - `Number: val`

**Returns:**
  - `String: octal string`

#### `Math.bin(val)`
Get octal string value of number.  Number must be integer, or float that is an exact integer value.

**Required Arguments:**
  - `Number: val`

**Returns:**
  - `String: binary string`

#### `Math.bit_test(val, bit)`
Test bit x (from right) in value. Returns true if set, false if not.

**Required Arguments:**
  - `Integer: val`
  - `Integer: bit`

**Returns:**
  - `Bool: true if set`

#### `Math.min(a, b)`
Gets min value between two values

**Required Arguments:**
  - `Number: a`
  - `Number: b`

**Returns:**
  - `Number: smallest number`

#### `Math.max(a, b)`
Gets max value between two values

**Required Arguments:**
  - `Number: a`
  - `Number: b`

**Returns:**
  - `Number: largest number`

#### `Math.parse(str, [base])`
Parse a string into a Integer or Float, in a specific base (default 10)

**Required Arguments:**
  - `String: str`

**Optional Arguments:**
  - `Integer: base`

**Returns:**
  - `Number`

#### `Math.from_hex(val)`
Converts a hex string to an Integer

**Required Arguments:**
  - `String: val`

**Returns:**
  - `Integer`

#### `Math.from_bin(val)`
Converts a hex string to an Integer

**Required Arguments:**
  - `String: val`

**Returns:**
  - `Integer`

#### `Math.round(val, [places])`
Round value to specific decimal places, default 0

**Required Arguments:**
  - `Number: val`

**Optional Arguments:**
  - `Integer: places`

**Returns:**
  - `Number: rounded number`

#### `Math.sin(val)`
Get sin of value

**Required Arguments:**
  - `Number: val`

**Returns:**
  - `Float`

#### `Math.tan(val)`
Get tan of value

**Required Arguments:**
  - `Number: val`

**Returns:**
  - `Float`

#### `Math.atan2(x, y)`
Get atan2 of two values

**Required Arguments:**
  - `Number: x`
  - `Number: y`

**Returns:**
  - `Float`

#### `Math.cos(val)`
Get cos of value

**Required Arguments:**
  - `Number: val`

**Returns:**
  - `Float`

#### `Math.acos(val)`
Get acos of value

**Required Arguments:**
  - `Number: val`

**Returns:**
  - `Float`

#### `Math.to_fixed(val, decimals)`
Get value as a string, rounded to some decimal places

**Required Arguments:**
  - `Number: val`
  - `Integer: decimals`

**Returns:**
  - `String`

---

## Class: Integer
Integer class for oop methods on Integers

### Methods

#### `Integer.hex()`
Get hex string value of an integer.

**Returns:**
  - `String: hex value`

#### `Integer.oct()`
Get octal string value of number.

**Returns:**
  - `String: octal string`

#### `Integer.bin()`
Get octal string value of integer.

**Returns:**
  - `String: binary string`

#### `Integer.bit_test(bit)`
Test bit x (from right) in value. Returns true if set, false if not.

**Required Arguments:**
  - `Integer: bit`

**Returns:**
  - `Bool: true if set`

---

## Class: Float
Float class for oop methods on Floats
Float class, pseudo class for floats, handles IEEE 754 double

---

## Class: Number
Number class for oop methods on any Number type, Integers or Floats

### Methods

#### `Number.sqrt()`
Get sqrt of number

**Returns:**
  - `Float: sqrt of value`

#### `Number.abs()`
Get absolute value

**Returns:**
  - `Number`

#### `Number.floor()`
Get whole number less than current value. Returns int if it fits in int64,
otherwise float.

**Returns:**
  - `Integer or Float`

#### `Number.ceil()`
Get whole number greater than current value. Returns int if it fits in int64,
otherwise float.

**Returns:**
  - `Integer or Float`

#### `Number.random([max])`
Gets a random number between [0, 1), or between [0, max), or between [min, max)

**Optional Arguments:**
  - `Number: max`

**Returns:**
  - `Float or Integer`

#### `Number.exp()`
Get exp of number

**Returns:**
  - `Float`

#### `Number.min(b)`
Gets min value between two values

**Required Arguments:**
  - `Number: b`

**Returns:**
  - `Number: smallest number`

#### `Number.max(b)`
Gets max value between two values

**Required Arguments:**
  - `Number: b`

**Returns:**
  - `Number: largest number`

#### `Number.round([places])`
Round value to specific decimal places, default 0

**Optional Arguments:**
  - `Integer: places`

**Returns:**
  - `Number: rounded number`

#### `Number.sin()`
Get sin of value

**Returns:**
  - `Float`

#### `Number.tan()`
Get tan of value

**Returns:**
  - `Float`

#### `Number.atan2(x)`
Get atan2 of two values

**Required Arguments:**
  - `Number: x`

**Returns:**
  - `Float`

#### `Number.cos()`
Get cos of value

**Returns:**
  - `Float`

#### `Number.acos()`
Get acos of value

**Returns:**
  - `Float`

#### `Number.to_int()`
Converts a number to an integer. Integers stay the same, floats will
be converted to ints, with possible truncation.

**Returns:**
  - `Integer`

#### `Number.to_fixed(decimals)`
Get value as a string, rounded to some decimal places

**Required Arguments:**
  - `Integer: decimals`

**Returns:**
  - `String`

#### `Number.to_string()`
Converts a number to a string

**Returns:**
  - `String`

---

## Class: Result
Result type.  Evaluates to true if ok. If true, value can be accessed with .val, or .unwrap().
If false, error message can be accessed with .err.

**Required Arguments:**
  - `Bool: ok`

**Optional Arguments:**
  - `Value: okval`
  - `Value: errval`

### Methods

#### `Result.unwrap()`
Get ok value of result. If not ok, causes a runtime error.

**Returns:**
  - `Value: okval`

---

## Class: Option
Option type. Variant of Result type. is_some() means a value is present,
otherwise nil.

**Optional Arguments:**
  - `Value: val`

### Methods

#### `Option.unwrap()`
Get ok value, or error.

**Returns:**
  - `Value`

#### `Option.unwrap_or(default)`
Get ok value, or return default.

**Required Arguments:**
  - `Value: default`

**Returns:**
  - `Value`

#### `Option.is_some()`
Check if is Some

**Returns:**
  - `Bool`

#### `Option.is_none()`
Check if is None

**Returns:**
  - `Bool`

---

## Class: Regex
Class to implement regex operations. Create with a regex pattern:
var r = Regex("^\\d+");
Can use raw string r"" to pass escape sequences.

**Required Arguments:**
  - `String: pattern`

### Methods

#### `Regex.test(string)`
Test if a string matches.

**Required Arguments:**
  - `String: string`

**Returns:**
  - `Bool`

#### `Regex.match(string)`
Check string against regex. Get match groups

**Required Arguments:**
  - `String: string`

**Returns:**
  - `Array`

#### `Regex.get_pattern()`
Get the pattern stored in this regex.

**Returns:**
  - `String`

---

## Class: File
File library. Set of functions for reading, writing, and managing files.
Most return a Result instance, which evaluates to true if the result is ok,
or false if it had an error.

### Methods

#### `File.load(filename)`
Load a file as bytes into a string value.

**Required Arguments:**
  - `String: filename`

**Returns:**
  - `Result: String`

#### `File.save(pathname, contents)`
Save string buffer to a file. Overwrites any previous content.

**Required Arguments:**
  - `String: pathname`
  - `String: contents`

**Returns:**
  - `Result: Bool`

#### `File.exists(pathname)`
Checks if a path exists

**Required Arguments:**
  - `String: pathname`

**Returns:**
  - `Bool`

#### `File.open(pathname, [mode])`
Opens a file for reading or writing. STDOUT, STDERR, STDIN are special case names
for stdout, stderr, stdin.

**Required Arguments:**
  - `String: pathname`

**Optional Arguments:**
  - `String: mode`

**Returns:**
  - `Result: handle`

#### `File.read([len])`
Read data from a file

**Optional Arguments:**
  - `Integer: len`

**Returns:**
  - `Result: bytes`

#### `File.readline()`
Read a line

**Returns:**
  - `Result: line`

#### `File.write(content)`
Writes data to a file. Takes a string, and writes the length of the string. The
string is a byte buffer, not a printable string, so everything is written, it does
not stop at \0.

**Required Arguments:**
  - `String: content`

**Returns:**
  - `Result: byteswritten`

#### `File.close()`
Close a file handle

**Returns:**
  - `Result: closeresult`

#### `File.seek(offset)`
Seek to a location in a file.

**Required Arguments:**
  - `Integer: offset`

**Returns:**
  - `Result: seekresult`

#### `File.tell()`
Get current position in file.

**Returns:**
  - `Result: position`

#### `File.stderr()`
Gets stderr file handle

**Returns:**
  - `File: stderr`

#### `File.flush()`
Flushes buffered data to disk.

**Returns:**
  - `Result`

#### `File.copy(src, dest)`
Copy file from src to dest

**Required Arguments:**
  - `String: src`
  - `String: dest`

**Returns:**
  - `Result: byteswritten`

#### `File.chmod(pathname, mode)`
Change mode of file

**Required Arguments:**
  - `String: pathname`
  - `String: mode`

**Returns:**
  - `Result`

#### `File.chown(pathname, uid, gid)`
Change owner/group of file.

**Required Arguments:**
  - `String: pathname`
  - `Integer: uid`
  - `Integer: gid`

**Returns:**
  - `Result`

#### `File.unlink(pathname)`
Remove a file link. If it is the last link, the file is deleted.

**Required Arguments:**
  - `String: pathname`

**Returns:**
  - `Bool`

#### `File.rename(pathname)`
Rename a file.

**Required Arguments:**
  - `String: pathname`

**Returns:**
  - `Bool`

---

## Class: Process:
### Methods

#### `Process:.pid()`
Get current pid

**Returns:**
  - `Integer`

---

## Class: GC
GC interface. Control various aspeccts of the garbage collector.

### Methods

#### `GC.heap_growth_factor(val)`
Set the heap growth factor. Default is 2.0. Must be 1.1 or greater.

**Required Arguments:**
  - `Number: val`

#### `GC.get_growth_factor()`
Get the current growth factor

**Returns:**
  - `Float`

#### `GC.set_log(enable)`
Toggle whether to log gc events.

**Required Arguments:**
  - `Bool: enable`

**Returns:**
  - `Bool: previous`

#### `GC.get_log()`
Get current gc log value.

**Returns:**
  - `Bool`

#### `GC.bump_size(bumpsize)`
Set bump size

**Required Arguments:**
  - `Integer: bumpsize`

**Returns:**
  - `Integer: bumpsize`

#### `GC.get_bumpsize()`
Get the current bumpsize

**Returns:**
  - `Integer`

#### `GC.stress_mode(mode)`
Set stress mode. Valid values are GC.NormalMode, GC.StressMode, GC.DisableMode

**Required Arguments:**
  - `Integer: mode`

**Returns:**
  - `Integer`

#### `GC.get_stressmode()`
Gets current stress mode

**Returns:**
  - `Integer`

#### `GC.type(type)`
Set the gc type. GC.TypeLinear = Linear/Bump, GC.TypeMult = Multiplier

**Required Arguments:**
  - `Integer: type`

**Returns:**
  - `Integer: type`

#### `GC.get_gctype()`
Get the current gc type

**Returns:**
  - `Integer`

#### `GC.gc()`
Run the GC. Also available under System.gc()

#### `GC.stats()`
Get gc stats

**Returns:**
  - `Map`

---

## Class: IO
Base class for TCP and UDP.
Should not be called directly.  Should use IO.tcp() or IO.udp() to get the specific type.

### Methods

#### `IO.inspect()`
Low level function to print io internal information. Prints directly to stdout.

#### `IO.connect(ip, port)`
Connect to a remote endpoint. All data is internal to the class.

**Required Arguments:**
  - `String: ip`
  - `Integer: port`

**Returns:**
  - `Bool`

#### `IO.send(data)`
Send data through a connection. Should be connected firsst.

**Required Arguments:**
  - `String: data`

**Returns:**
  - `Result: bytessent`

#### `IO.recv(buffersize)`
Receive data from remote. Requires size of buffer for max amount of data to receive.
Result is data, length is amount of data received, which could be less than max size.

**Required Arguments:**
  - `Integer: buffersize`

**Returns:**
  - `Result: data`

#### `IO.listen(port)`
Listen on a port

**Required Arguments:**
  - `Integer: port`

**Returns:**
  - `Result`

#### `IO.accept()`
Accept a connection

**Returns:**
  - `TCP: instance`

#### `IO.bind(ip, port)`
Bind to an ip and port

**Required Arguments:**
  - `String: ip`
  - `Integer: port`

**Returns:**
  - `Result`

#### `IO.readable()`
Checks if there is data to be read.

**Returns:**
  - `Result: readable`

#### `IO.close()`
Close an io connection. Resets it completely, it cannot be reused. Should be discarded
and a new instance created.

**Returns:**
  - `Result`

#### `IO.poll(instances)`
Checks socket instances to see which ones have active events.

**Required Arguments:**
  - `Array: instances`

**Returns:**
  - `Array`

#### `IO.set_recv_timeout()`
Set receive timeout

---

## Class: Base64
### Methods

#### `Base64.encode(input)`
Encodes text into base64 code

**Required Arguments:**
  - `String: input`

**Returns:**
  - `String: encoded text`

#### `Base64.decode(input)`
Decode a String into original text

**Required Arguments:**
  - `String: input`

**Returns:**
  - `String: decoded text`

---

## Class: Buffer
### Methods

#### `Buffer.alloc(size)`
Allocate a Buffer of size bytes

**Required Arguments:**
  - `Integer: size`

**Returns:**
  - `Buffer: a buffer`

#### `Buffer.size()`
Get size of Buffer

**Returns:**
  - `Integer: size of buffer`

#### `Buffer.to_string()`
Converts a Buffer into a String

**Returns:**
  - `String: Buffers as a string`

#### `Buffer.to_array()`
Converts a Buffer into an array of bytes

**Returns:**
  - `Array: byte list`

#### `Buffer.slice()`
Get a slice of bytes as a new buffer

**Returns:**
  - `Buffer: new buffer`

#### `Buffer.fill()`
Fill buffer with a value

#### `Buffer.write_uint32(offset, value)`
Write a uint32 value to offset

**Required Arguments:**
  - `Integer: offset`
  - `Integer: value`

**Returns:**
  - `Integer: next offset`

#### `Buffer.write_uint64(offset, value)`
Write a uint64 value to offset

**Required Arguments:**
  - `Integer: offset`
  - `Integer: value`

**Returns:**
  - `Integer: next offset`

#### `Buffer.write_uint8()`
Write uint8 value to offset

**Returns:**
  - `Integer: next offset`

#### `Buffer.get_ptr()`
Return ptr value as String

**Returns:**
  - `String: pointer value`

---

## Class: Signal
### Methods

#### `Signal.trap(signum, callback)`
**Required Arguments:**
  - `Number: signum`
  - `Callback: callback`

**Returns:**
  - `Nil`

---

# Global Functions

## `benchmark(fn)`
Quick and simple benchmark. Runs function once and returns time spent.

**Required Arguments:**
  - `Value: fn`

**Returns:**
  - `Map: stats`

---

## `printBenchmark(name, stats)`
Given a name and stats from benchmark(), prints out a summary.

**Required Arguments:**
  - `String: name`
  - `Map: stats`

---

## `deserialize()`
Convert a string to its original Value recursively.
Currently broken.

---

## `pp(obj)`
Simple function that calls PP.pp() and prints the result. Calling PP.pp() directly
Gets the output as a string but doesn't print it.

**Required Arguments:**
  - `Value: obj`

---

## `sendAll()`
Send all bytes in a string, perhaps with multiple send calls
if not all were sent at first.

---

## `readExact()`
read

---

## `assert(expr)`
Test expr, and if false, throw an AssertionError

**Required Arguments:**
  - `Value: expr`

---

## `stringReplaceAll(str, target, replacement)`
Replace all occurrances of target in str with replacement

**Required Arguments:**
  - `String: str`
  - `String: target`
  - `String: replacement`

**Returns:**
  - `String`

---

## `isHexDigit(c)`
Test if character is a hex digit. Character must be the ascii code for the character. 5 is false, '5' is true.

**Required Arguments:**
  - `Integer: c`

---

## `isOctDigit(c)`
Test if character is a octal digit. Character must be the ascii code for the character. 5 is false, '5' is true.

**Required Arguments:**
  - `Integer: c`

---

## `isDigit(c)`
Test if character is a decimal digit. Character must be the ascii code for the character. 5 is false, '5' is true.

**Required Arguments:**
  - `Integer: c`

---

## `isAlpha(c)`
Test if character is an alpha character. Includes '_' for identifiers.

**Required Arguments:**
  - `Integer: c`

---

## `isAlphaNumeric(c)`
Test if character is an alpha character or digit. Includes '_' for identifiers.

**Required Arguments:**
  - `Integer: c`

---

## `parseBinary()`
Parse a string as binary.  Skips leading '%', and stops at first character that isn't '0' or '1'.

**Returns:**
  - `Integer`

---

## `clock()`
Interface to c function clock(3)

---

## `str(val)`
Converts current value to its string representation

**Required Arguments:**
  - `Value: val`

**Returns:**
  - `String: string value`

---

## `typeof(val)`
Gets class type of val

**Required Arguments:**
  - `Value: val`

**Returns:**
  - `String: class name`

---

## `chr(val)`
Turns a number into a character

**Required Arguments:**
  - `Integer: val`

**Returns:**
  - `String: string value`

---

## `eval(str)`
Evaluates a string as slox code.

**Required Arguments:**
  - `String: str`

---

## `create_instance(class)`
Creates a new instance from a class name.
Does not run init()

**Required Arguments:**
  - `String: class`

**Returns:**
  - `Instance: instance of class`

---

## `program(filename)`
Creates a new class based off the name of the file. Not totally working.
Then an instance can be created with class_name().

**Required Arguments:**
  - `String: filename`

**Returns:**
  - `Class: class value`

---

## `isfloat(val)`
Checks if a value is a float.

**Required Arguments:**
  - `Value: val`

**Returns:**
  - `Bool: true if float`

---

## `isnumber(val)`
Checks if a value is a number (either float or integer).

**Required Arguments:**
  - `Value: val`

**Returns:**
  - `Bool: true if number`

---

## `isint(val)`
Checks if a value is an integer.

**Required Arguments:**
  - `Value: val`

**Returns:**
  - `Bool: true if int`

---

## `isstring(val)`
Checks if a value is a string.

**Required Arguments:**
  - `Value: val`

**Returns:**
  - `Bool: true if string`

---

## `isbool(val)`
Checks if a value is a bool (boolean)

**Required Arguments:**
  - `Value: val`

**Returns:**
  - `Bool: true if bool`

---

## `isnil(val)`
Checks if a value is nil

**Required Arguments:**
  - `Value: val`

**Returns:**
  - `Bool: true if nil`

---

## `isclass(val)`
Checks if a value is a class

**Required Arguments:**
  - `Value: val`

**Returns:**
  - `Bool: true if class`

---

## `isinstance(val)`
Checks if a value is an instance

**Required Arguments:**
  - `Value: val`

**Returns:**
  - `Bool: true if instance`

---

## `require(filename, [true])`
Load/require lox code from a file

**Required Arguments:**
  - `String: filename`

**Optional Arguments:**
  - `Bool: true if overwrite cache`

**Returns:**
  - `Bool: true if successful`

---

## `inspect(object)`
Get list of classes, methods, and fields on this instance/class

**Required Arguments:**
  - `Value: object`

---
