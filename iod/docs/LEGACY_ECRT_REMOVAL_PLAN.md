# Plan: remove legacy IgH ecrt from branch B

**Status:** largely complete on **B** (`feature/iod-elc-kernel-transport`) — ecrt runtime path removed  
**Legacy ecrt home:** line **A** (`prod-experimental-mqtt-fix` / `iod_sdo`) — **kept**  
**Product on B:** `iod-elc` only (binary name **not** renamed to `iod`)  
**Date:** 2026-07-31

## Decision

Fleet plants that still need IgH userland keep building and running from **A**.
Branch **B** is cleaned of dual-path ecrt so maintainers do not reason about two
backends in one tree. This is **not** a requirement that every plant migrate
before B is elc-only.

| Line | Product | ecrt? |
|------|---------|-------|
| A | `iod_sdo` | yes (canonical) |
| B | `iod-elc` | **no** (removed) |
| C | clients | N/A |

## Why

- Dual-path cost: ~66 `USE_KERNEL_ETHERCAT` sites; fixes often elc-only or ecrt-only.
- Stubs (`ecrt_stubs_elc.cpp`) existed only because shared TUs still referenced ecrt.
- Three-line port model already isolates bus work (`scope: bus-elc` vs `bus-legacy`).

## Work on B (checklist)

| Phase | Work | Status |
|-------|------|--------|
| 0 | Docs: B elc-only policy; A holds ecrt | **done** |
| 1 | CMake: drop `BUILD_LEGACY_IOD`, `iod`, `iod_sdo`, EtherLab plant wiring | **done** |
| 2 | Delete ecrt path on B (no separate ecrt TU kept) | **done** — SDO local `sdoBuffer()` + mailbox; DC/check_* elc; stubs file **deleted** |
| 3 | Scripts/docs: elc-primary; legacy scripts demoted | **done** |
| 4 | Verify: `iod-elc` + `cw` build; no undefined `ecrt_*` | **done** |

### Outcome (B)

- No `ecrt_stubs_elc.cpp`; `nm -u ECInterface.cpp.o` has **no** `ecrt_*`.
- IgH types may still come from system `ecrt.h` (`ec_slave_info_t`, `EC_READ_*` macros on local buffers).
- Method names `ECModule::ecrtMasterSlaveConfig` / `ecrtSlaveConfigPdos` remain as **no-ops** (rename optional cleanup).
- Full IgH `iod_sdo` lives only on **line A**.

## Port rules (after B cleanup)

1. **Classify** with scope tags (`BRANCHES.md`).
2. **Never** reintroduce ecrt ifdefs on B.
3. **`scope: iod-core`** ports A↔B are **semantic** (ready/wait/IO may differ).
4. **`scope: bus-legacy`** only on A; **`scope: bus-elc`** only on B.
5. Verify on the target line’s product binary.

## Success metrics (B)

| Metric | Target |
|--------|--------|
| `#ifdef USE_KERNEL_ETHERCAT` in `iod/src` | 0 |
| `ecrt_*` in product sources | 0 |
| `ecrt_stubs_elc.cpp` | deleted |
| `BUILD_LEGACY_IOD` / `iod` / `iod_sdo` on B | gone |
| Binary name | `iod-elc` |
| Line A | untouched by this cleanup |

## What not to do

- Gut A as part of B cleanup.
- Full-merge A↔B tips.
- Rename `iod-elc` → `iod` in the same program.
- Assume domain 2 offline is “broken” on multi-domain plants.
