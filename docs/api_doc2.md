# Classes

## Class: Signal
### Methods

#### `Signal.trap(signum, callback)`
**Required Arguments:**
  - `Number: signum`
  - `Callback: callback`

**Returns:**
  - `Nil`

---

## Class: Timer
Timer class base

### Methods

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

