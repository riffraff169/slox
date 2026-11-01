# Classes

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
