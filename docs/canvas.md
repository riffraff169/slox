# X11 + Cairo canvas

`lib/canvas.lox` is a small retained-mode drawing library on top of Xlib and
Cairo. It is pure Lox: `lib/x11.lox` and `lib/cairo.lox` use FFI, and
`canvas.lox` adds drawing helpers, plotting, and widgets.

```lox
require("canvas.lox");

var canvas = Canvas.window(800, 600, "plot");
var graph = Graph(canvas, -6.28, 6.28, -2, 2);

fun draw(c) {
  c.clear();
  graph.draw();
  graph.plot(fun(x) { return Math.sin(x); });
}

canvas.run(draw);
canvas.close();
```

Run from the repository root so native modules and `lib/` are found. If
`DISPLAY` is not set, or the display cannot be opened, a window canvas falls
back to an offscreen image surface. Use `--png` style offscreen rendering for
headless tests and screenshots:

```lox
var canvas = Canvas.offscreen(800, 600);
canvas.render(draw);
canvas.savePng("plot.png");
canvas.close();
```

## Canvas

```lox
Canvas(width, height, title = nil, options = nil)
Canvas.window(width, height, title = "slox canvas", options = nil)
Canvas.offscreen(width, height, options = nil)
```

Options: `offscreen`, `x`, `y`, `fontFamily`, `fontSize`, `background`.

Shared helpers are static methods on `Canvas`: `option`, `copyMap`, `env`,
`displaySize`, `clamp`, `compareText`, `truncateText`, `rgba`, `niceStep`,
`tickDecimals`, and `tickLabel`. `displaySize()` returns `[width, height]` for
the default X11 screen, or `nil` when no display is available. Keeping helpers
on `Canvas` makes it clear where they are defined and lets other Canvas widgets
call them without global helper functions.

Drawing methods:

| Method | Description |
| --- | --- |
| `clear(color)` | Fill the canvas |
| `setColor(color)` | Set the Cairo source color |
| `line(x1, y1, x2, y2, color, width)` | Stroke a line |
| `polyline(points, color, width)` | Stroke connected `[x, y]` points |
| `fillRect(x, y, w, h, color)` | Filled rectangle |
| `strokeRect(x, y, w, h, color, width)` | Rectangle outline |
| `rect(x, y, w, h, color, fill, width)` | Either rectangle form |
| `circle(cx, cy, r, color, fill, width)` | Circle |
| `ellipse(cx, cy, rx, ry, color, fill, width)` | Ellipse |
| `arc(cx, cy, r, a1, a2, color, width)` | Arc in radians |
| `text(x, y, text, color, size, family)` | Text with baseline at `y` |
| `textTop(x, y, ...)`, `textMiddle(...)` | Convenience positioning |
| `textWidth`, `textHeight`, `textExtents` | Measure text |
| `setDash(dashes, offset)`, `clearDash()` | Dashed strokes |
| `save()`, `restore()`, `translate()`, `scale()`, `rotate()` | Cairo state/transforms |
| `clipRect(x, y, w, h)` | Clip (pair with save/restore) |
| `resize(width, height)` | Resize the window or offscreen surface |
| `setTitle(title)` | Change the X11 window title |
| `present()`, `flush()`, `sync()` | Push drawing to the window |
| `savePng(path)` | Write the surface as PNG |

Colors may be `0xRRGGBB`, `"#rrggbb"`, `"#rrggbbaa"`, or an
`[r, g, b]`/`[r, g, b, a]` array. Array components can be either 0..1 or
0..255; values above 1 are treated as 0..255.

Event handling:

```lox
canvas.on("keypress", fun(c, event) {
  print event["keysym"] + " " + event["text"];
});
canvas.on("buttonpress", fun(c, event) {
  print event["x"] + "," + event["y"];
});
canvas.on("close", fun(c, event) { /* window manager close */ });
canvas.run(draw, {"fps": 60, "maxFrames": 0});
```

Callback names: `event`, `keypress`, `keyrelease`, `buttonpress`,
`buttonrelease`, `motion`, `expose`, `resize`, `enter`, `leave`, `close`.
Parsed events contain `type`, `x`, `y`, and type-specific fields. Key events
also contain `keysym` and `text`.

`run(draw, options)` calls `draw(canvas)` each frame, then draws registered
widgets, then presents. `options` supports `fps` (default 60) and `maxFrames`
(default 0, meaning until the window closes). Offscreen canvases render once
instead of entering a loop.

## Widgets

Widgets are added with `canvas.add(widget)`. Added widgets are drawn
automatically after the `draw` callback and receive events in reverse add
order, so later widgets (menus) get the first chance to consume an event.
Button callbacks take no arguments; Menu and ListWidget callbacks receive
`(item, index)`.

```lox
var button = Button(20, 20, 120, 32, "Save", fun() {
  canvas.savePng("out.png");
});

var menu = Menu(20, 60, 180, [
  {"label": "Open", "callback": fun() { }},
  {"label": "Quit", "callback": fun() { canvas.close(); }}
], nil, {"title": "File"});

var list = ListWidget(20, 150, 180, 120,
    ["First", "Second", {"label": "Disabled", "enabled": false}],
    fun(item, index) { print "" + index + ": " + item; },
    {"rowHeight": 24, "selected": 0});

canvas.add(list);
canvas.add(button);
canvas.add(menu);
```

- `Button(x, y, w, h, label, callback = nil, options = nil)` supports
  `color`, `textColor`, `hoverColor`, `pressedColor`, `borderColor`,
  `fontSize`, and `disabled`.
- `Menu(x, y, w, items, callback = nil, options = nil)` supports string items
  or maps with `label`, `callback`, and `enabled`. `open(x, y)` and `close()`
  show/hide it. Options include `title`, `rowHeight`, and `fontSize`.
- `ListWidget(x, y, w, h, items, callback = nil, options = nil)` is a
  scrollable selection list. Items may be strings or maps with `label`,
  `enabled`, and `callback`. It handles Up/Down, Page Up/Down, Home/End, wheel,
  and click; Return/Space activates the selection. Options include `rowHeight`,
  `fontSize`, `padding`, `selected`, `emptyText`, `scrollbar`,
  `activateOnClick`, `background`, `textColor`, `selectedColor`,
  `selectedTextColor`, and `borderColor`. Use `setItems(items, selected)`,
  `select(index)`, `move(delta)`, `selectedIndex()`, `selectedItem()`, and
  `activate()` to drive it from code. Call `handle(event, canvas)` and
  `draw(canvas)` directly if you are not using `canvas.add()`.
- `TextInput(x, y, w, h, options = nil)` supports `text`, `placeholder`,
  `focused`, `onSubmit`, and `onChange`. Call
  `input.handle(event, canvas)` directly if you are not using `canvas.add()`
  (Canvas passes the canvas automatically).

## Graph

```lox
var graph = Graph(canvas, xMin, xMax, yMin, yMax, options);
graph.draw();          // grid, axes, frame, tick labels
graph.plot(fun(x) { return Math.sin(x); }, color, lineWidth, samples);
graph.plotPoints([[0, 0], [1, 1]], color, lineWidth);
graph.plotParametric(fun(t) { return Math.cos(t); },
                     fun(t) { return Math.sin(t); }, 0, 6.28);
```

Options: `marginLeft`, `marginRight`, `marginTop`, `marginBottom`,
`gridColor`, `axisColor`, `labelColor`, `frameColor`, `fontSize`.
Coordinates are mapped with `mapX`/`mapY` and `unmapX`/`unmapY`.

## Displaying images

`require("image_view.lox")` connects the native `image` module (stb image
loading) to a Cairo-backed `Canvas`. It loads an image into a mutable Slox RGBA
array, builds a Cairo image surface from that array, and can draw it scaled to
any rectangle.

```lox
require("image_view.lox");

var view = ImageView.window("photo.jpg", 800, 600, "photo");
var canvas = view.canvas;

canvas.on("keypress", fun(c, event) {
  if (event["text"] == "q") c.close();
});

canvas.run(fun(c) {
  c.clear(Canvas.BG);
  view.draw(c, 0, 0, c.width, c.height);
});
view.close();
```

`ImageView.fromFile(path, options)` wraps an `Image.load()` result without
opening a window. `ImageView.window(path, width, height, title, options)` and
`ImageView.offscreen(path, width, height, options)` create a canvas too; a
window canvas falls back to offscreen rendering when no X11 display is
available. When no size is given, `ImageView.window()` scales large images down
to fit the X11 screen with a small margin. The `maxWidth` and `maxHeight`
options override that limit; `ImageView.fitSize(w, h, maxWidth, maxHeight)`
applies the same aspect-preserving scaling directly.

Useful methods:

| Method | Description |
| --- | --- |
| `getPixel(x, y)`, `setPixel(x, y, [r, g, b, a])` | Read/write one pixel. Values are 0..255. |
| `getPixels()` | Return the mutable RGBA byte array. |
| `refreshFromPixels()` | Upload direct array edits to the Cairo surface. |
| `refresh()` | Reload pixels from the underlying `Image` object. |
| `syncImage()` | Copy Slox pixel edits back into the `Image` object. |
| `draw(canvas, x, y, width, height, filter = nil)` | Draw the surface, scaled to the rectangle. In fit mode the image is centered in that rectangle with its aspect ratio preserved. `filter` is a `Cairo.FILTER_*` constant or `"nearest"`, `"fast"`, `"good"`, `"best"`, `"bilinear"`, or `"gaussian"`; the default is nearest. |
| `save(path)` | Save the image after syncing pixel edits. |
| `cairoSurface()` | Return the raw Cairo surface pointer. |
| `zoomFactor()`, `zoomSize()` | Current multiplier and display size. |
| `zoomIn()`, `zoomOut()`, `zoomReset()`, `zoomTo(factor)` | Change zoom by 10% steps (or set it, rounded to 10%), resizing the Canvas and exiting fit mode. |
| `fitToWindow(enabled = true)`, `isFitToWindow()` | Fit the image into the current Canvas without resizing it; disable to restore the zoomed size. |
| `fitRect(x, y, width, height)` | Return the letterboxed `[x, y, width, height]` used in fit mode. |
| `release()` | Destroy only the Cairo surface and buffer, leaving the canvas open. |
| `dispose()` | Alias for `release()`. |
| `close()` | Release the surface and close the canvas. |

Zoom never changes the image's pixel array or Cairo source surface. `draw()`
scales that original surface to the zoomed rectangle on each frame, so zooming
out and back in does not resample an already-resampled image. Zoom steps are
stored as integer tenths, so zoom in followed by zoom out returns to exactly
100%.

`examples/image_viewer.lox` uses `+`/`=` to zoom in, `-`/`_` to zoom out, `0`
to reset, `n`/`p` to step through images from the current directory, and `f` to
toggle fit-to-window (letterboxed, with the window size unchanged). Explicit
zoom or reset leaves fit mode. `q` quits both windows.

For a viewer that keeps one window open while swapping images, call
`view.release()` (or `view.dispose()`) before dropping a view and reuse the same
`Canvas`; call `view.refresh()` on a retained view to rebuild its surface after
a release. Use `Cairo.FILTER_NEAREST` for pixel-art or integer zoom and
`"bilinear"`/`Cairo.FILTER_GOOD` for smooth scaling.

The conversion from RGBA to Cairo's premultiplied ARGB32 is handled by the
library. Initial surface construction and `refresh()` use the image module's
native `Image.Data.copy_bgra_to()` fast path when available;
`refreshFromPixels()` uses `Image.copy_bgra_from_array()` for direct
`getPixels()` edits. Fallbacks are written in Lox. Filters can be written in
plain Lox:

```lox
var pixels = view.getPixels();
for (var i = 0; i < pixels.length(); i += 4) {
  pixels[i] = 255 - pixels[i];
  pixels[i + 1] = 255 - pixels[i + 1];
  pixels[i + 2] = 255 - pixels[i + 2];
}
view.refreshFromPixels();
```

The tracked `examples/image_view_demo.lox` shows a complete program. It opens a
window when `DISPLAY` is available and otherwise renders one frame to
`image_view_demo.png`. Press `i` to invert, `g` to grayscale, `r` to reset, and
`q` to quit.

## Browsing for images

`require("file_browser.lox")` adds a Canvas-based directory browser. When
`examples/image_viewer.lox` is given a directory instead of an image, it opens a
second window listing subdirectories and supported image files. Use Up/Down or
the mouse wheel to move, Enter/Space or a click to open an entry, and Esc (or the
window manager close button) to close the browser. The first image is selected
by default; directories are listed first, hidden files are skipped, and `..`
navigates to the parent directory.

Selecting an image leaves the browser open so another row can be chosen. In the
viewer, `n` and `p` load the next and previous images from the browser's current
directory and keep the browser highlight in sync; `q` quits both windows.

`FileBrowser` is a thin adapter over `ListWidget`: it scans directories and
supplies labeled rows, while the widget owns selection, scrolling, and drawing.
The underlying widget is available as `browser.list`.

```lox
require("file_browser.lox");

var browserCanvas = Canvas.window(520, 640, "Select image");
var browser = FileBrowser(dir, browserCanvas, fun(path) {
  print "selected: " + path;
}, {
  "title": "slox image browser",
  "onClose": fun(browser) { browserCanvas.close(); }
});

browserCanvas.on("event", fun(c, event) {
  browser.handleEvent(event, c);
});
browserCanvas.run(fun(c) {
  browser.draw(c);
});
```

`FileBrowser(directory, canvas, onSelect, options = nil)` reads the directory
with `Dir.list()` and calls `onSelect(path)` when an image is activated. It does
not load the image itself. Supported extensions are `png`, `jpg`, `jpeg`, `bmp`,
`gif`, `tga`, `pnm`, `ppm`, `pgm`, and `pbm` (case-insensitive). Options are
`title`, `rowHeight`, `fontSize`, and `onClose`; `onClose(browser)` is called
once when the browser is closed. Useful methods are `refresh()`,
`navigate(path)`, `moveSelection(delta)`, `selectedEntry()`, `imagePaths()`,
`selectPath(path)`, `handleEvent(event)`, `draw()`, and `close()`.

With no X11 display, the viewer's directory mode renders the browser once to
`file_browser.png`; selecting an image there renders it to `image_viewer.png`.

## Low-level Cairo and X11

`require("cairo.lox")` creates a `Cairo` object with FFI bindings and both
camelCase and snake_case methods:

```lox
var cairo = Cairo();
var surface = cairo.imageSurfaceCreate(cairo.FORMAT_ARGB32, 320, 240);
var cr = cairo.create(surface);
cairo.setSourceRGB(cr, 0.1, 0.2, 0.3);
cairo.paint(cr);
cairo.setDash(cr, [4.0, 2.0]);
cairo.textExtents(cr, "hello");
cairo.surfaceWriteToPng(surface, "out.png");
cairo.destroy(cr);
cairo.surfaceDestroy(surface);
```

`cairo.imageSurfaceCreateForData(buffer, format, width, height, stride)` wraps a
writable `Buffer`; keep the buffer alive while the surface exists. Pixel data
can be refreshed with `cairo.surfaceMarkDirty(surface)`. To draw one surface
onto another, use `cairo.setSourceSurface(cr, surface, x, y)`; the pattern
filter can be set with `cairo.patternSetFilter(cairo.getSource(cr),
Cairo.FILTER_NEAREST)`.

`require("x11.lox")` creates an `X11` object. `parse_event(buffer)` returns a
Map for key, button, motion, expose, configure, and client-message events;
`lookup_string(buffer)` returns `text`, `keysym`, and `bytes_read`. The
`eventType`, `eventX`, and `eventY` helpers are available for older code.

`Struct.pack` and `Struct.unpack` understand `f` (32-bit float) and `d`
(64-bit double), which the Cairo bindings use for dash arrays and text
extents.
