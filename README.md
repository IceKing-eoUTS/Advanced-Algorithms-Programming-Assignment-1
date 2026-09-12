# Resizable Array With Worst-Case O(1) Append

This repository implements two small array containers:

- `ResizableArray<T>`: a vector-like educational container using deamortised incremental migration.
- `BaselineArray<T>`: the same exercised interface backed by an ordinary `std::vector<T>`.

The custom array supports default construction, destruction, copy/move construction, copy/move assignment, `push_back`, `emplace_back`, `operator[]`, checked `at`, `front`, `back`, `size`, `empty`, and `clear`.

## Design

`ResizableArray<T>` intentionally does not promise contiguous storage at all times. When the current buffer reaches half capacity, it allocates a replacement buffer with double the capacity and starts a migration. Each later insertion constructs the new element directly in the replacement buffer at its final logical index, then moves or copies one existing element from the old buffer into the replacement buffer.

During migration, the logical array is split into three O(1)-decidable regions:

- indices `< migrated`: already in the replacement buffer
- indices `>= migrated && < migration_start_size`: still in the old buffer
- indices `>= migration_start_size`: newly inserted elements in the replacement buffer

That means indexed access performs only a constant number of integer comparisons and one pointer access. Since migration starts at half capacity and one old element migrates per insertion, migration finishes by the time the replacement buffer reaches half capacity. The next growth can then begin before the replacement buffer is full.

## Complexity Model

The advertised `push_back` bound is worst-case O(1) under a unit-cost RAM-style model where:

- allocating or deallocating one raw buffer is treated as one constant-time operation
- constructing, moving, copying, or destroying one `T` object is treated as one constant-time operation
- arithmetic, comparisons, and pointer indexing are constant time

This is the standard model needed for the assignment-style deamortisation result. It is not a wall-clock guarantee about operating-system allocation latency, cache behavior, or arbitrary user-defined `T` operations.

Under that model:

- `push_back`: worst-case O(1)
- `operator[]` and `at`: worst-case O(1)
- `size` and `empty`: O(1)
- total allocated storage: O(n)

## Trade-Off Against `std::vector`

A normal `std::vector` provides contiguous storage but occasionally reallocates and moves every live element in one insertion, giving amortised O(1) append rather than worst-case O(1) append. This project chooses the opposite trade-off: append work is spread over later insertions, preserving order and O(1) indexing, but storage may be split across old and new buffers while a migration is active.

For that reason, `ResizableArray<T>` does not expose `data()`. Providing a raw contiguous pointer during migration would conflict with the chosen worst-case append design.

## Type and Exception-Safety Notes

The container uses raw uninitialised storage and constructs only live elements. It destroys exactly those live elements during migration, clearing, assignment, and destruction.

For migration, `ResizableArray<T>` moves an element when `T` is nothrow move-constructible or not copy-constructible; otherwise it copies the element. The container remains valid if construction during insertion or migration throws. Strong logical exception safety for appends depends on `T` copy/move construction not changing source values before throwing. As with standard containers, `T` destructors are expected not to throw.

## Repository Layout

- `include/resizable_array.hpp`: custom deamortised array implementation
- `include/baseline_array.hpp`: `std::vector`-backed baseline with the exercised interface
- `src/resizable_array_demo.cpp`: small demonstration of the custom array
- `src/baseline_demo.cpp`: small demonstration of the baseline
- `src/empirical_study.cpp`: timing and checksum study for both containers
- `tests/test_arrays.cpp`: correctness tests
- `tools/generate_input.cpp`: deterministic random input generator
- `data/random_input.csv`: checked-in generated integer input
- `CMakeLists.txt`: C++20 build configuration

## Build and Test

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

Run the demos:

```sh
./build/resizable_array_demo
./build/baseline_demo
```

On Windows with the default Visual Studio generator, the executables may be under `build/Debug` or `build/Release`.

## Generate Input

The checked-in dataset is generated reproducibly with:

```sh
./build/generate_input data/random_input.csv 4096 20260912
```

The CSV has an `index,value` header and one generated integer per row.

## Empirical Study

Run:

```sh
./build/empirical_study data/random_input.csv
```

The program prints:

```text
container,n,total_push_ns,max_single_push_ns,indexed_scan_ns,checksum
```

The checksum verifies that indexed access sees the same logical sequence. Timing results are empirical and machine-dependent; they are included to compare behavior, not to prove the asymptotic model. The baseline intentionally uses ordinary `std::vector` growth without calling `reserve()`, so vector reallocation events remain visible in the measurements.
