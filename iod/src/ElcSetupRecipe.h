/*
 * Apply elc ordered setup recipes (same format as elc_sdo recipe files).
 * Used for ED3L velocity PDO map at cold start and after servo power return.
 */
#ifndef ELC_SETUP_RECIPE_H
#define ELC_SETUP_RECIPE_H

#include <cstdint>
#include <vector>

class KernelEthercatBus;
class ECModule;

namespace ElcSetupRecipe {

/** Default path: plant config, then iod/recipes. */
const char *defaultEd3lRecipePath();

/** Summa/Estun ED3L product code used on this plant. */
bool isEd3lProduct(uint32_t product_code);
bool isEd3lModule(const ECModule *m);

/**
 * Expand recipe.in (POS placeholder) for each position and run one
 * setup_begin / add / apply batch. Blocking; do not call from hard RT.
 * Returns 0 on success.
 */
int applyForPositions(KernelEthercatBus *bus, const char *recipe_path,
                      const std::vector<uint16_t> &positions);

/** Queue a position for re-apply (servo return). Thread-safe. */
void requestReapply(uint16_t position);

/** Drain queue (one batch). Safe to call from ecat userspace loop. */
void processPending(KernelEthercatBus *bus);

/** Apply recipe for every ED3L currently on the bus (listSlaves / modules). */
int applyForAllEd3lOnBus(KernelEthercatBus *bus, const char *recipe_path = nullptr);

} // namespace ElcSetupRecipe

#endif
