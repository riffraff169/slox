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
