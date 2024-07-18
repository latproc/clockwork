#pragma once

#include <stdlib.h>
#include <stdint.h>

class ProcessData {
public:
    ProcessData();

    void setDataSize(size_t);
    void setProcessData(uint8_t *pd, size_t new_size);
    uint8_t *getProcessData();

    void setProcessMask(uint8_t *new_mask, size_t new_size);
    uint8_t *getProcessMask();
    void setAppProcessMask(uint8_t *new_mask, size_t new_size);

    void
    setMaxIOIndex(unsigned int new_max); // min index into user required process data (must be zero)
    void setMinIOIndex(unsigned int new_min); // max index into user required process data
    uint32_t getProcessDataSize();            // returns process data size of user selected data set

    void setUpdateData(uint8_t *ud, size_t new_size);
    void setUpdateMask(uint8_t *m, size_t new_size);
    uint8_t *getUpdateData();
    uint8_t *getUpdateMask();

    void reallocate_update_data_and_mask(size_t new_size);

    size_t data_size;
    size_t mask_size;
    uint8_t *process_data;
    uint8_t *process_mask;
    uint8_t *update_data;
    uint8_t *update_mask;
    unsigned int
        min_io_index; // first byte of the process data needed by the user (must be zero currently)
    unsigned int max_io_index; // last byte of the process data needed by the user
    uint8_t *app_process_mask; // copy of user provided mask data
};


