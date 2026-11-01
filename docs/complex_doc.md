# Classes

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
