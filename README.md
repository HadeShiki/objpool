# objpool

A fixed-size object pool with an intrusive freelist. Header-only, C++17, no deps.

```
[free] --alloc()--> [in use] --free()--> [free]
   |                   |                    |
holds next*         holds a T          holds next* again
```

## What it does

- O(1) alloc and free — two pointer writes per op
- Zero per-object overhead — the freelist link lives inside the free slot
- No locks, no coalescing, no bookkeeping
- Fixed capacity known at compile time

## What it doesn't do

- It is **not** thread-safe. One pool per thread.
- It does **not** grow. Capacity is fixed.
- It does **not** construct or destroy objects. It manages storage;
  you place objects with `construct()` or placement new.

## API

| Method | Description |
|--------|-------------|
| `alloc()` | Return raw storage for one T, or `nullptr` if full |
| `free(void*)` | Return storage to the pool (does NOT run dtor) |
| `construct(Args...)` | `alloc()` + placement-new; returns `T*` or `nullptr` |
| `destroy(T*)` | Run dtor + `free()` |
| `capacity()` | `N` |
| `in_use()` | slots currently allocated |
| `available()` | `N - in_use()` |
| `empty()` / `full()` | predicates |

## Tradeoffs

- Memory is not returned to the OS until the pool is destroyed.
  You trade RSS for speed.
- Fixed capacity means you must size the pool for peak usage.
  Over-provision if unsure.
- No thread safety. For thread-local pooling, see
  [fastalloc](https://github.com/HadeShiki/fastalloc).

## Usage

```cpp
#include "objpool.hpp"

objpool::pool<particle, 256> pp;

auto* p = pp.construct(1.0f, 2.0f, /*id=*/1);
// ...
pp.destroy(p);
```

## Requirements

- C++17 (g++ 7+, clang++ 5+, MSVC 2017 15.7+)
- GNU make

## Build

```sh
make          # build demo + tests
make test     # run tests
make run      # run demo
make clean    # remove build/
```

## License

MIT. See [LICENSE](LICENSE) for details.