# slox

C implementation of Lox (a bytecode VM derived from clox) with additional native classes. Entry point: `src/main.c`; compiler: `src/compiler.c`; VM: `src/vm.c`; native functions/classes: `src/native.c`; GC: `src/memory.c`.

## Build

- `make -j4` builds `bin/slox` and rebuilds native modules in `modules/` (via `modules/build_modules.sh`, including BearSSL).
- `make bin/slox` builds only the interpreter; use this for quick C changes.
- `make clean` removes `src/*.o`, `src/*.d`, and `bin/`.
- The build depends on the readline and libpcre2-8 development packages (`pkg-config readline libpcre2-8`).

## Run

- `./bin/slox path/to/file.lox`
- Native modules are loaded from `./modules/liblox_*.so`, so run module-using scripts from the repo root.

## Tests

- Run the suite with `bash scripts/run_tests.sh` (the script is `run_tests.sh`, not `runtests.sh`). It requires a built `bin/slox`.
- The runner only executes files containing at least one `// expect:` comment; files without expectations are skipped silently.
- Expected output is the text after each `// expect: ` in file order, compared line-for-line against combined stdout and stderr. VM stack-trace lines matching `[file:line] in ...` are filtered; compile-error diagnostics are not, so list each expected diagnostic as its own `// expect:` line.
- Run one test directly with `./bin/slox tests/arrays/basic.lox`.
- For GC-sensitive changes, prepend `GC.stress_mode(1);` to a copy of the test to force a collection on every allocation.

## Layout and conventions

- `src/version.h` is the single version source. `scripts/bump-version.sh X.Y.Z [release]` updates version files, commits, creates tag `vX.Y.Z`, and pushes. Do not bump versions manually.
- The language extends Lox with raw strings: `r"..."` does not process escapes and ends at the next `"` (used for regex patterns such as `Regex(r"\d+")`).
- Native functions use `Value nameNative(int argCount, Value* args)`; `args[-1]` is the receiver/callee and `args[0..]` are arguments. Report errors with `runtimeError(...)` and return a fallback value.
- Callback-driven natives must stop when a callback throws; follow the `VM_CALLBACK_*` macros at the top of `src/native.c` and preserve `vm.exceptionThrown`/pending-unwind state instead of resetting the stack.
- `src/native.h` is generated from `src/native.c` by `src/makeheaders.sh src/native.c`.
- `tests/`, `examples/`, `lib/` (Lox standard library), and `docs/` are tracked. The API reference is `docs/api_doc.md`; the README link to `docs/api_docs.md` is stale.
- Many scratch `.lox`, `.txt`, `.ppm`, and similar files in the repo root are not part of the source tree; leave them alone.
- `origin` has two push URLs (Gitea and GitHub), so `git push origin` publishes to both.
