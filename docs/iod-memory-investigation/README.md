# IOD memory investigation handoff pack

Portable package from **2G4C-120** (Jul 2026) for another latproc/iod machine or
lab host. Includes findings, ownership patches, low-overhead monitor, and an
install script safe for **Grok / agent** use.

## Quick start (target machine)

```bash
cd /opt/latproc          # or your LATPROC root
tar xzf iod-memory-investigation-*.tar.gz
cd iod-memory-investigation-*   # actual dir name from tar
chmod +x install.sh
./install.sh --monitor
./scripts/iod_memory_status.sh
# optional after iod is running:
./scripts/iod_enable_memsnapshot.sh
# prefer DEBUG_MEMSNAPSHOT in etc/iod.conf for restarts
# MEMSNAPSHOT file (iod-elc): sampling/iod-memory/memsnapshot.log
# full verbose without restart: scripts/iod_verbose.sh on [ttl] → /tmp/iod.log
```

Full agent checklist: **`GROK_INSTALL.md`**. Also `/opt/latproc/TRANSPORT.md`.

## What is “fixed” vs still open

| Area | Status |
|------|--------|
| Idle / overnight cJSON drip (ITEM DEFAULT null, scalars) | **Fixed** in patches 01–02; plant night was flat |
| Curl Request vs HTTP 200 “gap” | **Not a bug** (sampler property clears) |
| Production-day live cJSON + WEBREQUEST arenas | **Open** — offline work (patch 03 is a larger step) |

Long-run evidence and offline plan: `docs/IOD_WEBREQUEST_MEMORY_GROWTH_20260721.md`.

## Contents

| Path | Purpose |
|------|---------|
| `install.sh` | Docs + tools; optional `--monitor`, `--apply-patches` |
| `docs/` | Handoffs + methodology |
| `tools/iod_memory_monitor.sh` | RSS/VSZ CSV + pmap sampling |
| `patches/01–03` | Ownership / WEBREQUEST patches (apply only after dry-run) |
| `scripts/` | Status + MEMSNAPSHOT enable |

## Location on this plant

```text
(Optional plant-local copies, if present on this host — not product source:)
site llm-rules/iod-memory-investigation/
site llm-rules/iod-memory-investigation-*.tar.gz
```

Copy the tarball with `scp`, USB, or internal file share. No secrets required.

## Environment overrides

```bash
export LATPROC=/opt/latproc
export IOD_MEMORY_IOD_NAME=2GRAB          # optional --name filter
export IOD_MEMORY_IOD_PID=12345           # pin iod PID
export IOD_MEMORY_OUTPUT_DIR=/opt/latproc/sampling/iod-memory
```
