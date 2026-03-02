The "hilarious waste" Bob Nystrom refers to is storing one int for every single byte of bytecode. If you have 50 instructions on line 121, you're storing the number 121 fifty times.

A Run-Length Encoding (RLE) approach is the industry standard here. Instead of a flat array, we store pairs: (line number, count of instructions).
1. The Strategy: Run-Length Encoding

Instead of an array like [1, 1, 1, 2, 2, 3], we store:

    Line: 1, Count: 3

    Line: 2, Count: 2

    Line: 3, Count: 1

The Data Structure

In your Chunk struct, replace int* lines with a dynamic array of a simple LineStart struct:
C

typedef struct {
  int line;
  int count;
} LineStart;

typedef struct {
  // ... other fields ...
  int lineCount;
  int lineCapacity;
  LineStart* lines; 
} Chunk;

2. Updating writeChunk()

When adding an instruction, check if it's still on the same line as the last one. If it is, just increment the count. If it's a new line, add a new entry to the array.
C

void writeChunk(Chunk* chunk, uint8_t byte, int line) {
  // ... (handle bytecode array as usual) ...

  // Compression logic:
  if (chunk->lineCount > 0 && 
      chunk->lines[chunk->lineCount - 1].line == line) {
    // Still on the same line, just increment the "run"
    chunk->lines[chunk->lineCount - 1].count++;
  } else {
    // New line encountered, create a new run
    if (chunk->lineCapacity < chunk->lineCount + 1) {
      // ... (standard array growth logic) ...
    }
    chunk->lines[chunk->lineCount].line = line;
    chunk->lines[chunk->lineCount].count = 1;
    chunk->lineCount++;
  }
}

3. Implementing getLine()

Since the data is compressed, we can't just index it. We have to walk through the "runs" and subtract their counts from our target instruction index until we find the right one.
C

int getLine(Chunk* chunk, int instructionIndex) {
  int offset = 0;
  for (int i = 0; i < chunk->lineCount; i++) {
    offset += chunk->lines[i].count;
    if (instructionIndex < offset) {
      return chunk->lines[i].line;
    }
  }
  return -1; // Should not happen
}

Why this is "Better"
Feature	Original (Flat Array)	RLE Approach
Memory	O(n) where n is instructions	O(l) where l is source lines
Writing	O(1)	O(1)
Lookups	O(1) (Direct access)	O(l) (Linear search)
Optimization Tip

In a real-world VM, you almost never call getLine() during execution—it’s only used for printing stack traces when an error occurs. Therefore, trading a slower lookup (O(l)) for significantly lower memory usage is a massive win. If you find lookups are too slow, you could use Binary Search on the offsets to find the line in O(logl) time.

Would you like to see how to implement the Binary Search version to make the lookups faster?
