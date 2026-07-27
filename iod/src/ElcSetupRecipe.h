/*
 * Apply elc ordered setup recipes (same format as elc_sdo recipe files).
 *
 * Sources (both optional; combined):
 *   1) Plant machines of class ECSETUPRECIPE (discovered like STARTUP)
 *   2) CLI --setup-recipe / --setup-positions / --setup-domain / --setup-product / --setup-vendor
 *
 * No vendor/product hardcoding in iod — plant or CLI supplies path and targets.
 * Domains come from topology; recipes only select which slaves get which SDO batch.
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

/** Queue a position for re-apply (servo/power return). Thread-safe. */
void requestReapply(uint16_t position);

/** Drain queue (one or more batches). Safe from ecat userspace loop. */
void processPending(KernelEthercatBus *bus);

} // namespace ElcSetupRecipe

#endif
