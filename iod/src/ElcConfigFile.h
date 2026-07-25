/*
 * Load a declarative elc topology/config file (same format as tools/elc_config)
 * into an open KernelEthercatBus, and rebuild ECModule PDO entry tables.
 */

#ifndef ELC_CONFIG_FILE_H
#define ELC_CONFIG_FILE_H

#include <string>

class KernelEthercatBus;

// Apply config file to an open bus (config_begin..domain_create).
// Returns 0 on success.
int elcApplyConfigFile(KernelEthercatBus *bus, const char *path);

// After domain is created, fill each ECModule at bus positions with
// pdo_entries / entry_details / offsets / bit_positions / syncs from the
// same file, resolving offsets via elc_get_entry_offset.
int elcPopulateModulesFromConfigFile(KernelEthercatBus *bus, const char *path);

// Default topology for this machine (34 slaves).
const char *elcDefaultTopologyConfigPath();

#endif
