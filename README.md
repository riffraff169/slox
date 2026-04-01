clox implementation from https://craftinginterpreters.com

Several features have been added:
#### `has_method()`
See if a class has a method

#### `operator overloading`

`__add__`, `__sub__`, `__mul__`, `__div__` so that you can use those ops on classes like:
 
```
class A { 
 init() { this.a = 1; }
  __add__(a) { this.a = this.a + a.a; return this; }
  display() { return "A(${this.a})"; }
}

var a1 = A();
print a1;
var a2 = A();
a2.a = 4;
print a2;
var a3 = a1 + a2;
print a3;
```

#### Lambdas (anonymous functions)
Demonstrated later

#### Arrays
```
var array = [1, 2, 3, 4, 5];
var a2 = [1; 25]; // fill with 25 1's
var a3 = [1, "fred", [3, 4]]; // mixed types
```

#### Maps
```
var map = {"fred": "father", "barney": "friend" };
```

#### Array ops
push, pop, len, map, dup, isEmpty, select, reduce, join, each, find, slice, sort, reverse, flatten
```
array.select(fun(w) { return w >= 3;} );
```

#### Map ops
keys, values, has, remove, len

#### String ops
trim, contains, toUpper, toLower

#### Math ops
sqrt, abs, floor, ceil, random, pi, exp, hex, oct, bin, `bit_test`, parse, `from_hex`, `from_bin`, round, `to_number`

#### File ops
load, save, exists, list (directory), open, read, readline, write, close, seek, tell, stderr, flush

#### Regex ops
test, exec

#### GI Module
GTK4 through gi module:
```
import "gi";
var Gdk = gi.load("Gdk");
var Gtk = gi.load("Gtk");
Gtk.init();

var win = Gtk.Window();
win.title = "Lox Layout";
win.default_width = 400;
win.default_height = 300;

var box = Gtk.Box();
box.orientation = Gtk.Orientation.vertical;

var btn = Gtk.Button();
btn.label = "Click Me";

gi.connect(btn, "clicked", fun(sender) {
  print "--- Signal Debug ---";
  print sendre;

  var currentLabel = sender.label;
  print "Old Label: " + currentLabel;

  sender.label = "It Works!";
});

gi.connect(win, "close-request", fun(sender) {
  print "Quitting!";
  System.exit(1);
});

box.append(btn);
win.child = box;

print "Window Title is: " + win.title;
win.present();

var controller = Gtk.EventControllerKey();

win.add_controller(controller);

gi.connect(controller, "key-pressed", fun(ctrl, keyval, keycode, state) {
  if ((state & Gdk.ModifierType.control_mask) != 0 and keyval == Gdk.KEY_q) {
    print "Control-Q pressed! Quitting...";
    gi.quit();
    return false;
  }
  print "Key pressed: ", keyvarl;
  return true;
});

gi.loop();
```

