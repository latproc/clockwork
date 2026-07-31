#!/bin/sh
# Not used on this branch (feature/iod-elc-kernel-transport).
#
# Kernel + userland for plant iod-elc come from the elc transport tree
# (typically /opt/etherlab-cyclic-kmod): DKMS module, libelcethercat, tools.
# See TRANSPORT.md.
#
# Building classic IgH EtherLab for iod_sdo is only relevant on legacy
# branches, not this one.

echo "scripts/etherlab.sh: IgH EtherLab install is not used on this branch." >&2
echo "See TRANSPORT.md and the elc transport source tree." >&2
exit 1
