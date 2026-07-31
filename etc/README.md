# Product config samples (generic Clockwork)

These are **defaults/samples shipped with the product**, not a plant checkout.

| File / dir | Role |
|------------|------|
| `iod.conf` | Sample debug flags for iod (`-c`) |
| `elc_topology.conf` | **Sample** bus topology for demos / first bring-up |
| `recipes/` | **Sample** CoE setup listings (e.g. servo PDO map) |

## Plant sites

Put **your** topology and recipes in the **plant tree** (e.g. `code/config/`), and
point the plant boot script at them:

```bash
export ELC_TOPOLOGY_CONFIG=/opt/latproc/code/config/elc_topology.conf
# ECSETUPRECIPE.recipe → /opt/latproc/code/config/recipes/….recipe.in
```

This machine’s service run script (`code/config/scripts/iod-elc.sh`) sets
`ELC_TOPOLOGY_CONFIG` to `code/config/elc_topology.conf` automatically.

## Quick start (generic / no plant tree)

```bash
# 1) elc transport — see TRANSPORT.md
# 2) build iod-elc under iod/build-elc
# 3) start with product helper + your CW sources:
./scripts/iod-elc.sh --name MYCELL /path/to/cw/lib /path/to/cw/config
# uses etc/elc_topology.conf unless ELC_TOPOLOGY_CONFIG is set
```

See also `TRANSPORT.md`, `scripts/iod-elc.sh`, `etc/recipes/README.md`.
