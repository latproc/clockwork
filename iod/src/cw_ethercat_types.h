/*
 * Clockwork process-data / bus-side types.
 *
 * Plant iod uses the kernel elc transport (libelcethercat). These structs describe
 * PDO/SM layout and slave identity for Clockwork and ESI XML mapping.
 */

#ifndef CW_ETHERCAT_TYPES_H
#define CW_ETHERCAT_TYPES_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef EC_MAX_STRING_LENGTH
#define EC_MAX_STRING_LENGTH 64
#endif
#ifndef EC_MAX_PORTS
#define EC_MAX_PORTS 4
#endif

typedef enum {
    EC_DIR_INVALID = 0,
    EC_DIR_OUTPUT,
    EC_DIR_INPUT,
    EC_DIR_COUNT
} ec_direction_t;

typedef enum {
    EC_WD_DEFAULT = 0,
    EC_WD_ENABLE,
    EC_WD_DISABLE
} ec_watchdog_mode_t;

typedef enum {
    EC_WC_ZERO = 0,
    EC_WC_INCOMPLETE,
    EC_WC_COMPLETE
} ec_wc_state_t;

typedef struct {
    uint16_t index;
    uint8_t subindex;
    uint8_t bit_length;
} ec_pdo_entry_info_t;

typedef struct {
    uint16_t index;
    unsigned int n_entries;
    ec_pdo_entry_info_t const *entries;
} ec_pdo_info_t;

typedef struct {
    uint8_t index;
    ec_direction_t dir;
    unsigned int n_pdos;
    ec_pdo_info_t const *pdos;
    ec_watchdog_mode_t watchdog_mode;
} ec_sync_info_t;

typedef struct {
    unsigned int slaves_responding;
    unsigned int al_states : 4;
    unsigned int link_up : 1;
} ec_master_state_t;

typedef struct {
    unsigned int working_counter;
    ec_wc_state_t wc_state;
    unsigned int redundancy_active;
} ec_domain_state_t;

typedef struct {
    uint8_t online;
    uint8_t operational;
    uint8_t al_state;
} ec_slave_config_state_t;

/** Placeholder handles (plant elc path keeps these null; simulator may allocate). */
typedef struct ec_master {
    int unused;
} ec_master_t;
typedef struct ec_domain {
    int unused;
} ec_domain_t;
typedef struct ec_slave_config {
    int unused;
} ec_slave_config_t;

typedef struct {
    uint16_t position;
    uint32_t vendor_id;
    uint32_t product_code;
    uint32_t revision_number;
    uint32_t serial_number;
    uint16_t alias;
    int16_t current_on_ebus;
    uint8_t al_state;
    uint8_t error_flag;
    uint8_t sync_count;
    uint16_t sdo_count;
    char name[EC_MAX_STRING_LENGTH];
} ec_slave_info_t;

/* Host little-endian process-image access (plant CPUs). */
#define EC_READ_BIT(DATA, POS) ((*((uint8_t *)(DATA)) >> (POS)) & 0x01)
#define EC_WRITE_BIT(DATA, POS, VAL)                                                                       \
    do {                                                                                                   \
        if (VAL)                                                                                           \
            *((uint8_t *)(DATA)) |= (uint8_t)(1u << (POS));                                                \
        else                                                                                               \
            *((uint8_t *)(DATA)) &= (uint8_t) ~(1u << (POS));                                              \
    } while (0)

#define EC_READ_U8(DATA) ((uint8_t) * ((uint8_t *)(DATA)))
#define EC_WRITE_U8(DATA, VAL)                                                                             \
    do {                                                                                                   \
        *((uint8_t *)(DATA)) = (uint8_t)(VAL);                                                             \
    } while (0)
#define EC_WRITE_S8(DATA, VAL) EC_WRITE_U8(DATA, VAL)

#define EC_READ_U16(DATA) ((uint16_t) * ((uint16_t *)(DATA)))
#define EC_WRITE_U16(DATA, VAL)                                                                            \
    do {                                                                                                   \
        *((uint16_t *)(DATA)) = (uint16_t)(VAL);                                                           \
    } while (0)
#define EC_WRITE_S16(DATA, VAL) EC_WRITE_U16(DATA, VAL)

#define EC_READ_U32(DATA) ((uint32_t) * ((uint32_t *)(DATA)))
#define EC_WRITE_U32(DATA, VAL)                                                                            \
    do {                                                                                                   \
        *((uint32_t *)(DATA)) = (uint32_t)(VAL);                                                           \
    } while (0)
#define EC_WRITE_S32(DATA, VAL) EC_WRITE_U32(DATA, VAL)

#define EC_READ_U64(DATA) ((uint64_t) * ((uint64_t *)(DATA)))
#define EC_WRITE_U64(DATA, VAL)                                                                            \
    do {                                                                                                   \
        *((uint64_t *)(DATA)) = (uint64_t)(VAL);                                                           \
    } while (0)

#ifdef __cplusplus
}
#endif

#endif /* CW_ETHERCAT_TYPES_H */
