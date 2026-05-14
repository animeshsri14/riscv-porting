# RISC-V HPC Application Porting

Native RISC-V porting of HPC applications from the [LFX RISC-V target spreadsheet](https://tinyurl.com/4v22ujvt), performed inside a `riscv64/ubuntu:24.04` Docker container on Apple Silicon (M1). All binaries were compiled natively on RISC-V — not cross-compiled from x86.

This repository is the supporting evidence for my LFX Mentorship application proposal:  
**[Broadening the RISC-V High Precision Code Base and Reach](lfx_proposal.md)**

---

## Ported Applications

| # | Application | Category | Build System | Validation |
|---|-------------|----------|-------------|------------|
| 1 | [LAMMPS](https://github.com/lammps/lammps) | Molecular dynamics | CMake | Binary self-reports riscv64, 500+ source files, zero arch errors |
| 2 | [HPCC (HPL)](https://github.com/icl-utk-edu/hpcc) | HPC benchmark suite | HPL arch-file | Binary executes, awaits `hpccinf.txt` config |
| 3 | [Graph500](https://github.com/graph500/graph500) | Graph benchmark | Makefile + MPI | 64 BFS + 64 SSSP iterations validated, ~8.5e+07 TEPS |
| 4 | [CloverLeaf](https://github.com/UK-MAC/CloverLeaf_Serial) | Hydrodynamics mini-app | Makefile | Runs, timestep output verified |
| 5 | [standard-RAxML](https://github.com/stamatak/standard-RAxML) | Phylogenetics | Makefile | RISC-V ELF binary, exits with standard usage message |

Full build logs, error traces, and step-by-step notes for each port are in [`logs/porting_log.md`](logs/porting_log.md).

---

## Error Taxonomy

Across these 5 codebases I identified 11 recurring build-failure classes. These patterns form the basis for my automation script, which maps each class to a detection-and-fix rule.

| # | Error Class | Observed In | Fix |
|---|-------------|-------------|-----|
| 1 | Hardcoded `-march=native` | CloverLeaf, common in raw Makefiles | Replace with `-march=rv64gc` (arch-detect block) |
| 2 | Missing core toolchain (Fortran) | CloverLeaf, HPCC | `apt install gfortran` |
| 3 | Missing MPI compiler/runtime | Graph500, HPCC, LAMMPS | `apt install libopenmpi-dev openmpi-bin` |
| 4 | GCC 10+ `-fno-common` linker failures | Graph500, legacy C projects | Add `-fcommon` to CFLAGS |
| 5 | Env `CFLAGS` dropping internal includes | Graph500, typical in older Makefiles | Always append flags, never replace |
| 6 | Broken or implicit default `make` targets | Graph500 | Build specific target explicitly |
| 7 | x86 intrinsics leaking into scalar builds | RAxML, hand-optimized HPC code | `#if defined(__x86_64__)` guard |
| 8 | Hardcoded absolute paths for MPI/BLAS | HPCC | Update `MPdir` for container layout |
| 9 | Hardcoded legacy BLAS (e.g., ATLAS) | HPCC | Swap to OpenBLAS, link `-lopenblas` |
| 10 | Missing build-time script dependencies | LAMMPS | `apt install python3 python3-dev` |
| 11 | QEMU user-mode OOM at high concurrency | LAMMPS, any large CMake build | Throttle to `make -j2` |

---

## Patches and Configs

These are the only source modifications made. Apply them to a fresh clone of each upstream repo.

### CloverLeaf patch `patches/cloverleaf-march-riscv.patch`

Replaces hardcoded `-march=native` with an architecture-detecting block:

```makefile
ARCH := $(shell uname -m)
ifeq ($(ARCH), riscv64)
    ARCH_FLAGS = -march=rv64gc
else
    ARCH_FLAGS = -march=native
endif
```

Apply:
```bash
git clone https://github.com/UK-MAC/CloverLeaf_Serial
cd CloverLeaf_Serial
git apply ../patches/cloverleaf-march-riscv.patch
make COMPILER=GNU
```

---

### RAxML patches `patches/raxml-x86-intrinsic-guard.patch` and `patches/raxml-sse_shim.h`

Two changes:

1. **`axml.c`**: Tightens the preprocessor guard from a PPC-exclusion (which accidentally lets RISC-V through) to an explicit x86-only inclusion. Changes `#if !(ppc...)` to `#if defined(__x86_64__) || defined(__i386__)` at the `_mm_setcsr` call site.

2. **`sse_shim.h`**: New file. Provides scalar type aliases for `__m128d`, `__m128`, `__m128i` and no-op macros for `_mm_setzero_pd`, `_mm_load_pd`, `_mm_store_pd` on RISC-V and AArch64. Included in place of `<xmmintrin.h>`.

Apply:
```bash
git clone https://github.com/stamatak/standard-RAxML
cd standard-RAxML
cp ../patches/raxml-sse_shim.h sse_shim.h
git apply ../patches/raxml-x86-intrinsic-guard.patch
make -f Makefile.gcc
```

---

### HPCC config `configs/hpcc-Make.riscv`

Custom HPL platform file for the riscv64 container. Key differences from the stub in the upstream repo:

- `MPdir = /usr/lib/riscv64-linux-gnu/openmpi` (OpenMPI, not MPICH)
- `LAlib = -lopenblas` (OpenBLAS, not ATLAS)
- `LINKER = /usr/bin/mpicc` with `-DHPL_CALL_CBLAS` (eliminates the `g77` Fortran linker dependency)
- `CCFLAGS` uses `-march=rv64gc` explicitly

Apply:
```bash
git clone https://github.com/icl-utk-edu/hpcc
cd hpcc
cp ../configs/hpcc-Make.riscv hpl/Make.riscv
make arch=riscv
```

---

### Graph500 — no patch needed

Only runtime flags and dependency installs required. No source changes:
```bash
git clone https://github.com/graph500/graph500
cd graph500/src
apt install -y libopenmpi-dev openmpi-bin
make graph500_reference_bfs_sssp CFLAGS="-g -O2 -DSSSP -fcommon -I../aml"
mpirun --allow-run-as-root -np 1 ./graph500_reference_bfs_sssp 10
```

---

### LAMMPS — no patch needed

Only CMake flags and a Python install required. No source changes:
```bash
git clone https://github.com/lammps/lammps
cd lammps && mkdir build && cd build
apt install -y python3 python3-dev libopenmpi-dev
cmake ../cmake \
  -DCMAKE_CXX_COMPILER=mpicxx \
  -DCMAKE_CXX_FLAGS='-march=rv64gc' \
  -DBUILD_MPI=yes \
  -C ../cmake/presets/nolib.cmake
make -j2
```

---

## Environment

```
Container:   riscv64/ubuntu:24.04 (Docker + QEMU binfmt on Apple Silicon M1)
Compiler:    GCC 13.3 (native riscv64)
Fortran:     GFortran 13.3
MPI:         OpenMPI 4.1.6 (native riscv64 Debian package)
BLAS:        OpenBLAS (libopenblas-dev:riscv64 — native Debian package)
Build tools: CMake 3.28, GNU Make 4.3, Autotools
```

To reproduce the environment:
```bash
docker run --platform linux/riscv64 -it riscv64/ubuntu:24.04 bash
apt update && apt install -y \
  gcc g++ gfortran make cmake git \
  libopenmpi-dev openmpi-bin \
  libopenblas-dev \
  python3 python3-dev
```

---

## Repository Structure

```
riscv-porting/
   lfx_proposal.md            // LFX mentorship proposal
   patches/
         cloverleaf-march-riscv.patch
         raxml-x86-intrinsic-guard.patch
         raxml-sse_shim.h
   configs/
         hpcc-Make.riscv
   logs/
         porting_log.md       full porting journal (step-by-step)
         cloverleaf_build.txt
         graph500_make.txt
         raxml_build.txt
         hpcc_build.txt
         lammps_build.txt
         lammps_cmake.txt
   README.md
```
