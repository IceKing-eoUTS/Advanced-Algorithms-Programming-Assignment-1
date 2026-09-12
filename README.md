# Resizable Array With Worst-Case O(1) Append

## Project Purpose

This project implements and studies a vector-like resizable array that supports worst-case O(1) `push_back` under a documented unit-cost computational model. It is compared with a baseline wrapper around ordinary `std::vector` growth, whose `push_back` is amortized O(1) but can take O(n) on a resize.

The goal is educational: show how deamortized resizing can spread reallocation work across many insertions while preserving element order, O(1) indexed access, and O(n) total storage.

## Custom Data Structure

`ResizableArray<T>` is implemented in `include/resizable_array.hpp`. It supports default construction, destruction, copy/move construction, copy/move assignment, `push_back`, `emplace_back`, `operator[]`, bounds-checked `at`, `front`, `back`, `size`, `empty`, and `clear`.

The container uses raw, uninitialized storage. It constructs only live elements and destroys only live elements. It does not wrap `std::vector` or `std::deque`.

`BaselineArray<T>` is implemented in `include/baseline_array.hpp`. It exposes the same operations used by the demos, tests, and benchmark, but stores elements in a normal `std::vector<T>` without calling `reserve()` before insertion.

## Complexity

These bounds use the theoretical model described below.

| Operation | `ResizableArray<T>` | `BaselineArray<T>` / `std::vector<T>` |
| --- | --- | --- |
| `push_back` / `emplace_back` | worst-case O(1) | amortized O(1), worst-case O(n) on reallocation |
| `operator[]` | worst-case O(1) | worst-case O(1) |
| `at` | worst-case O(1) | worst-case O(1) |
| `front` / `back` | worst-case O(1) | worst-case O(1) |
| `size` / `empty` | O(1) | O(1) |
| `clear` | O(n) | O(n) |
| Total storage | O(n) | O(n) |
| Contiguous storage | not guaranteed during migration | guaranteed by `std::vector` |

## Incremental Migration

When the active buffer reaches half capacity, `ResizableArray<T>` allocates a new buffer with double capacity and begins migration before the old buffer is full. Each following insertion does two constant-sized jobs:

- construct the newly inserted element directly in the new buffer at its final logical index
- migrate at most one existing element from the old buffer to the new buffer

During migration, the logical array has three regions:

| Index range | Physical location |
| --- | --- |
| `index < migrated` | already moved/copied into the new buffer |
| `migrated <= index < migration_start_size` | still in the old buffer |
| `index >= migration_start_size` | newly inserted in the new buffer |

The invariants are:

- element order is always the logical insertion order
- an element is live in exactly one buffer
- each `push_back` migrates only a fixed constant number of old elements
- migration starts early enough that it finishes before the new buffer itself reaches its next migration threshold
- indexed access chooses old or new storage using a constant number of comparisons

Those invariants give O(1) indexed access and worst-case O(1) insertion in the model below.

## Theoretical Assumptions And Trade-Offs

The worst-case O(1) append claim is measured in a unit-cost model where:

- allocating or deallocating one raw buffer is treated as one constant-time operation
- constructing, moving, copying, or destroying one `T` object is treated as one constant-time operation
- arithmetic, comparisons, and pointer indexing are constant time

This is not a wall-clock guarantee. Operating-system allocation latency, allocator internals, cache behavior, paging, CPU frequency changes, and arbitrary user-defined `T` operations can all vary in real time.

The main trade-off is contiguous storage. A standard `std::vector` can expose a single contiguous range. `ResizableArray<T>` may be split across an old and new buffer while migration is active, so it deliberately does not provide `data()`.

For exception safety, destructors are expected not to throw. During migration, the implementation moves an element when `T` is nothrow move-constructible or not copy-constructible; otherwise it copies. Strong logical append safety depends on `T` copy/move construction not modifying the source before throwing.

## Repository Structure

```text
include/resizable_array.hpp   Custom deamortized resizable array
include/baseline_array.hpp    std::vector-backed baseline array
include/input_csv.hpp         Shared CSV loading and validation utilities
src/resizable_array_demo.cpp  Demonstration for the custom array
src/baseline_demo.cpp         Demonstration for the baseline array
src/empirical_study.cpp       Benchmark and empirical-study executable
tests/test_arrays.cpp         Lightweight deterministic and randomized tests
tools/generate_input.cpp      Reproducible random CSV generator
data/random_input.csv         Checked-in generated integer dataset
CMakeLists.txt                C++20 build and test configuration
README.md                     Project documentation
```

## Prerequisites

- CMake 3.16 or newer
- A C++20 compiler, such as GCC, Clang, MSVC, or AppleClang
- A shell capable of running the commands below

AddressSanitizer and UndefinedBehaviorSanitizer builds require compiler support. GCC and Clang use both ASan and UBSan in the provided sanitizer configuration. MSVC uses AddressSanitizer where available.

## Configure And Build

Build the normal Release configuration. Use this build for empirical timing.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

For multi-configuration generators such as Visual Studio, use:

```sh
cmake -S . -B build
cmake --build build --config Release
```

With single-configuration generators, executables are placed directly under `build/`. With Visual Studio or other multi-configuration generators, they are usually under `build/Release/`.

## Demonstrations

Run both demonstration programs after building:

```sh
./build/resizable_array_demo
./build/baseline_demo
```

With a multi-configuration generator, use the configuration directory:

```sh
./build/Release/resizable_array_demo
./build/Release/baseline_demo
```

## Generate Input Data

The generator accepts:

```text
generate_input <output-file> <number-of-values> <seed> <minimum-value> <maximum-value>
```

The checked-in dataset was generated with:

```sh
./build/generate_input data/random_input.csv 4096 20260912 -1000000 1000000
```

It contains an `index,value` header and 4096 reproducibly generated signed integers. The empirical study can use the checked-in file or any newly generated file with the same CSV shape.

## Empirical Study

Run the benchmark in Release mode without sanitizers:

```sh
./build/empirical_study --input data/random_input.csv --output results.csv --repetitions 10
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

If `--sizes` is omitted, the study uses powers of two and values immediately before and after expected resize or migration boundaries, up to `--max-n` or the input length.

Before timing each input size, the study builds both implementations from exactly the same input prefix and checks that they produce identical sizes, element values, and checksums.

## Benchmark Methodology

The empirical study writes machine-readable CSV. It separates throughput, latency, and random-access measurements:

- Throughput: times a complete insertion loop with no clock call inside the loop.
- Per-operation latency: times each individual `push_back` separately and reports median, p95, p99, and maximum latency.
- Random access: builds the array first, then times a fixed-seed sequence of random indexed reads.
- File I/O and CSV output are outside timed regions.
- A checksum sink prevents the compiler from optimizing away the benchmarked work.

CSV columns:

| Column | Meaning |
| --- | --- |
| `structure` | `ResizableArray` or `BaselineArray` |
| `n` | number of input values inserted |
| `trial` | measured repetition number |
| `total_ns` | total insertion-loop time for the throughput experiment |
| `ns_per_push` | `total_ns / n`, or 0 when `n == 0` |
| `median_push_ns` | median single-`push_back` latency |
| `p95_push_ns` | 95th percentile single-`push_back` latency |
| `p99_push_ns` | 99th percentile single-`push_back` latency |
| `max_push_ns` | maximum observed single-`push_back` latency |
| `random_access_ns` | total time for the random-index access experiment |
| `checksum` | checksum of logical array contents |
| `estimated_bytes` | allocated element slots times `sizeof(int)`, excluding allocator metadata |

## Tests

Run the standard tests:

```sh
ctest --test-dir build --output-on-failure
```

Or run the test executable directly:

```sh
./build/test_arrays
```

Create and run a sanitizer-enabled test build:

```sh
cmake -S . -B build-sanitize -DCMAKE_BUILD_TYPE=Debug -DRESIZABLE_ARRAY_ENABLE_SANITIZERS=ON
cmake --build build-sanitize
ctest --test-dir build-sanitize --output-on-failure
```

The sanitizer option is intended for tests. Benchmarks should be run from the Release build without sanitizers.

The test harness covers empty-array behavior, first insertion, insertion before/at/after migration boundaries, many consecutive migrations, order preservation, mutable and const indexed access, `at()` success and exceptions, `front()` and `back()`, clearing and reuse, copy/move construction and assignment, self-assignment, move-only values, counted non-trivial objects, destructor balance, throwing element operations during migration, randomized differential testing against `std::vector`, supplied input loading, malformed and partially invalid CSV input, and size/overflow guard behavior. The randomized test uses a fixed seed and prints it if it fails.

This is comprehensive coverage of identifiable branches, boundary conditions, ownership transitions, migration states, and input errors. It is not a claim of mathematically exhaustive testing.

## Interpreting Results

The theoretical complexity result comes from the migration invariants, not from timing measurements. Empirical timing can illustrate behavior, but it cannot prove worst-case asymptotic complexity.

Expected observations:

- `BaselineArray` may show large individual latency spikes when `std::vector` reallocates and moves all live elements.
- `ResizableArray` spreads migration work across insertions, so resize-related work should be less concentrated in single pushes under the model.
- `ResizableArray` may have higher constant factors because indexed access has an extra branch during migration and two buffers may affect cache locality.
- Throughput and latency may disagree: a structure can have good total throughput but poor tail latency, or vice versa.

## Known Limitations And Platform-Dependent Behavior

- `ResizableArray<T>` is not a full `std::vector` replacement and does not provide `data()`, iterators, erase, insert-at-position, `reserve`, or `pop_back`.
- Storage may be split during migration, so code requiring contiguous storage should use `std::vector`.
- Timing results depend on compiler, optimization level, allocator, operating system, CPU, cache state, power management, and system load.
- Clock granularity and measurement overhead affect the latency experiment, especially for very small `n`.
- `estimated_bytes` counts element storage slots only; allocator bookkeeping and object padding outside `sizeof(T)` are not included.
- Very large inputs can fail due to real memory limits even when the asymptotic storage bound is O(n).
- Sanitizers intentionally change performance and should not be used for benchmark interpretation.
