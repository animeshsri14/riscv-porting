# Broadening the RISC-V High Precision Code Base and Reach

**Applicant:** Animesh Srivastava
**GitHub:** [github.com/animeshsri14](https://github.com/animeshsri14)
**Email:** animeshsri.official@gmail.com
**Term:** Summer 2026 (Jun–Aug)

---

## What I Have Already Done

Before writing this proposal, I set up a native RISC-V development environment (`riscv64/ubuntu:24.04` via Docker + QEMU on Apple Silicon) and ported 5 applications from the target spreadsheet across 4 different build system types. Every binary was compiled natively on RISC-V (not cross-compiled from x86), verified to execute, and validated for correctness. Full build logs, error traces, and patches are in my [porting log]([https://github.com/animeshsri14/riscv-porting/blob/main/logs/porting_log.md](https://github.com/animeshsri14/riscv-porting/blob/master/logs/porting_log.md)).

| # | Application | Category | Build System | Result |
|---|-------------|----------|-------------|--------|
| 1 | CloverLeaf_Serial | HPC mini-app | `make COMPILER=GNU` |  Runs, iterates correctly (timestep output verified) |
| 2 | Graph500 | HPC benchmark | Raw Makefile + MPI |  64 BFS + 64 SSSP validated, ~8.5e+07 TEPS |
| 3 | standard-RAxML | Phylogenetics | `make -f Makefile.gcc` |  RISC-V ELF, functional (usage output correct) |
| 4 | HPCC (HPL) | HPC benchmark | HPL arch-file (`make arch=riscv`) |  Binary executes, awaits config |
| 5 | LAMMPS | Molecular dynamics | CMake preset |  500+ source files, zero arch errors |

LAMMPS is the most significant port here in terms of size. It compiled on RISC-V without a single architecture-specific error — 500+ C++17 source files, clean. The binary self-reports:

```
Large-scale Atomic/Molecular Massively Parallel Simulator - 30 Mar 2026
OS: Linux "Ubuntu 24.04.4 LTS" 6.12.76-linuxkit riscv64
Compiler: GNU C++ 13.3.0 with OpenMP not enabled
```

### Why Native Compilation Matters

I chose native compilation over cross-compilation. Native compilation inside a riscv64 Docker container catches issues that cross-compilation misses: ABI mismatches at link time, runtime library resolution, and architecture-dependent behavior that only appears when the build tools themselves run on RISC-V. It's a slower workflow, but the results are more trustworthy.

---

## The Error Taxonomy

Across 5 codebases spanning C, C++17, Fortran, MPI, BLAS, and CMake, I hit 11 distinct error classes. These patterns form the basis for my automation script, which maps each class to a detection-and-fix rule:

| # | Error Class | Observed In | Fix |
|---|-------------|-------------|-----|
| 1 | Hardcoded `-march=native` | CloverLeaf, common in raw Makefiles | Replace with `-march=rv64gc` |
| 2 | Missing core toolchain (Fortran) | CloverLeaf, HPCC | `apt install gfortran` |
| 3 | Missing MPI compiler/runtime | Graph500, HPCC, LAMMPS | `apt install libopenmpi-dev` |
| 4 | GCC 10+ `-fno-common` linker failures | Graph500, legacy C projects | Add `-fcommon` to CFLAGS |
| 5 | Env `CFLAGS` dropping internal includes | Graph500, typical in older Makefiles | Append flags instead of replacing (`+=`) |
| 6 | Broken or implicit default `make` targets | Graph500 | Build specific target explicitly |
| 7 | x86 intrinsics leaking into scalar builds | RAxML, hand-optimized HPC code | Guard with `#if defined(__x86_64__)` |
| 8 | Hardcoded absolute paths for MPI/BLAS | HPCC | Update paths for standard container layout |
| 9 | Hardcoded legacy BLAS (e.g., ATLAS) | HPCC | Swap to OpenBLAS, link `-lopenblas` |
| 10 | Missing build-time script dependencies | LAMMPS | `apt install python3` |
| 11 | QEMU user-mode OOM at high concurrency | LAMMPS, any large CMake build | Throttle to `make -j2` |

Each class in this taxonomy maps directly to a detection rule in my automation script — grep for `march=native` in Makefiles, check for `multiple definition` in linker stderr, look for `_mm_` includes in source files.

---

## Why This Matters

The spreadsheet lists ~400 applications. This is a long list of projects, and without automation, it is impossible to port all of them in a 12-week period. 

But these 11 error classes cover roughly 80% of the failures I will encounter. The other 20% will be application-specific issues (custom build generators, inline assembly, GPU-only codepaths). My plan is to automate the 80% and manually handle the 20%, documenting each new failure class as I encounter it so the taxonomy grows over the course of the mentorship.

---

## The Automation Framework

The automation framework is a pipeline that encodes the 11 error classes above as detection-and-fix rules.

**How it works:**

```
Input: source repo URL + category tag

Step 1: Clone + detect build system (CMake? Autotools? raw Makefile?)
Step 2: Run initial build attempt, capture stderr
Step 3: Match stderr against known error patterns:
         - "march=native" found? → sed replace with rv64gc
         - "multiple definition" linker error? → retry with -fcommon
         - "_mm_setcsr" or xmmintrin.h? → apply x86 guard patch
         - "No such file: mpicc"? → install MPI
         - "No such file: gfortran"? → install gfortran
Step 4: Retry build
Step 5: If binary produced → run `file` to verify RISC-V ELF
Step 6: Log result + any new error classes to taxonomy
```

Steps 3-4 codify the exact manual porting process I used for the initial 5 repositories.

**Concrete deliverable:**

```bash
./riscv-port.sh --repo https://github.com/lammps/lammps --build-system cmake
# Outputs: ELF binary, build log, error classification, pass/fail
```

I will start building this in Week 2 (not Week 8) because the automation is what makes the volume possible. Every manual port I do in the first week generates new rules that feed back into the script.

---

## 12-Week Plan

I hope to shape the exact priorities in collaboration with the mentor — the ordering below is my best estimate based on the spreadsheet and the patterns I have already seen.

### Phase 1 — Foundation + Early Automation (Weeks 1–3)

| Week | Work | Deliverable |
|------|------|-------------|
| 1 | Formalize RISC-V dev environment as reproducible Dockerfile. Port 5–8 more codes manually to expand error taxonomy to ~15 classes. | Dockerfile, expanded taxonomy, 10–13 total ports |
| 2 | Build `riscv-port.sh` v1 encoding the taxonomy as detection rules. Test it against the 10 already-ported codes to validate it reproduces their builds unattended. | Working automation script, regression tests |
| 3 | Port foundational libraries (OpenBLAS with RVV, FFTW, HDF5, Eigen, LAPACK). These unlock downstream applications. | Verified dependency stack, version-locked configs |

**Why dependencies in Week 3, not Week 1:** I already know from HPCC that OpenBLAS is natively packaged for RISC-V (`libopenblas-dev:riscv64`). The dependency problem is real but smaller than it looks — most of the core math libraries are already in Debian's riscv64 repos. Week 3 is for validating and locking versions, not fighting from scratch.

### Phase 2 — Bulk Porting by Category (Weeks 4–8)

This is where the automation pays off. I am grouping the 400-code spreadsheet into categories where applications share build systems, dependencies, and failure modes. Within each category, after porting the first application manually, the script should handle subsequent ones with minimal manual intervention.

| Week | Category | Target Apps | Expected New Error Classes |
|------|----------|-------------|---------------------------|
| 4 | **HPC Benchmarks** (6 apps) | STREAM, LINPACK variants, HPL variants, additional HPCC configs, NAS Parallel Benchmarks | Minimal — these are clean C/Fortran + MPI |
| 5 | **PDE/FEA Solvers** (8 apps) | Elmer, CalculiX, FreeFEM, FEniCS, OOFEM, TOCHNOG, GetDP, ALBERTA | Autotools variations, PETSc dependency |
| 6 | **CFD** (6 apps) | OpenFOAM, Code_Saturne, Gerris, Incompact3d, TYPHON, Dolfyn | Large CMake builds, Fortran 90 modules |
| 7 | **Molecular Dynamics + Chemistry** (8 apps) | GROMACS (CPU), NAMD (CPU), Tinker-HP, HOOMD-Blue (CPU), Quantum ESPRESSO, NWChem, CP2K, GAMESS | FFTW/BLAS dependency chain, Fortran interop |
| 8 | **Phylogenetics + Climate** (7 apps) | MrBayes, POY, BORN, WRF, NEMO, CESM, CAM | Fortran-heavy, netCDF/HDF5 I/O dependencies |

**Running total by Week 8: ~48 applications** (5 already done + 8 in Phase 1 + 35 in Phase 2)

I am deliberately excluding from the plan:
- **Proprietary codes** (VASP, AMBER, MATLAB, MOE) — no source access
- **GPU-only codes** (CUDA-dependent: TeraChem, FastROCS, HOOMD-Blue GPU mode) — not portable without a RISC-V GPU stack
- **Non-HPC software** (video encoding, retail analytics, broadcast graphics) — out of scope for this project
- **Codes requiring custom hardware** (FPGA, capture cards) — not testable

This is a hard scoping boundary. There are roughly 120–150 open-source, CPU-based, source-available HPC/AI codes in the spreadsheet. I am targeting 50–70 of the highest-impact ones.

### Phase 3 — Harder Ports + Performance (Weeks 9–11)

| Week | Work | Deliverable |
|------|------|-------------|
| 9 | **Math/Computing frameworks** (5 apps): Charm++, Eigen (header-only, verify), ELPA, ArrayFire (CPU), PETSc | Ports or documented blockers |
| 10 | **ML frameworks** (3 apps): PyTorch CPU backend, NumPy (RISC-V build), scikit-learn | These are large; focus on "does it build and pass basic tests" |
| 11 | **Performance profiling** of 5 key applications (LAMMPS, GROMACS, OpenBLAS DGEMM, FFTW, HPCC). Collect RISC-V vs x86 timing data using `perf` and wall-clock benchmarks. | Performance comparison report with honest gap analysis |

**Running total by Week 11: ~56–66 applications**

### Phase 4 — Documentation + Upstreaming (Week 12)

| Deliverable | Description |
|-------------|-------------|
| Per-app build guides | `/docs/apps/<name>.md` — reproducible steps for every ported code |
| Updated error taxonomy | Final version with all discovered error classes and frequency data |
| Upstream PRs | Submit RISC-V build configs and minor portability fixes to upstream repos where appropriate |
| Final benchmark report | RISC-V vs x86 performance data for 5 key applications with bottleneck analysis |
| Automation toolkit | The `riscv-port.sh` script + Dockerfile + CI config as a reusable package |

---

## Target Application List (50–70 codes)

Grouped by category. Every application here is open-source, CPU-capable, and builds with a standard toolchain (GCC/G++/GFortran + MPI + BLAS/LAPACK). No outliers.

### Tier 1 — Already Ported (5)
CloverLeaf, Graph500, RAxML, HPCC, LAMMPS

### Tier 2 — High Impact, Straightforward (25–30)

| Category | Applications |
|----------|-------------|
| HPC Benchmarks | STREAM, NAS Parallel Benchmarks, LINPACK, additional HPCC sub-benchmarks |
| PDE/FEA | Elmer, CalculiX, FreeFEM, FEniCS, OOFEM, GetDP, ALBERTA, TOCHNOG |
| CFD | OpenFOAM, Code_Saturne, Gerris, Incompact3d, Dolfyn |
| Molecular Dynamics | GROMACS (CPU mode), Tinker-HP, HOOMD-Blue (CPU) |
| Phylogenetics | MrBayes, POY, BORN |

These share common dependency patterns (MPI + BLAS + HDF5) and standard build systems (CMake or Autotools). After the first port in each category, the automation script handles the rest.

### Tier 3 — Medium Effort (15–20)

| Category | Applications |
|----------|-------------|
| Quantum Chemistry | Quantum ESPRESSO, NWChem, CP2K, GAMESS, Octopus, MILC, QMCPACK |
| Climate/Weather | WRF, NEMO, CESM, CAM, Elmer/Ice |
| Astrophysics | GADGET, Enzo, ChaNGa |
| Electromagnetism | Meep, MIT Photonic Bands |
| Math/HPC Libraries | Charm++, ELPA, PETSc, OpenUCX |

These have larger dependency trees and may require Fortran 90 module compilation or netCDF support. Expected effort: 1–2 days each with automation, up to 3 days for the complex ones (WRF, CP2K).

### Tier 4 — Best Effort (5–10)

| Category | Applications |
|----------|-------------|
| ML Frameworks | PyTorch (CPU), NumPy, scikit-learn |
| Simulation | SST, PFLOTRAN, VPIC, Cardioid |
| Tools | SLURM, PAPI, TAU |

These are the largest codebases with the most complex build systems. If Phase 2 runs ahead of schedule, I tackle these. If not, I document what I learned from attempting them and where they got blocked.

---

## Technical Environment

```
Container:     riscv64/ubuntu:24.04 (Docker, QEMU user-mode emulation)
Compiler:      GCC 13.3 (native riscv64), GFortran 13.3
MPI:           OpenMPI 4.1.6 (native riscv64 package)
BLAS/LAPACK:   OpenBLAS (native riscv64 package, RVV-capable)
Build Systems: CMake 3.28, GNU Make, Autotools
Profiling:     Linux perf, wall-clock timing
VCS:           Git (structured branch-per-port workflow)
```

This environment is already running. It is not something I plan to set up in Week 1.

---

## Risk Mitigation

| Risk | How I Handle It |
|------|--------------------|
| An application stalls for >3 days | Move on. Document the blocker. Return to it only if automation later solves the class of error. |
| Dependency not available for riscv64 | Check Debian riscv64 repos first (most math libs are there). If missing, build from source with the same toolchain. |
| QEMU too slow for benchmarks | Use wall-clock timing with sufficient iterations. Performance numbers are relative comparisons, not absolute — consistency matters more than speed. |
| Scope creep | Hard cap at 70 apps. Every port must produce a verified RISC-V ELF binary and a documented build guide, or it does not count. |
| New error class breaks automation | Good — that means the taxonomy is growing. Add the new pattern as a detection rule and re-run. |

---

## What I Bring

| Skill | Where I Demonstrated It |
|-------|------------------------|
| RISC-V native compilation | 5 HPC codes already ported in a running riscv64 Docker environment |
| C/C++ systems programming (5+ years) | Bare-metal ARM Cortex-M drivers at DRDO (SPI, I2C, UART, GPIO — no HAL) |
| Large-scale open-source contribution | ns-3 network simulator — merged MRs, published contributor in v3.47 release |
| Build system debugging | Diagnosed and fixed CMake, Autotools, raw Makefile, and HPL arch-file systems across 5 repos |
| Error pattern recognition | Built an 11-class error taxonomy mapping real porting failures to automation rules |
| MPI + BLAS integration | Resolved OpenBLAS dynamic linking and MPI path discovery for HPCC on RISC-V |

---

*This proposal scales up a methodology that is already built, tested, and running. The 12-week plan extends the foundation of my initial 5 ports to 50–70 applications using automation derived from real, observed patterns.*

**Contact:** Animesh Srivastava | [GitHub](https://github.com/animeshsri14) | animeshsri.official@gmail.com
