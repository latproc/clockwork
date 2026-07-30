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

## Current interim workflow (three long-lived lines + agent ports)

Until the fleet is elc-only for **plant iod**, maintain **three long-lived branches**
rather than one tree full of forever-ifdefs. Partial sync only: function diverges
by product; shared surfaces (JSON, channels, client ZMQ) stay ported.

| Line | Branch | Product / consumers | Owns (canonical home) |
|------|--------|---------------------|------------------------|
| **A — Prod legacy** | `prod-experimental-mqtt-fix` | Plant `iod` / `iod_sdo` (ecrt) | ecrt idle/pending-out; mqtt-fix plant deploys; Track F memory on live 2G4C |
| **B — Elc / kernel** | `feature/iod-elc-kernel-transport` | Plant `iod-elc` | multi-domain, shadow apply, promote, topology, dig ASAP on kernel PD |
| **C — Client / ZMQ** | `prod-client-zmq-fix` (cut from **A**) | `humid`, `modbusd`, `dbd`, `persistd`, `device_connector`, panel/tooling linking **cw_client** / channel setup | Channel handshake, timeout/REQ recreate, ConnectionManager, portable socket monitors, thin-client CMake (no full EtherCAT) |

**Historical:** `humid-zmq-client-fix` holds older REQ-hang history. Do **not** use it as a sync base; treat as archive. Prefer C for new client work.

Also see: `iod/docs/BRANCHES.md` (port matrix + commit scopes).

### Port matrix (what moves where)

| Scope tag | Meaning | Land first | Port to |
|-----------|---------|------------|---------|
| `scope: bus-legacy` | ecrt domain queue/send, DEFAULT_DATA TX, pending-out clear tied to process-image echo | **A** | nowhere (or note “N/A elc”) |
| `scope: bus-elc` | kernel shadow, multi-domain WC firewall, `kernelPromoteIoOperational`, active+link ready, `ecrt_stubs_elc`, topology/recipe elc tools | **B** | nowhere (or note “N/A legacy”) |
| `scope: iod-core` | ownership, JSON/cJSON, WEBREQUEST, thrash/PROCSNAP/HEALTH, non-bus Scheduler floors, pure iod processing shared by both plant binaries | **A or B** (prefer A if proven on 2C-120) | the other of A/B same week |
| `scope: client-zmq` | channel client setup REQ, ConnectionManager, humid/modbusd/dbd/persistd ZMQ, cw_client API/build without ecat | **C** | A and B when monorepo sources overlap (`Channel.*`, `ConnectionManager.*`, `cw_client*`, client CMake) so plant trees do not ship stale client surfaces |

**Not full merges** of A↔B↔C tips. Prefer small cherry-picks; divergence is large.

### Agent port rules

1. **Classify** every change with a **scope tag** (above) before coding.
2. **Prefer cherry-pick** of a small commit; if conflict is only `#ifdef USE_KERNEL_ETHERCAT` / file layout, resolve for the **target** backend and drop the other arm.
3. **Do not** reintroduce dual-path ifdefs on A “for future elc” or on B “for future ecrt” when porting — each plant branch stays single-backend where possible.
4. **Verify on target:**
   - **A:** build `iod_sdo`, idle PROCSNAP if processing change, no EtherCAT regression.
   - **B:** build `iod-elc`, idle PROCSNAP (`brk_out`/`absorb`), multi-domain still sane if domain2 down.
   - **C:** build client targets used in the change (`humid`, `modbusd`, `dbd`, `persistd`, `cw_client` / tests) without requiring a full plant EtherCAT stack when possible.
5. **Commit message style:**
   - Scope: `scope: client-zmq|iod-core|bus-elc|bus-legacy` on the subject or first body line.
   - Ports: `Port of <hash> from <branch>: <one line>` plus any intentional A/B/C delta.
6. **Memory / WEBREQUEST / cJSON** → almost always `scope: iod-core` — port A↔B the same week.
7. **ProcessingThread wait / IO ready / Output::turnOn** — re-read A and B; often a **semantic** port, not a literal diff.
8. **Track E channel/HMI** → `scope: client-zmq` on **C** first; redeploy **client** binaries; port overlapping sources to A/B. Server sticky-REQ is usually not the plant bug.

This three-line model is **better than one mega-branch with permanent dual ifdefs** for plant backends, and keeps client ZMQ from being a side effect of A or B only. Removal plan Phases 3–4 still apply when legacy ecrt plants are gone; **C survives** after A dies (clients always exist).

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
- Keep the **three-line** port model (A/B plant + C clients) in `BRANCHES.md` until
  A is retired; client line C does not go away with ecrt removal.
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
