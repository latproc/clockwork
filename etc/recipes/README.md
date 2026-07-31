# Sample CoE setup recipes (PREOP)

Format matches `elc_sdo` ordered setup files:

```text
sequence  position  type  index  subindex  value
```

`POS` in the position column expands per target slave.

| File | Typical use |
|------|-------------|
| `ed3l_velocity_pdo.recipe.in` | CiA402 velocity PDO map + profile defaults |
| `profile_accel_800k.recipe.in` | Optional higher accel/decel group |

**How to apply**

1. Clockwork machine class `ECSETUPRECIPE` (see plant LPC / docs):

   ```text
   M_ServoPdoSetup ECSETUPRECIPE (
     recipe: "/opt/latproc/etc/recipes/ed3l_velocity_pdo.recipe.in",
     domain_id: 2,
     reapply: true
   );
   ```

2. Or CLI on `iod-elc`:

   ```bash
   --setup-recipe /opt/latproc/etc/recipes/ed3l_velocity_pdo.recipe.in \
   --setup-domain 2
   ```

Copy and edit for your drives; do not hardcode vendor IDs in iod C++.
