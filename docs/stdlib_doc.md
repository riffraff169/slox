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

## Class: FieldIndex
Mixin (include) that adds custom __geti__ and __seti__ implementations.
With this, you can access fields of an instance with [], and set with []=.
Example:
```
class User {
  include FieldIndex;

  init(name, role) {
    this.name = name;
    this.role = role;
  }
}
var u = User("Fred", "Architect");
print u.name;
print u["role"];
u["role"] = "Principal Architect";
print u["role"];
```

---

## Class: Serializable
Add serializing features to a class, with .serialize().
deserialize() is a separate external function.

---

