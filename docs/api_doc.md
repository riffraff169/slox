# Classes

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

## Class: Array
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

## Class: Map
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

## Class: Set
Special case of Map. Values are ignored, can keep a count of number
of times key has been added.

### Methods

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

## Class: Dir
Collection of methods dealing with directories.

### Methods

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

## Class: Process
Class for managing processes.  Includes pipes, fork/exec, system.

### Methods

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

## Class: Process:
### Methods

#### `Process:.pid()`
Get current pid

**Returns:**
  - `Integer`

---

## Class: Struct
Provides two methods, pack and unpack, for creating and splitting data.

### Methods

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

## Class: System
Various system methods.
Also initializes ARGS and ENV, and sets include paths

### Methods

#### `System.time()`
Get the current value of time()

**Returns:**
  - `Integer`

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

## Class: Error
Error class for throwing errors and raising exceptions

**Required Arguments:**
  - `None`

**Optional Arguments:**
  - `message: String`

---

# Global Functions

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

