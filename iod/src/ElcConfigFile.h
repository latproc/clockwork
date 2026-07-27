/*
 * Load a declarative elc topology/config file (same format as tools/elc_config)
 * into an open KernelEthercatBus, and rebuild ECModule PDO entry tables.
 */

#ifndef ELC_CONFIG_FILE_H
#define ELC_CONFIG_FILE_H

#include <cstdint>
#include <string>
#include <vector>

class KernelEthercatBus;

// Apply config file to an open bus (config_begin..domain_create).
// Returns 0 on success. On multi-domain configs, domain_ids_out (if non-null)
// is filled with domain_config_id in declaration order (first = primary).
int elcApplyConfigFile(KernelEthercatBus *bus, const char *path,
                       std::vector<uint32_t> *domain_ids_out = nullptr);

// After domain is created, fill each ECModule at bus positions with
// pdo_entries / entry_details / offsets / bit_positions / syncs from the
// same file, resolving offsets via elc_get_entry_offset.
int elcPopulateModulesFromConfigFile(KernelEthercatBus *bus, const char *path);

// Default topology for this machine (34 slaves).
const char *elcDefaultTopologyConfigPath();

// Positions of slaves assigned to domain_config_id (topology domain_slave lines).
// Returns 0 on success (empty out if domain has no slaves). Negative on parse error.
int elcPositionsForDomain(const char *path, uint32_t domain_config_id,
                          std::vector<uint16_t> *positions_out);

#endif
