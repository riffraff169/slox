# Classes

## Class: Event
Event class. Similar to gtk signals.
Optionnal:
Value: value

### Methods

#### `Event.get()`
Get current value.

**Returns:**
  - `Value`

#### `Event.set(newValue)`
Set a new value. Notifies any listeners of the change.

**Required Arguments:**
  - `Value: newValue`

#### `Event.subscribe(callback)`
Subscribe to this event. Returns the callback.

**Required Arguments:**
  - `Value: callback`

**Returns:**
  - `Value: callback`

#### `Event.unsubscribe(callback)`
Remove a callback from the listeners.

**Required Arguments:**
  - `Value: callback`

#### `Event.notify(oldVal, newVal)`
Notify any listeners. Calls the callback (listener) with newVal, oldVal.

**Required Arguments:**
  - `Value: oldVal`
  - `Value: newVal`

#### `Event.on(cb)`
Set callback for this event.

**Required Arguments:**
  - `Value: cb`

#### `Event.off(callback)`
Revmove this callback.

**Required Arguments:**
  - `Value: callback`

#### `Event.emit(data)`
Call the callbacks with data.

**Required Arguments:**
  - `Value: data`

---
