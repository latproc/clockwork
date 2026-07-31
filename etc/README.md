# Config defaults (install / site)

| File / dir | Role |
|------------|------|
| `iod.conf` | Debug groups for iod (`-c`) |
| `elc_topology.conf` | Bus topology (slaves, domains, PDOs). Override with `ELC_TOPOLOGY_CONFIG`. |
| `all34_captured_topology.conf` | Symlink → `elc_topology.conf` (old name) |
| `recipes/` | Sample CoE startup listings (servo PDO map, accel, …) for `ECSETUPRECIPE` or `--setup-recipe` |
| `modbus_addressing.conf` | Optional site symlink for Modbus addressing |

## Quick start (EtherCAT plant)

1. Install kernel elc + lib (see `TRANSPORT.md` / `/opt/etherlab-cyclic-kmod`).
2. Review/edit **`etc/elc_topology.conf`** for your bus (or point `ELC_TOPOLOGY_CONFIG` at a site copy).
3. Build/install **`iod-elc`** (`iod/build-elc`).
4. Start with the product helper, passing **your** Clockwork source dirs:

```bash
# from latproc root
./scripts/iod-elc.sh --name MYCELL \
  /path/to/your/cw/lib \
  /path/to/your/cw/config
```

Optional env: `IOD_PERSIST`, `IOD_MODBUS_MAP`, `IOD_PLUGIN_DIR`, `IOD_STREAM_FILTER=1`.

5. Servo CoE setup (PDO map in PREOP): either  
   - Clockwork `ECSETUPRECIPE` with `recipe: "…/etc/recipes/ed3l_velocity_pdo.recipe.in"`, or  
   - CLI: `iod-elc --setup-recipe etc/recipes/ed3l_velocity_pdo.recipe.in --setup-domain 2 …`

Site service units (daemontools/systemd) should call `scripts/iod-elc.sh` with plant paths — plant LPC is not under this product tree.
