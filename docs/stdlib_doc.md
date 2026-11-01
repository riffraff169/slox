# Classes

## Class: System
Part of main core libraries

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

---

## Class: Error
Error class. Used for throwing errors from so that `catch` can deal with it.

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
### Methods

#### `Struct.packNative()`
Assume native ordering for pack

#### `Struct.unpackNative()`
Assume native ordering for unpack

#### `Struct.packNet()`
Assume network ordering for pack

#### `Struct.unpackNet()`
Assume network ordering for pack

---

## Class: Array
### Methods

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

---

## Class: Set
### Methods

#### `Set.multiset([Array])`
Create a new Set as a multiset. Use as an initializer like: var s = Set.multiset().

**Optional Arguments:**
  - `Array`

**Returns:**
  - `Set`

---

# Global Functions

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
