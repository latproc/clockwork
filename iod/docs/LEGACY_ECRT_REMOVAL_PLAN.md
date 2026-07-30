# Plan: remove legacy IgH ecrt userland path

**Status:** proposal only (not scheduled)  
**Branch context:** `feature/iod-elc-kernel-transport` (kernel `elc_ethercat`)  
**Date:** 2026-07-30

## Why consider it

Today iod is built two ways:

| Target | Define | Bus path |
|--------|--------|----------|
| `iod` / `iod_sdo` | no `USE_KERNEL_ETHERCAT` | IgH **ecrt** userland (`ecrt_*`, domain queue/send) |
| `iod-elc` | `USE_KERNEL_ETHERCAT` | Kernel cyclic master + shadow + stubs |

Rough dual-path surface today: **~70** `#ifdef USE_KERNEL_ETHERCAT` sites, concentrated in:

- `ECInterface.cpp` / `.h` (largest)
- `IOComponent.cpp` (turnOn/setValue, processAll, defaults)
- `ProcessingThread.cpp` (ready/HW init/brk_out — just fixed)
- `ecat_thread.cpp`, `iod.cpp`, plus `ecrt_stubs_elc.cpp` for elc link

**Maintenance cost of keeping both:**

- Every idle/CPU, digital-edge, and multi-domain fix must be reasoned twice.
- Easy to “fix” one path and leave the other broken (this plant’s HW-init thrash was elc-only).
- Reviews and tests double; plant fleet still mixed (`iod_sdo` vs `iod-elc`).

**Benefit of removing legacy ecrt from the tree (or from the elc product line):**

- Single ownership model: kernel apply + process mask + multi-domain.
- Drop stubs and dead DEFAULT_DATA / process-image TX paths for new work.
- Simpler PROCSNAP/idle semantics.

**Cost / risk:**

- Fleet plants still on **userland ecrt** (`iod_sdo`) need a migration path or a maintained fork/target.
- Etherlab version/matrix, commissioning scripts, and operator muscle memory.
- Regression surface: SDO, DC, multi-domain WC firewall, CiA402, softstart outs.

**Recommendation:** Yes — **easier to maintain long-term if the fleet standardises on iod-elc**. Do **not** delete ecrt until a written fleet matrix and a green “elc-only” CI job exist. Prefer a staged removal over a big bang.

---

## Preconditions (gate)

1. **Fleet inventory:** each plant → binary (`iod_sdo` vs `iod-elc`), kernel module, domain count, known offline-slave policy.
2. **Feature parity checklist** green on at least one 1G2C and one 2G4C plant (or lab twin):
   - dual-domain arm/disarm, WC incomplete firewall  
   - digital edge latency vs quiet pull  
   - ANALOG/COUNTER emit + softstart SetState  
   - SDO / recipe / ESTUN path used in production  
3. **CI:** `iod-elc` Release build + unit/integration tests that do not require ecrt.
4. **Rollback:** package keeps last `iod_sdo` artifact for one release train (or plant-local prev binary policy already used).

---

## Phased plan

### Phase 0 — Document and freeze dual-path policy (1–2 days)

- Mark `iod_sdo` / ecrt as **maintenance-only** in `TRANSPORT.md` / `OPEN_WORK_PLAN.md`.
- New features default to elc; dual-path PRs need explicit “both paths tested” or “legacy N/A”.
- Inventory plants (spreadsheet or `cw_issues` note).

### Phase 1 — Shrink ifdef surface without deleting ecrt (1–2 weeks)

Goal: make elc the **default mental model**; legacy behind thin adapters.

| Step | Work |
|------|------|
| 1.1 | Extract shared “IO ready / promote operational / out-service” helpers used by both paths (today’s kernel helpers become the main API; ecrt calls in). |
| 1.2 | Collapse `Output::turnOn` / `setValue` so legacy is a single `applyLegacyProcessImage()` and kernel is `applyKernel*`. |
| 1.3 | Ensure all idle/CPU and multi-domain docs describe **elc first**, ecrt as appendix. |
| 1.4 | No behaviour change required on `iod_sdo` plants. |

Exit: fewer nested ifdefs in `ProcessingThread` / `IOComponent`; still two link targets.

### Phase 2 — elc-only product branch / CMake option (1 week)

- CMake: `IOD_ETHERCAT_BACKEND=kernel|ecrt` (default **kernel** on this branch).
- When `kernel`: do not compile ecrt-only TU paths; keep stubs only as needed.
- When `ecrt`: current `iod_sdo` behaviour.
- CI matrix: both backends until Phase 4.

Exit: clean build flags; developers rarely touch ecrt when working elc.

### Phase 3 — Plant migration (calendar driven)

For each plant:

1. Preflight: kernel module, topology, dual-domain, RT cycle params (`TRANSPORT.md`).
2. Stage `iod-elc`, keep `iod_sdo.prev-*` rollback.
3. Soak: PROCSNAP idle (`brk_out≈0`, absorb high), production day, servo domain power cycle.
4. Flip service run script permanently; record plant in inventory as elc.

No code deletion yet — only deployment.

### Phase 4 — Delete legacy ecrt from mainline (after inventory empty or forked)

**Only when no production plant requires ecrt on the main branch** (or ecrt lives on `legacy/ecrt-userland`).

| Remove / gut | Keep |
|--------------|------|
| `ecrt_*` real calls in `ECInterface` userland path | Kernel bus + multi-domain |
| DEFAULT_DATA / process-image TX queues used only by ecrt | Kernel shadow apply |
| Dual `machine_is_ready` definitions that encode ecrt “all slaves OP” | elc active+link (+ explicit optional-slave policy) |
| Dead `#else` branches under `USE_KERNEL_ETHERCAT` | Single processing wait model |
| Optionally `ecrt_stubs_elc.cpp` if no leftover refs | |

Suggested PR stack:

1. Docs + inventory freeze  
2. Helper extraction (no delete)  
3. CMake backend default kernel  
4. Delete ecrt after last plant migrated (or move to legacy branch)

### Phase 5 — Optional-slave policy (related cleanup)

Independent of ecrt removal but tangled with today’s bug:

- Config flag or LPC: “required modules” vs “optional” for OP aggregation.
- Avoid encoding “all modules OP” as the only ready signal for multi-domain plants.

---

## What not to do

- Delete ecrt while any live plant still runs `iod_sdo` without a migration window.
- Merge elc-only idle fixes into shared code without an ecrt compile/test if `iod_sdo` remains supported.
- Assume domain 2 offline is “broken” — it is a valid dual-domain power state; ready policy must allow primary domain work.

---

## Success metrics

| Metric | Target |
|--------|--------|
| `#ifdef USE_KERNEL_ETHERCAT` count on main product | → 0 (or only build-system) |
| Idle PROCSNAP on dual-domain with domain2 down | `brk_out≈0`, absorb dominates (as after 2026-07-30 fix) |
| Time to implement IO/processing change | single path review |
| Fleet | 100% iod-elc or explicit legacy branch |

---

## Decision

| Question | Answer |
|----------|--------|
| Would removal be easier long-term? | **Yes**, if the fleet commits to elc. |
| Do it immediately? | **No** — phase 0–1 first; delete only after migration. |
| Next concrete step | Fleet inventory + mark ecrt maintenance-only; continue ifdef shrink on elc branch. |
