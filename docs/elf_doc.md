# Classes

## Class: ELFParser
Parse elf header

**Required Arguments:**
  - `String: bytes`

### Methods

#### `ELFParser.readU16LE(offset)`
Read 16 bit unsigned integer (little endian)

**Required Arguments:**
  - `Integer: offset`

#### `ELFParser.readU32LE(offset)`
Read 32 bit unsigned integer le

**Required Arguments:**
  - `Integer: offset`

#### `ELFParser.readU64LE(offset)`
Read 64 bit unsigned char

**Required Arguments:**
  - `Integer: offset`

#### `ELFParser.readHex64LE(offset)`
Read 64 bits, return as hex string.

**Required Arguments:**
  - `Integer: offset`

**Returns:**
  - `String`

#### `ELFParser.readString(offset)`
Convert raw byte offset to zero-terminated ascii string

**Required Arguments:**
  - `Integer: offset`

**Returns:**
  - `String`

#### `ELFParser.formatU64(val64)`
Helper to format [lo, hi] 64-bit values for display

**Required Arguments:**
  - `Array: val64`

**Returns:**
  - `String`

#### `ELFParser.toOffset32(val64)`
Returns the lo byte of a 64 bit value array

**Required Arguments:**
  - `Array: val64`

**Returns:**
  - `Integer`

#### `ELFParser.parse()`
Parse elf header

#### `ELFParser.parseProgramHeaders(phoff, phnum, phentsize, width)`
Print program headers

**Required Arguments:**
  - `Integer: phoff`
  - `Integer: phnum`
  - `Integer: phentsize`
  - `Integer: width`

#### `ELFParser.parseSectionHeaders(shoff, shnum, shentsize, shstrndx, width)`
Print section headers

**Required Arguments:**
  - `Integer: shoff`
  - `Integer: shnum`
  - `Integer: shentsize`
  - `Integer: shstrndx`
  - `Integer: width`

#### `ELFParser.getSegmentType(type)`
Return the segment type

**Required Arguments:**
  - `Integer: type`

**Returns:**
  - `String`

#### `ELFParser.getSegmentFlags(flags)`
Return the segment flags

**Required Arguments:**
  - `Integer: flags`

**Returns:**
  - `String`

#### `ELFParser.getSectionType(type)`
Returns the segment type as a string

**Required Arguments:**
  - `Integer: type`

**Returns:**
  - `String`

#### `ELFParser.getSectionFlags(flags64)`
Get the section flags

**Required Arguments:**
  - `Array: flags64`

**Returns:**
  - `String`

#### `ELFParser.getOSABI(abi)`
Get the OS ABI

**Required Arguments:**
  - `Integer: abi`

**Returns:**
  - `String`

#### `ELFParser.getType(typeVal)`
Get the type of file

**Required Arguments:**
  - `Integer: typeVal`

**Returns:**
  - `String`

#### `ELFParser.getMachine(mach)`
Get the machine type

**Required Arguments:**
  - `Integer: mach`

**Returns:**
  - `String`

---
