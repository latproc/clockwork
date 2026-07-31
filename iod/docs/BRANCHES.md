# Long-lived git lines (iod / Clockwork clients)

**Updated:** 2026-07-31 (B is elc-only product)

Three lines stay in **partial** sync. Do not full-merge tips. Port small commits
with scope tags.

| Line | Branch | Tip product | Canonical home for |
|------|--------|-------------|--------------------|
| **A** | `prod-experimental-mqtt-fix` | `iod_sdo` (IgH ecrt) | Legacy bus; plant memory/WEBREQUEST deploys on 2G4C |
| **B** | `feature/iod-elc-kernel-transport` | `iod-elc` (kernel elc) **only** | Multi-domain, shadow, promote, topology — **no ecrt / no iod_sdo build** |
| **C** | `prod-client-zmq-fix` | humid / modbusd / dbd / persistd cw_client | Channel client ZMQ, setup REQ recovery, thin-client build |

**Cut of C:** from **A** (`prod-experimental-mqtt-fix`) at creation (2026-07-31).

**Archive only:** `humid-zmq-client-fix` — older humid REQ hang history; not a sync base.

## Branch B policy (elc-only)

On **B**, plant iod is **`iod-elc` only**:

- CMake: `BUILD_IOD_ELC=ON`; no `BUILD_LEGACY_IOD`, no `iod` / `iod_sdo` targets.
- Sources: no dual-path `#ifdef USE_KERNEL_ETHERCAT` / ecrt `#else` arms.
- Legacy IgH ecrt lives on **A** only. Do not reintroduce ecrt ifdefs on B “for porting.”

## Scope tags (commit messages)

| Tag | Land first | Port |
|-----|------------|------|
| `scope: bus-legacy` | **A only** | — (N/A on B) |
| `scope: bus-elc` | **B only** | — (N/A on A) |
| `scope: iod-core` | A or B | other of A/B same week (**semantic** port after B elc-only cleanup) |
| `scope: client-zmq` | C | A and B if shared sources |

Port line: `Port of <hash> from <branch>: <one line>`.

## Port matrix (summary)

| Surface | A | B | C |
|---------|---|---|---|
| ecrt pending-out / process-image TX | own | **N/A (removed)** | N/A |
| kernel promote / shadow / multi-domain WC | N/A | own | N/A |
| JSON / cJSON / WEBREQUEST / PROCSNAP / HEALTH | port | port | only if client binary links it |
| Channel **server** in plant iod | port | port | usually N/A |
| Channel **client** / ConnectionManager / cw_client | port from C | port from C | **own** |
| Thin client CMake (no ecat / optional modbus) | port from C | port from C | **own** |

## Verify (minimum)

- **A:** `iod_sdo` build; processing/idle check if loop changed.
- **B:** `iod-elc` build only; PROCSNAP quiet (`brk_out` / absorb) if wait/ready changed.
- **C:** build the client targets touched; deploy those binaries to HMI/aux hosts.

## Track E (channel / HMI)

Canonical development: **C**. Redeploy **clients** after client-zmq fixes. Port
overlapping monorepo files into A/B so plant checkouts do not rebuild stale
client surfaces.
