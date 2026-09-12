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

Empirical timing cannot prove this worst-case complexity claim. The proof comes from the algorithm and its invariants: migration starts before the old buffer is full, each insertion does a bounded amount of migration work, and access chooses between the old and new buffer with a bounded number of checks. The benchmark can only illustrate practical behavior such as resize spikes, cache effects, allocator behavior, constant factors, and places where the theory and observed timings agree or diverge.

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
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

For sanitizer-enabled tests on compilers that support AddressSanitizer and UndefinedBehaviorSanitizer:

```sh
cmake -S . -B build-sanitize -DCMAKE_BUILD_TYPE=Debug -DRESIZABLE_ARRAY_ENABLE_SANITIZERS=ON
cmake --build build-sanitize
ctest --test-dir build-sanitize --output-on-failure
```

The empirical-study executable should be built in Release mode without sanitizers. Sanitizers are intentionally configured only for the `test_arrays` target.

Run the demos:

```sh
./build/resizable_array_demo
./build/baseline_demo
```

On Windows with the default Visual Studio generator, the executables may be under `build/Debug` or `build/Release`.

## Generate Input

The checked-in dataset is generated reproducibly with:

```sh
./build/generate_input data/random_input.csv 4096 20260912 -1000000 1000000
```

The generator arguments are:

```text
generate_input <output-file> <number-of-values> <seed> <minimum-value> <maximum-value>
```

The checked-in CSV has an `index,value` header and 4096 signed integers generated from seed `20260912` in the inclusive range `[-1000000, 1000000]`. The empirical study can use this file or any newly generated file with the same CSV shape.

## Empirical Study

Run a full study and write machine-readable CSV:

```sh
./build/empirical_study --input data/random_input.csv --output results.csv --repetitions 5 --warmups 1
```

Useful options:

```text
--input PATH          Input CSV path; positional input path is also accepted
--output PATH         Output CSV path; omit or use - for stdout
--max-n N             Largest prefix size to test
--sizes A,B,C         Exact comma-separated input sizes to test
--repetitions N       Measured repetitions per size
--warmups N           Warm-up repetitions per size
--accesses N          Random-index reads per access trial; default is n
--access-seed N       Seed for the random-index experiment
```

Without `--sizes`, the study automatically includes powers of two and values immediately before and after the expected migration/resize boundaries, up to `--max-n` or the input length.

The output columns are:

```text
structure,n,trial,total_ns,ns_per_push,median_push_ns,p95_push_ns,p99_push_ns,max_push_ns,random_access_ns,checksum,estimated_bytes
```

The throughput experiment times bulk insertion with no per-operation clock call inside the loop. The latency experiment is separate and records one clocked duration per `push_back`, then reports median, p95, p99, and maximum latency. Random-index access is also timed separately after the arrays have already been built. File I/O and CSV output happen outside timed regions.

Before recording timings for each input size, the program checks that `ResizableArray<int>` and `BaselineArray<int>` produce identical sizes, values, and checksums from exactly the same input prefix. The baseline intentionally uses ordinary `std::vector` growth without calling `reserve()`, so vector reallocation events remain visible in the measurements.

## Test Coverage

The standard-library test harness integrated with CTest covers empty arrays, first insertion, insertion before/at/after migration boundaries, many consecutive migrations, order preservation, mutable and const indexed access, `at()` success and exceptions, `front()` and `back()`, clearing and reuse, copy/move construction and assignment, self-assignment, move-only values, counted non-trivial objects, destructor balance, throwing element operations during migration, randomized differential testing against `std::vector`, supplied input loading, malformed and partially invalid CSV input, and size/overflow guard behavior. The randomized test uses a fixed seed and prints it if the test fails.

This is comprehensive coverage of identifiable branches, boundary conditions, ownership transitions, migration states, and input errors; it is not a claim of mathematically exhaustive testing.
