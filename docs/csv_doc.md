# Classes

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
