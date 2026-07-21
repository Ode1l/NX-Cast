# C Safety And Ownership Rules

NX-Cast keeps ownership explicit. These rules apply to new code and to code
touched during maintenance; they are intended to prevent leaks, truncated
protocol data, integer wrap, and partially published parser state.

## Owned Values

- An owning structure must be zero-initialized before its first API call.
- Every owning type provides one clear/dispose function. It must be safe to call
  repeatedly on an initialized object and must restore the zero state.
- Copy and set operations replace the destination only after every allocation
  succeeds. On failure, the destination remains unchanged.
- A self-copy is a successful no-op. If `NULL` means clear, the implementation
  must call the typed clear function rather than overwrite pointers with
  `memset`.
- Ownership transfer must be visible in the function name or header contract.
  Do not return a borrowed pointer through an interface documented as owned.

The player value contracts in `source/player/types.h` are the reference model.

## Length And Allocation

Use `source/util/size.h` before arithmetic that controls allocation, indexing,
or a protocol length:

- `nxcast_size_add()` for combined payload and terminator lengths.
- `nxcast_size_multiply()` for element counts and worst-case escaping.
- `nxcast_size_grow()` for bounded geometric capacity growth.

Validate the complete expression before `malloc`, `realloc`, pointer addition,
or subtraction. A writer must prove `offset <= capacity` before evaluating
`capacity - offset`. Preserve the original pointer until `realloc` succeeds.

## Strings And Truncation

Every `snprintf` result belongs to one of two policies:

- Protocol values, URLs, file paths, identifiers, and HTTP/XML fields reject a
  negative result or a result greater than or equal to the destination size.
- Display-only labels may truncate deliberately when the call site makes that
  policy obvious and the buffer remains NUL-terminated.

Do not repair a truncated transport value with `strncat` or another partial
append. Calculate the complete required length, then write once or fail.

## Parsers And Collections

- Validate external counts and lengths before addition, multiplication, or
  advancing a cursor.
- Keep a partially parsed object private. Increment collection counts or expose
  pointers only after construction completes.
- A failure path frees every member allocated for the unpublished object.
- Output lengths, flags, and pointers should be reset before parsing so callers
  cannot consume stale values after failure.
- Optional metadata may be dropped on overflow; required media URLs and
  protocol fields must reject the containing item.

## Cleanup And Threads

- Prefer one cleanup path for functions owning more than one resource.
- Stop producers and join worker threads before freeing the state they access.
- Do not hold application locks while calling network, filesystem, renderer, or
  logging code that may block or call back.
- Close in dependency order. A subsystem must stop accepting work before its
  queue, socket, renderer, or storage is destroyed.

Thread ownership and shutdown ordering are described in
`docs/threading-design.md`.

## Validation

Run the focused portable gate:

```sh
make test-c-safety
```

Run it under AddressSanitizer and UndefinedBehaviorSanitizer:

```sh
make test-c-safety-sanitize
```

Leak detection is disabled by that target because the installed macOS ASan
runtime does not support LeakSanitizer. Repeated lifecycle tests still exercise
typed clear/copy paths. The strict Switch build remains authoritative for code
that cannot be compiled by the host tests.
