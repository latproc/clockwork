#!/bin/sh
# Not used on this branch (feature/iod-elc-kernel-transport).
#
# Plant EtherCAT is the kernel elc transport (elc_ethercat + libelcethercat +
# iod-elc). See the repo root TRANSPORT.md for module load, userland install,
# and rates. Site-specific service wrappers live outside this generic tree.
#
# IgH master helpers (ec_master / /dev/EtherCAT0) belong only on legacy
# branches that still build iod_sdo.

echo "scripts/ethercat.sh: IgH ec_master helper is not used on this branch." >&2
echo "Use: scripts/iod-elc.sh  (see TRANSPORT.md for elc_ethercat / iod-elc)." >&2
exit 1
