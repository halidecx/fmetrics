# Overview

`fmetrics` is a library that implements fast image/video fidelity metrics in C.
It includes a CLI tool, `fmetrics`.

## Code Style

- Never add comments to your code.
- Never remove existing comments.
- Single line `if` statements in C wherever possible.
- Fewer lines of code is always better.
- Maximum line length of 80 characters for C; `zig fmt <file>` for Zig.
- **Never** delete the `zig-out` directory.

## Compilation

```sh
zig build --release=fast
```

Emits:

```
zig-out
├── bin
│   ├── fmetrics
├── include
│   └── fmetrics.h
└── lib
    └── libfmetrics.a
```

## Testing

Check score:

```sh
./zig-out/bin/fmetrics <metric> zig-out/bin/ref.pam zig-out/bin/dst.pam
```

Always run a preliminary score check before making changes.

Check performance:

```sh
hyperfine -N --warmup 2 "./zig-out/bin/fmetrics <metric> zig-out/bin/ref.pam zig-out/bin/dst.pam"
```
