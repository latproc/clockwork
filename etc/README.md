# Product config samples (generic Clockwork)

These are **defaults/samples shipped with the product**, not a plant checkout.

| File / dir | Role |
|------------|------|
| `iod.conf` | Sample debug flags for iod (`-c`) |
| `elc_topology.conf` | Empty placeholder; never a default runtime topology |
| `all34_captured_topology.conf` | Explicit 1G2C/34-slave example fixture only |
| `recipes/` | **Sample** CoE setup listings (e.g. servo PDO map) |

## Plant sites

Put **your** topology and recipes in the **plant tree** (e.g. `code/config/`),
and point the plant boot script at them:

```bash
export ELC_TOPOLOGY_CONFIG=/opt/latproc/code/config/elc_topology.conf
# ECSETUPRECIPE.recipe → /opt/latproc/code/config/recipes/….recipe.in
```

Generate a topology from the target machine’s captured EtherCAT/PDO map, review
the slave identities, Sync Managers, PDOs, entries, and domain assignments, then
save the reviewed result as `code/config/elc_topology.conf`. Do not copy
`all34_captured_topology.conf` unless the target is the matching 34-slave
example plant.

The 4C04 service run script sets `ELC_TOPOLOGY_CONFIG` to
`code/config/elc_topology.conf` automatically. If that file is absent or empty,
startup must stop with a clear configuration error.

## Quick start (generic / no plant tree)

```bash
# 1) elc transport — see TRANSPORT.md
# 2) build iod-elc under iod/build-elc
# 3) start with product helper + your CW sources:
./scripts/iod-elc.sh --name MYCELL /path/to/cw/lib /path/to/cw/config
# no topology is selected unless ELC_TOPOLOGY_CONFIG is set
```

See also `TRANSPORT.md`, `scripts/iod-elc.sh`, `etc/recipes/README.md`.
