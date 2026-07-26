# padff

Force feedback mixer: game rumble effects to two motor intensities.

## Build and install

```sh
cmake -S . -B build -DBUILD_SHARED_LIBS=ON
cmake --build build
cmake --install build --prefix /usr/local
```

Use it from CMake:

```cmake
find_package(padff REQUIRED)
target_link_libraries(app PRIVATE padff::padff)
```

Or with pkg-config:

```sh
cc app.c $(pkg-config --cflags --libs padff)
```

## Single-file form

`scripts/amalgamate.sh` writes `dist/padff.h`. Copy it into your project and, in exactly
one `.c` file:

```c
#define PFF_IMPLEMENTATION
#include "padff.h"
```

Define `PFF_STATIC` as well to keep every function private to that file.

## Usage

See [`examples/basic.c`](examples/basic.c). The header `include/padff/padff.h` is the
API reference.

Conventions:

- every function that can fail returns `PFF_OK` or an error code; `pff_strerror` describes it
- what `pff_x_create` returns is released only by `pff_x_destroy`, which accepts `NULL`
- functions never take ownership of their arguments; returned pointers belong to the library
- pass your own allocator to `pff_buf_create`, or `NULL` for `malloc`/`free`

## Versioning

Semantic Versioning. Compatible releases bump the minor version; incompatible ones bump the
major version and the soname. See [CHANGELOG.md](CHANGELOG.md).

## License

MIT, see [LICENSE](LICENSE).
