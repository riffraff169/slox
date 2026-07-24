# Slox Interpreter - Complete Feature & API Documentation

This document provides a comprehensive reference for all native classes, methods, global functions, standard library modules, and language syntax supported by the interpreter.

---

## Table of Contents
1. [Core Global Functions](#core-global-functions)
2. [Object Class](#object-class)
3. [String Class](#string-class)
4. [Array Class](#array-class)
5. [Map Class](#map-class)
6. [Set Class](#set-class)
7. [Math & Number Classes](#math--number-classes)
8. [File Class](#file-class)
9. [Dir Class](#dir-class)
10. [System Class](#system-class)
11. [GC Class](#gc-class)
12. [Regex Class](#regex-class)
13. [Result & Option Classes](#result--option-classes)
14. [Compiler & Language Syntax](#compiler--language-syntax)

---

## Core Global Functions

These global utility functions are available directly everywhere in the environment without requiring class instantiation.

* `clock()`: Returns the current CPU execution time in seconds as a Number float.
* `str(val)`: Converts any value `val` into its string representation.
* `typeof(val)`: Returns a string representing the class name/type of `val` (e.g., `"String"`, `"Number"`, `"Array"`, `"Nil"`).
* `chr(num)`: Takes an integer ASCII code `num` (0-255) and returns a 1-character String.
* `eval(source_code)`: Dynamically compiles and executes a String containing Lox code within the current runtime VM context and returns the result of the last evaluated expression.
* `create_instance(class_name)`: Looks up a global class named `class_name` (String) and creates/returns a new instance of that class.
* `program(filename)`: Dynamically loads and compiles a `.lox` source file at path `filename`, instantiating and returning a dynamic Class object derived from the file name.
* `require(path)`: Imports and executes a script at `path` (String). Caches and returns the exported result stored in `vm.requires` so subsequent calls return the cached module value.
* `isnumber(val)`: Returns `true` if `val` is a Number; otherwise `false`.
* `isstring(val)`: Returns `true` if `val` is a String; otherwise `false`.
* `isbool(val)`: Returns `true` if `val` is a Boolean (`true`/`false`); otherwise `false`.
* `isnil(val)`: Returns `true` if `val` is `nil`; otherwise `false`.
* `isclass(val)`: Returns `true` if `val` is a Class object; otherwise `false`.
* `isinstance(val)`: Returns `true` if `val` is an Instance of a class; otherwise `false`.

---

## Object Class

The root class from which all objects inherit. Methods defined on `Object` can be invoked on instances or dual-type values.

* `fields()`: Returns an `Array` containing all string field keys defined on the instance.
* `get_fields()`: Alias for `fields()`. Returns an `Array` of all field keys on the target object.
* `get_field(name)`: Retrieves the value of a field named `name` (String) on an instance or class object. Returns `nil` if not found.
* `set_field(name, value)`: Sets field `name` (String) to `value` on the receiver instance or class and returns `value`.
* `get_methods([recurse])`: Returns an `Array` of method name strings present on the target class. Accepts an optional boolean `recurse` (default `false`) to walk up the superclass inheritance chain.
* `has_method(name)`: Returns `true` if the object's class or any of its superclasses implements method `name` (String).
* `responds_to(name)`: Alias for `has_method(name)`.
* `get_superclass()`: Returns the superclass object of the target receiver, or `nil` if none exists.
* `superclass()`: Alias for `get_superclass()`.
* `to_string()`: Converts the object receiver to its string representation.
* `class()`: Returns the Class object of the receiver.
* `class_name()`: Returns the string name of the receiver's class (e.g., `"Object"`).
* `freeze()`: Marks the class or instance as frozen, preventing subsequent field modifications.
* `is_frozen()`: Returns `true` if the class or instance is frozen; otherwise `false`.
* `isnumber()`: Receiver instance check returning `true` if receiver is a Number.
* `isstring()`: Receiver instance check returning `true` if receiver is a String.
* `isbool()`: Receiver instance check returning `true` if receiver is a Boolean.
* `isnil()`: Receiver instance check returning `true` if receiver is `nil`.
* `isclass()`: Receiver instance check returning `true` if receiver is a Class.
* `isinstance()`: Receiver instance check returning `true` if receiver is a Class Instance.

---

## String Class

Provides built-in methods for text manipulation, formatting, and conversion.

* `trim()`: Strips leading and trailing whitespace characters from the string and returns a new String.
* `contains(substring)`: Returns `true` if `substring` (String) is contained within the string; otherwise `false`.
* `find(substring)`: Returns the integer 0-based index of the first occurrence of `substring`, or `nil` if not found.
* `to_upper()`: Returns a new String with all ASCII characters converted to uppercase.
* `to_lower()`: Returns a new String with all ASCII characters converted to lowercase.
* `len()`: Returns the character length of the string as a Number.
* `length()`: Alias for `len()`.
* `split(delimiter_or_size)`: Splits the string. If passed a String delimiter, returns an `Array` of split segments. If passed a Number chunk size, splits the string into character chunks of that length.
* `slice(start, [end])`: Extracts a substring from index `start` up to optional index `end`. Supports negative indices for counting from the end of the string.
* `to_array()`: Converts the string into an `Array` of numeric byte/ASCII values (0-255).
* `to_number()`: Parses string content into a Number float, or returns `0` if invalid.
* `tokens()`: Splits the string by whitespace into an `Array` of non-empty token strings.
* `format(...)`: Formats the string template using C-style specifiers (`%s` for string, `%d` for integer, `%f` for float, `%b` for boolean, `%%` for literal percent) with argument values provided.

---

## Array Class

Sequential, dynamically resizing ordered collections.

### Basic Usage
```lox
// Creating an array
var numbers = Array();
// or
var numbers = [];
// Initialize
var numbers = [1,2,3,4,5];
// Pre-allocate array with all 0's of size 25
var numbers = [0; 25];

// Adding elements
numbers.push(10);
numbers.push(20);
numbers.push(30);

// Accessing & modifying elements
print numbers[0];      // 10
numbers[1] = 25;
print numbers[1];      // 25

// Array properties & operations
print numbers.len();       // 3
print numbers.contains(25);// true

// Removing elements
var last = numbers.pop();  // Returns 30
// Destructure
var a,b = numbers;
// Output: Warning: Destructuring assigment ignored 3 trailing array elements.
// Turn off warnings
System.warn(false);
var a,b = numbers;
// Strict mode, catchable error if not enough vars
System.strict(true);
var a,b = numbers;
// Output: Destructuring error: Unassigned -3 trailing elements.
// Output: [repl:1] in script

// Getting first and rest, like Lisp car/cdr
var a = numbers.first();
var res = numbers.rest();
```

* `Array(...)`: Class constructor creating an array containing all passed argument elements.
* `push(item1, [item2, ...])`: Appends one or more items to the end of the array and returns the modified array.
* `pop()`: Removes and returns the last element from the array, or `nil` if empty.
* `len()`: Returns the number of elements in the array.
* `length()`: Alias for `len()`.
* `map(callback)`: Invokes `callback` closure on each element and returns a new `Array` containing the results.
* `dup()`: Creates and returns a shallow copy duplicate of the array.
* `is_empty()`: Returns `true` if the array length is 0; otherwise `false`.
* `filter(callback)`: Filters array elements using `callback` closure, returning a new `Array` containing only elements for which `callback` returned truthy.
* `reduce([initial], callback)`: Accumulates values by calling `callback(acc, item)`. Accepts optional `initial` value; defaults to first array element if omitted.
* `join(separator)`: Joins all array elements into a single String, inserting string `separator` between elements.
* `each(callback)`: Iterates over each array element, invoking `callback(element)`.
* `find(callback)`: Returns the first array element for which `callback(element)` evaluates to truthy, or `nil` if none match.
* `has(value)`: Returns `true` if `value` exists in the array (tested via equality); otherwise `false`.
* `slice([start], [end])`: Returns a new `Array` containing elements from index `start` to optional `end`. Supports negative indexing.
* `sort([comparator])`: Sorts array elements in-place. Accepts optional 2-argument `comparator(a, b)` callback returning a number or boolean for custom ordering.
* `sort_slice(start, end, [comparator])`: Sorts a slice of the array between `start` and `end` in-place.
* `reverse()`: Reverses the order of elements in the array in-place and returns the array.
* `flatten()`: Recursively flattens nested arrays into a single-dimensional `Array`.
* `to_string()`: Converts an array of byte numbers (0-255) directly into an ASCII/binary String.
* `first()`: Returns the first element of the array, or `nil` if empty.
* `rest()`: Returns a new `Array` containing all elements except the first.
* `split()`: Deconstructs array into a 2-element array containing `[first_element, rest_array]`.

A callback can be either a function, or an anonymous/lambda function, as in:
```lox
array.find(fun(a) { return a > 5; });
```
---

## Map Class

Key-value associative hash maps supporting arbitrary value keys.

### Basic Usage
```lox
// Creating a map
var user = Map();
// or
var user = {};
// Can initialize directly
var user = {"name": "Alice", "role": "Admin", "active": true};
// Or with Map(), must be even number of arguments
var user = Map("name", "Alice", "role", "Admin", "active", true);

// Inserting and updating key-value pairs
user["name"] = "Alice";
user["role"] = "Admin";
user["active"] = true;

// Accessing values
print user["name"];    // "Alice"

// Checking keys and size
if (user.has("role")) {
    print user["role"];// "Admin"
}

print user.len();          // 3

// Inspecting keys & values
var keys = user.keys();    // Returns Array of keys
var values = user.values();// Returns Array of values

// Removing entries remove/delete
user.remove("active");
```

* `Map(k1, v1, k2, v2, ...)`: Constructor instantiating a new map from key-value argument pairs (requires an even number of arguments).
* `keys()`: Returns an `Array` containing all keys present in the map.
* `values()`: Returns an `Array` containing all values stored in the map.
* `has(key)`: Returns `true` if `key` exists within the map; otherwise `false`.
* `remove(k1, [k2, ...])`: Removes specified keys from the map and returns a new `Map` containing the deleted key-value pairs. Alias delete.
* `len()`: Returns the total count of key-value pairs stored in the map.
* `each(callback)`: Iterates through all map entries, passing `(key, value)` to the provided `callback` closure.

---

## Set Class

Unordered collection of unique values or multi-set count values.

### Basic Usage
```lox
// Creating a set
var tags = Set();
// Create a multiset, items can be added multiple times and it keeps a count
var counts = Set();
counts.set_multiset();

// Adding unique elements
tags.add("lox");
tags.add("vm");
tags.add("lox"); // Duplicate, ignored

print tags.len();          // 2

// Checking for membership
if (tags.has("vm")) {
    print "Tag found!";
}

// Removing elements
tags.remove("vm");
print tags.has("vm");      // false

// Convert to array for processing
var tagList = tags.keys();

// Convert to map for histogram
counts["apple"] = 3; // Set count directly
print counts;
counts.add("apple"); // Increment, or counts["apple"] = counts["apple"] + 1
print counts;
counts["apple"] = 0; // Remove
```

* `Set(item1, item2, ...)`: Constructor creating a new set containing all provided argument values.
* `add(value)`: Adds `value` to the set. If set is converted to multiset mode, increments element count.
* `remove(value)`: Removes `value` from the set and returns `true` if element was present.
* `has(value)`: Returns `true` if `value` exists in the set; otherwise `false`.
* `len()`: Returns the total number of unique elements in the set.
* `length()`: Alias for `len()`.
* `keys()`: Returns an `Array` of all elements currently in the set.
* `set_multiset()`: Enables multiset behavior allowing duplicate counts for items (must be called on an empty set).
* `to_map()`: Converts the set into a `Map` mapping elements to boolean `true` (or numeric counts if multiset).

---

## Math & Number Classes

Provides mathematical operations, trigonometric calculations, random number generation, and number formatting.

### Math Constants
* `Math.PI`: `3.1415926535897932`
* `Math.E`: `2.7182818284590452`

### Dual Methods (Callable as `Math.method(n)` or `n.method()`)
* `sqrt([n])`: Calculates the square root of number `n`.
* `abs([n])`: Returns the absolute value of number `n`.
* `floor([n])`: Rounds `n` down to the nearest integer.
* `ceil([n])`: Rounds `n` up to the nearest integer.
* `exp([n])`: Returns e raised to the power of `n` ($e^n$).
* `hex([precision], [prefix])`: Formats number as a hexadecimal string (e.g. `"0x1a"`).
* `oct([precision])`: Formats number as an octal string (e.g. `"0755"`).
* `bin([min_bits])`: Formats number as a binary representation string (e.g. `"0b1010"`).
* `sin([n])`: Returns sine of angle `n` (in radians).
* `cos([n])`: Returns cosine of angle `n` (in radians).
* `tan([n])`: Returns tangent of angle `n` (in radians).
* `acos([n])`: Returns arc cosine of `n` in radians.
* `atan2(y, x)`: Returns 2-argument arc tangent of $y / x$ in radians.
* `to_int([n])`: Truncates floating-point number `n` to integer component.
* `to_fixed([decimals])`: Formats number `n` to string with specified decimal places (0-20).

### Math Static Methods
* `Math.random()`: Generates a pseudo-random floating-point Number in range `[0.0, 1.0)`.
* `Math.bit_test(num, bit)`: Returns `true` if bit index `bit` (0-63) is set in integer `num`.
* `Math.min(a, b)`: Returns the smaller of two numbers `a` and `b`.
* `Math.max(a, b)`: Returns the larger of two numbers `a` and `b`.
* `Math.parse(string)`: Auto-detects and parses string (decimal, `0x` hex, `0b` bin) into a Number.
* `Math.from_hex(string)`: Parses hexadecimal string into a Number.
* `Math.from_bin(string)`: Parses binary string into a Number.
* `Math.round([n])`: Rounds `n` to nearest integer.
* `Math.to_number(val)`: Converts string or primitive `val` into a Number.

### Number Instance Methods
* `to_string([precision])`: Converts number to string representation with optional decimal `precision`.

---

## File Class

Provides file system I/O capabilities, file inspection, and POSIX file system management.

### File Static Methods
* `File.load(path)`: Reads and returns the entire file contents at `path` (String) as a single String.
* `File.save(path, content)`: Overwrites or creates file at `path` (String) with `content` (String). Returns `Result`.
* `File.exists(path)`: Returns `true` if file or path exists; otherwise `false`.
* `File.open(path, [mode])`: Opens a file handle at `path` with mode string (e.g. `"r"`, `"w"`, `"a"`, `"rb"`, or system streams `"STDOUT"`, `"STDERR"`, `"STDIN"`). Returns `Result` containing wrapped `File` instance.
* `File.chmod(path, mode)`: Changes POSIX permissions of file at `path` to bitmask octal/integer `mode`.
* `File.chown(path, uid, gid)`: Changes owner user ID (`uid`) and group ID (`gid`) of file at `path`.

### File Instance Methods
* `read([length])`: Reads up to `length` bytes (or entire remaining content if omitted) from open file stream. Returns `Result` with read string.
* `readline()`: Reads a single line (ending in `\n`) from the file stream. Returns `Result` with string.
* `close()`: Flushes and closes underlying file descriptor handle.

---

## Dir Class

Directory stream operations and path handling.

* `Dir.list(path)`: Scans directory at `path` and returns an `Array` containing item names.
* `Dir.entries(path)`: Alias for `Dir.list(path)`.
* `Dir.mkdir(path)`: Creates a single new directory at `path`.
* `Dir.mkdir_p(path)`: Recursively creates directory tree paths at `path` (similar to POSIX `mkdir -p`). Need to `require("mkdir_p.lox")` to use.
* `Dir.rmdir(path)`: Removes an empty directory at `path`.
* `Dir.exists(path)`: Returns `true` if directory exists at `path`; otherwise `false`.
* `Dir.cwd()`: Returns string path of the current working directory.

---

## System Class

Operating system interop, process management, and environment variables.

* `System.exec(command)`: Executes external subshell `command` (String) and returns status/output.
* `System.env(var_name)`: Returns value of environment variable `var_name` (String), or `nil` if unset.
* `System.set_env(var_name, value)`: Sets environment variable `var_name` to `value` string.
* `System.exit(code)`: Instantly terminates script execution with exit code integer `code`.
* `System.args()`: Returns an `Array` of string command-line arguments passed to running script.
* `System.time()`: Returns current Unix timestamp in seconds or milliseconds.
* `System.ARGS`: Array of args available to script (vm options removed).
* `System.ENV`: Map of environment variables.

---

## GC Class

Garbage collector inspection and manual control interface.

* `GC.gc()`: Explicitly triggers a full garbage collection cycle in the VM.
* `GC.stats()`: Returns current gc stats.
* `GC.heap_growth_factor()`: Sets heap growth factor.
* `GC.get_growth_factor()`: Gets heap growth factor.
* `GC.init_threshold()`: Sets threshold.
* `GC.get_threshold()`: Gets threshold.
* `GC.bump_size()`: Sets bump size.
* `GC.get_bumpsize()`: Gets bump size.
* `GC.stress_mode()`: Sets stress mode.
* `GC.get_stress_mode()`: Gets stress mode.
* `GC.type()`: Sets gc type, linear or multiplier.
* `GC.get_gctype()`: Gets gc type, linear or multiplier.

---

## Regex Class

Regular expression engine wrapping PCRE2.

* `Regex(pattern)`: Constructor compiling regular expression string `pattern`.
* `test(subject)`: Tests if regex matches `subject` (String); returns `true` or `false`.
* `match(subject)`: Performs regex match against `subject` (String); returns `Array` of matched groups or empty Array.
* `get_pattern()`: Returns the original raw pattern string used to construct the regex.

---

## Result & Option Classes

Pattern-matching error handling and optional value structures.

### Result Class
* `Result(is_ok, [payload])`: Constructor creating a Result instance (`ok = true/false`, `val`/`err` set to `payload`).
* `unwrap()`: Extracts inner `val` if Result is Ok; triggers runtime panic if Result is Err.
* `unwrap_or(default_value)`: Extracts inner `val` if Result is Ok; otherwise returns `default_value`.
* *Fields*: `.ok` (Boolean), `.val` (Value on success), `.err` (Error message/value on failure).

### Option Class
* `Option(is_some, [value])`: Constructor creating Option instance (`is_some = true/false`).
* `unwrap()`: Returns inner value if Option contains `Some`; triggers runtime panic if `None`.
* `unwrap_or(default_value)`: Returns inner value if Option contains `Some`; otherwise returns `default_value`.
* *Fields*: `.is_some` (Boolean), `.val` (Value if Some), `.err` (Error message/value on failure).

---

## Compiler & Language Syntax

The interpreter compiler parses Lox source files into bytecode execution units. Supported language syntax constructs include:

### Variables & Storage
* Local and global variable declarations: `var name = expression;`
* Re-assignment: `name = new_value;`
* Constants / Class constants: `Math.PI`

### Binary Operators

| Operator | Category / Description               |
| :---     | :---                                 |
| **`+`**  | Addition                             |
| **`-`**  | Subtraction                          |
| **`*`**  | Multiplication                       |
| **`/`**  | Division                             |
| **`%`**  | Modulo                               |
| **`**`** | Exponentiation *(Right-associative)* |
| **`==`** | Equality comparison                  |
| **`!=`** | Inequality comparison                |
| **`<`**  | Less than                            |
| **`<=`** | Less than or equal to                |
| **`>`**  | Greater than                         |
| **`>=`** | Greater than or equal to             |
| **`&`**  | Bitwise AND                          |
| **`\|`** | Bitwise OR                           |
| **`^`**  | Bitwise XOR                          |
| **`<<`** | Bitwise Left Shift                   |
| **`>>`** | Bitwise Right Shift                  |

---

### Unary Operators

| Operator | Category / Description |
| :---     | :---                   |
| **`-`**  | Arithmetic Negation    |
| **`!`**  | Logical NOT            |
| **`~`**  | Bitwise NOT            |

---

### Key Parser Highlights

* **Right-Associativity (`**`)**: Parsed with `rule->precedence` so recursive calls consume right-hand expressions at the same precedence level, correctly evaluating `2 ** 3 ** 2` as `2 ** (3 ** 2)`.
* **Logical Inversion Tricks**:
  * `>=` is compiled as `!(a < b)` (`OP_LESS` + `OP_NOT`).
  * `<=` is compiled as `!(a > b)` (`OP_GREATER` + `OP_NOT`).
  * `!=` is compiled as `!(a == b)` (`OP_EQUAL` + `OP_NOT`).

### Control Flow
* **If-Else Conditional**:
  ```lox
  if (condition) {
      // then branch
  } else {
      // else branch
  }
  ```
* **Switch Statement**:
  Switch does not have fallthrough, all cases break automatically.
  ```lox
  switch (expression) {
      case value1:
          // statement
      case value2:
          // statement
      default:
          // default statement
  }
  ```
* **While Loop**:
  ```lox
  while (condition) {
      // loop body
  }
  ```
* **For Loop**:
  ```lox
  for (var i = 0; i < 10; i = i + 1) {
      // loop body
  }
  ```
* Loop Control: `break;` and `continue;` statements within loop blocks.

* **try/catch**:
```lox
fun divide(a, b) {
    if (b == 0) {
        throw "Division by zero is not allowed";
    }
    return a / b;
}

fun processCalculation(x, y) {
    print "Starting calculation...";

    try {
        var result = divide(x, y);
        print "Result: " + result;
    } catch (err) {
        print "Caught error: " + err;
    } finally {
        print "Calculation complete.\n";
    }
}

// Successful execution
processCalculation(10, 2);

// Triggers catch block
processCalculation(10, 0);
```

### Functions & Closures
* Named function declaration:
  ```lox
  fun add(a, b) {
      return a + b;
  }
  ```
* Anonymous closures & lambdas: `var f = fun(x) { return x * 2; };`
* First-class function passing, lexically scoped variable capturing.

### Classes & Object-Oriented Programming
* Class declaration and inheritance:
  ```lox
  class Animal {
      speak() {
          print "Generic sound";
      }
  }

  class Dog < Animal {
      init(name) {
          this.name = name;
      }

      speak() {
          super.speak();
          print this.name + " barks!";
      }
  }
  ```
* Instance instantiation (`Dog("Buddy")`) and initializer (`init`).
* Property/Field access (`obj.field = val`) and method invocation (`obj.method()`).
* Class opening and dynamic native method binding.
* Class re-opening to add new methods or redefine existing methods.

### Modules & Libraries
* Standard imports via `require("lib.lox")` and `program("class.lox")`.
* Inline evaluation using `eval("lox code string")`.
* Standard library stdlib.lox
  * Iterators for Array and Map, Serialization and Deserialization, among others.
  * Argument parser library similar to Ruby OptionParser.
  * Pretty printer pp(obj) or print PP.pp(obj).

