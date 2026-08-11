/*
 * Apply elc ordered setup recipes (same format as elc_sdo recipe files).
 *
 * Sources (both optional; combined):
 *   1) Plant machines of class ECSETUPRECIPE (discovered like STARTUP)
 *   2) CLI --setup-recipe / --setup-positions / --setup-domain / --setup-product / --setup-vendor
 *
 * Sample recipe files ship under etc/recipes/ (e.g. ed3l_velocity_pdo.recipe.in).
 * Point ECSETUPRECIPE.recipe or --setup-recipe at those paths (or site copies).
 *
 * No vendor/product hardcoding in iod — plant or CLI supplies path and targets.
 * Domains come from topology (etc/elc_topology.conf); recipes only select slaves.
 */
#ifndef ELC_SETUP_RECIPE_H
#define ELC_SETUP_RECIPE_H

#include <cstdint>
#include <vector>

class KernelEthercatBus;
class ECModule;

namespace ElcSetupRecipe {

/**
 * Expand recipe (POS and/or fixed positions) for the given bus positions and
 * run one setup_begin / add / apply batch. Blocking; not for hard RT.
 * Returns 0 on success.
 */
int applyForPositions(KernelEthercatBus *bus, const char *recipe_path,
                      const std::vector<uint16_t> &positions);

/**
 * Discover ECSETUPRECIPE machines + CLI entries; resolve targets; apply each
 * as its own batch (failure on one does not skip later recipes).
 * Call after topology is loaded, before activate.
 */
int applyAllConfigured(KernelEthercatBus *bus);

/** True if any machine/CLI recipe wants re-apply for this bus position. */
bool positionWantsReapply(uint16_t position);

/**
 * Queue a position for re-apply after offline→online (servo/power return).
 * Thread-safe. Debounced + retried in processPending until CoE succeeds or
 * max attempts / max age. Safe to call on every online/PREOP edge.
 */
void requestReapply(uint16_t position);

/**
 * Non-blocking: record bus and wake the reapply worker. Safe from sendUpdates /
 * ecat RT path. Blocking SETUP_APPLY / setup-hold must not run there.
 */
void scheduleProcessPending(KernelEthercatBus *bus);

/**
 * Blocking progress of pending re-applies (worker thread only).
 * Does not setup-hold or SDO-apply while a position is missing/offline
 * (waiting_device). Once visible: PDO map CoE in PREOP/SAFEOP. With
 * ELC_CAP_SETUP_HOLD: hold → apply → release. Without CAP: wait PREOP window.
 *
 * Must not call MachineInstance::setValue / Channel publish from this thread —
 * status and output-default side effects are queued for pollFromEcatThread().
 */
void processPending(KernelEthercatBus *bus);

/**
 * Apply deferred ECSETUPRECIPE status property updates and any pending
 * reapplyOutputDefaults(). Call only from the ecat / setup path (not the
 * reapply worker). Safe while g_setup_mailbox_busy is false.
 */
void pollFromEcatThread();

} // namespace ElcSetupRecipe

#endif
