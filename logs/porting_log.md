# RISC-V Porting Log
**Environment:** `riscv64/ubuntu:24.04` on Docker (Apple Silicon M1)

---

## Repo 1: CloverLeaf_Serial
**URL:** https://github.com/UK-MAC/CloverLeaf_Serial
**Date:** 2026-05-02
**Category:** HPC mini-app (hydrodynamics)
**Language:** C + Fortran

### Attempt 1
**Command:** `make`
**Error:** `make: CC: No such file or directory`
**Cause:** Makefile uses `CC` as a literal binary name, not the CC variable.
**Fix:** `make COMPILER=GNU`
**Status:** Partial — C compiled, Fortran failed

### Attempt 2
**Command:** `make COMPILER=GNU`
**Error:** `/bin/sh: 1: gfortran: not found`
**Fix:** `apt install -y gfortran`

### Attempt 3
**Command:** `apt install -y gfortran && make COMPILER=GNU`
**Status:** SUCCESS — binary `clover_leaf` produced

**Patch applied:** Replaced hardcoded `-march=native` with arch-detect block in Makefile.
See: `patches/cloverleaf-march-riscv.patch`

### Validation
**Command:** `./clover_leaf`
```text
 Step      10 time   0.0482336 control    sound    timestep   5.82E-03
 Wall clock    24.161110162734985
 Average time per cell    2.9129424868266523E-006
 Step time per cell       2.8137293540769155E-006
```
Iterates correctly on RISC-V.

### Notes
- `COMPILER=GNU` is a pattern across the UK Mini-App Consortium suite (TeaLeaf, NEUTRAL, etc.) — one fix covers all.
- Mixed C + Fortran codebase requires both `gcc` and `gfortran`.

---

## Repo 2: Graph500
**URL:** https://github.com/graph500/graph500
**Date:** 2026-05-02
**Category:** HPC benchmark (graph analytics)
**Language:** C + MPI

### Attempt 1
**Command:** `make` (repo root)
**Error:** `no makefile found`
**Fix:** `cd src && make`

### Attempt 2
**Command:** `cd src && make`
**Error:** `make: mpicc: No such file or directory`
**Fix:** `apt install -y libopenmpi-dev openmpi-bin`

### Attempt 3
**Command:** `make CFLAGS="... -fcommon"`
**Error:** `multiple definition of 'weights'` / `multiple definition of 'column'`
**Cause:** Global vars defined in headers, included by multiple translation units. GCC 10 changed default from `-fcommon` to `-fno-common`, breaking legacy code.
**Fix:** Add `-fcommon` to CFLAGS.
**Note:** Overriding CFLAGS entirely drops the Makefile's `-I` include paths. Always append: `make CFLAGS="$(original_flags) -fcommon"`

### Attempt 4
**Command:** `make graph500_reference_bfs_sssp CFLAGS="-g -O2 -DSSSP -fcommon -I../aml"`
**Prior error:** `undefined reference to 'run_sssp'` — default `bfs` target doesn't link `sssp_reference.c` despite main calling `run_sssp` with `-DSSSP`.
**Fix:** Build the `bfs_sssp` target explicitly.
**Status:** SUCCESS — binary produced and validated

### Validation
**Command:** `mpirun --allow-run-as-root -np 1 ./graph500_reference_bfs_sssp 10`
**Output:** 64 BFS + 64 SSSP iterations, all validated correctly
**TEPS:** ~8.5e+07 BFS, ~3.0e+07 SSSP

---

## Repo 3: standard-RAxML
**URL:** https://github.com/stamatak/standard-RAxML
**Date:** 2026-05-03
**Category:** Phylogenetics (maximum likelihood tree inference)
**Language:** C

### Key observation
RAxML ships 20+ Makefiles. All optimised variants are x86-specific (`Makefile.SSE3.gcc`, `Makefile.AVX.gcc`, etc.). Only `Makefile.gcc` is architecture-agnostic (scalar fallback). Use this for non-x86 targets.

### Attempt 1
**Command:** `make -f Makefile.gcc 2>&1 | tee /workspace/logs/raxml_build.txt`
**Error:** `error: '_MM_FLUSH_ZERO_ON' undeclared (first use in this function)`
**Cause:** `axml.c` line 13719 calls `_mm_setcsr(_mm_getcsr() | _MM_FLUSH_ZERO_ON)` unconditionally. The `#if` guard only excluded PowerPC (`__ppc`), not RISC-V.
**Status:** Failed

### Attempt 2 — Source patch
**Fix:** Two changes:

1. Replaced the include at the PPC-exclusion guard with `sse_shim.h`:
```c
// Before:
#if ! (defined(__ppc) || defined(__powerpc__) || defined(__POWERPC__) || defined(PPC))
#include <xmmintrin.h>

// After:
#include "sse_shim.h"
```

2. Changed the `_mm_setcsr` call site guard to explicit x86-only:
```c
// Before:
#if ! (defined(__ppc) || defined(__powerpc__) || defined(__POWERPC__) || defined(PPC))

// After:
#if defined(__x86_64__) || defined(__i386__)
```

3. Added `sse_shim.h` — scalar no-op stubs for `__m128d`, `_mm_setzero_pd`, etc. on RISC-V and AArch64.

See: `patches/raxml-x86-intrinsic-guard.patch`, `patches/raxml-sse_shim.h`

**Command:**
```bash
cp ../patches/raxml-sse_shim.h sse_shim.h
git apply ../patches/raxml-x86-intrinsic-guard.patch
make -f Makefile.gcc 2>&1 | tee /workspace/logs/raxml_build.txt
```
**Status:** SUCCESS — binary `raxmlHPC` produced

### Validation
**Command:** `file raxmlHPC`
**Output:** `ELF 64-bit LSB pie executable, UCB RISC-V, RVC, double-float ABI`

```
./raxmlHPC
 Error, you must specify a model of substitution with the "-m" option
```
Expected — binary is functional, exits with standard usage message.

### Notes
- `_mm_setcsr` flush-to-zero pattern appears in floating-point-heavy HPC codes that tune FPU rounding. Same pattern seen in Octopus and GAMESS-family codes.
- The RISC-V equivalent is writing to `fcsr`, but for a correctness port the scalar default is fine.

---

## Repo 4: HPCC (HPC Challenge Benchmark)
**URL:** https://github.com/icl-utk-edu/hpcc
**Date:** 2026-05-04
**Category:** HPC benchmark suite (HPL, DGEMM, STREAM, RandomAccess, FFT, PTRANS)
**Language:** C + MPI

### Key observation
HPCC uses HPL's arch-based build system. `hpl/Make.<arch>` defines all compiler and library paths. A `Make.riscv` stub existed in the repo root but had three wrong settings.

### Attempt 1 — Diagnose the stub
Three errors:

1. **Wrong MPI path:** `MPdir = /usr/local/mpi` (MPICH path) — container has OpenMPI.
   Fix: `MPdir = /usr/lib/riscv64-linux-gnu/openmpi`, link with `-lmpi`

2. **Wrong BLAS:** `LAlib = libcblas.a libatlas.a` — ATLAS not installed.
   Fix: `apt install -y libopenblas-dev`, then `LAlib = -lopenblas`

3. **Obsolete linker:** `LINKER = /usr/bin/g77` — not installed, not needed with CBLAS.
   Fix: `LINKER = /usr/bin/mpicc` with `-DHPL_CALL_CBLAS`

See: `configs/hpcc-Make.riscv`

### Attempt 2
```bash
apt install -y libopenblas-dev
cp ../configs/hpcc-Make.riscv hpl/Make.riscv
make arch=riscv 2>&1 | tee /workspace/logs/hpcc_build.txt
```
**Status:** SUCCESS — binary `hpcc` produced (exit 0, zero errors)

### Validation
**Command:** `file hpcc`
**Output:** `ELF 64-bit LSB pie executable, UCB RISC-V, RVC, double-float ABI`

```
mpirun --allow-run-as-root -np 1 ./hpcc
HPL WARNING from process # 0, on line 313 of function HPL_pdinfo:
>>> cannot open file hpccinf.txt <<<
```
Expected — no input config provided. Binary executes cleanly.

### Notes
- `hpccinf.txt` warning means binary is waiting for a run config, not broken.
- `libopenblas-dev:riscv64` is natively packaged in Debian. Any code linking BLAS/LAPACK ports trivially once you know this.
- `-DHPL_CALL_CBLAS` eliminates the Fortran linker dependency entirely.

---

## Repo 5: LAMMPS
**URL:** https://github.com/lammps/lammps
**Date:** 2026-05-04
**Category:** Molecular dynamics
**Language:** C++17 + MPI

### Attempt 1 — CMake configure (nolib preset)
`nolib.cmake` preset disables all external-library packages — tests whether the core C++17 engine compiles on RISC-V.
```bash
cmake ../cmake \
  -DCMAKE_C_COMPILER=mpicc \
  -DCMAKE_CXX_COMPILER=mpicxx \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_CXX_FLAGS='-march=rv64gc' \
  -DBUILD_MPI=yes \
  -DBUILD_OMP=no \
  -C ../cmake/presets/nolib.cmake
```
**Error:** `CMake Error: LAMMPS requires Python 3.6 or later`
**Cause:** CMake uses Python to auto-generate some headers at configure time.
**Fix:** `apt install -y python3 python3-dev`

### Attempt 2
Re-ran same cmake command after installing Python 3. Configure succeeded, `-march=rv64gc` confirmed passed to `mpicxx`.
```bash
make -j2
```
`-j2` to avoid OOM — high concurrency under QEMU uses significantly more memory during compilation.

**Status:** SUCCESS — ~500 source files compiled, zero architecture-specific errors.

### Validation
**Command:** `file lmp`
**Output:** `ELF 64-bit LSB pie executable, UCB RISC-V, RVC, double-float ABI`

```
./lmp -h
Large-scale Atomic/Molecular Massively Parallel Simulator - 30 Mar 2026 - Development
OS: Linux "Ubuntu 24.04.4 LTS" 6.12.76-linuxkit riscv64
Compiler: GNU C++ 13.3.0 with OpenMP not enabled
C++ standard: C++17
```

### Notes
- Modern C++17 codebase, zero arch-specific errors across 500+ files. No source changes needed.
- Throttle to `-j2` or `-j4` for large builds under QEMU.

---

## Master Error Taxonomy
| # | Error class | Observed in | Fix |
|---|-------------|-------------|-----|
| 1 | Hardcoded `-march=native` | CloverLeaf, common in raw Makefiles | Replace with `-march=rv64gc` (arch-detect block) |
| 2 | Missing core toolchain (Fortran) | CloverLeaf, HPCC | `apt install gfortran` |
| 3 | Missing MPI compiler/runtime | Graph500, HPCC, LAMMPS | `apt install libopenmpi-dev openmpi-bin` |
| 4 | GCC 10+ `-fno-common` linker failures | Graph500, legacy C projects | Add `-fcommon` to CFLAGS |
| 5 | Env CFLAGS dropping internal includes | Graph500, older Makefiles | Always append flags, never replace |
| 6 | Broken or implicit default `make` targets | Graph500 | Build specific target explicitly |
| 7 | x86 intrinsics leaking into scalar builds | RAxML, hand-optimized HPC code | `#if defined(__x86_64__)` guard |
| 8 | Hardcoded absolute paths for MPI/BLAS | HPCC | Update `MPdir` for container layout |
| 9 | Hardcoded legacy BLAS (e.g., ATLAS) | HPCC | Swap to OpenBLAS, link `-lopenblas` |
| 10 | Missing build-time script dependencies | LAMMPS | `apt install python3 python3-dev` |
| 11 | QEMU user-mode OOM at high concurrency | LAMMPS, any large CMake build | Throttle to `make -j2` |
