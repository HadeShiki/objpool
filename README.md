# objpool

A fixed-size object pool with an intrusive freelist. Header-only, C++17, no deps.

```
+---------+---------+---------+---------+
| slot 0  | slot 1  | slot 2  |  ...    |   N slots, one contiguous block
+---------+---------+---------+---------+
     |
     v
+-----+    +-----+    +-----+
|  #2 |--->|  #0 |--->|  #4 |---> nullptr   free_head
+-----+    +-----+    +-----+
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

## Usage

```cpp
#include "objpool.hpp"

objpool::pool<particle, 256> pp;

auto* p = pp.construct(1.0f, 2.0f, /*id=*/1);
// ...
pp.destroy(p);
```

## Build

```sh
make          # demo
make test     # tests
make clean
```

## License

License See [License](License) for details.