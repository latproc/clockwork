#pragma once

#include <stdlib.h>
#include <stdint.h>
#include <vector>

class ProcessData {
public:
    ProcessData();

    void clear();

    void setDataSize(size_t);
    void setProcessData(uint8_t *pd, size_t new_size);
    void setProcessData(std::vector<uint8_t> &pd);
    std::vector<uint8_t> &getProcessData();

    void setProcessMask(uint8_t *new_mask, size_t new_size);
    void setProcessMask(std::vector<uint8_t> &new_mask);
    std::vector<uint8_t> &getProcessMask();

    void
    setMaxIOIndex(unsigned int new_max); // min index into user required process data (must be zero)
    void setMinIOIndex(unsigned int new_min); // max index into user required process data
    uint32_t getProcessDataSize();            // returns process data size of user selected data set

    void setUpdateData(uint8_t *pd, size_t new_size);
    void setUpdateData(std::vector<uint8_t> &ud);
    void setUpdateMask(uint8_t *pd, size_t new_size);
    void setUpdateMask(std::vector<uint8_t> &m);
    std::vector<uint8_t> &getUpdateData();
    std::vector<uint8_t> &getUpdateMask();
    std::vector<uint8_t> &getDefaultData();
    std::vector<uint8_t> &getDefaultMask();
    void setDefaultData(std::vector<uint8_t> &pd);
    void setDefaultMask(std::vector<uint8_t> &pd);

    void reallocate_update_data_and_mask(size_t new_size);

    size_t data_size;
    size_t mask_size;
    std::vector<uint8_t> process_data;
    std::vector<uint8_t> process_mask;
    std::vector<uint8_t> update_data;
    std::vector<uint8_t> update_mask;
    std::vector<uint8_t> default_data;
    std::vector<uint8_t> default_mask;
    unsigned int
        min_io_index; // first byte of the process data needed by the user (must be zero currently)
    unsigned int max_io_index; // last byte of the process data needed by the user
    std::vector<uint8_t> *app_process_mask; // copy of user provided mask data
};

class IOUpdate {
  public:
    void clear();
    uint64_t global_clock() const;
    void setGlobalClock(uint64_t clock);
    uint32_t data_size() const;
    //void setSize(uint32_t sz);

    const uint8_t *data() const;
    void setData(uint8_t *dt, size_t size);
    void setData(std::vector<uint8_t> &dt);

    const uint8_t *mask() const;
    void setMask(uint8_t *ms, size_t size);
    void setMask(std::vector<uint8_t> &dt);

  private:
    uint64_t global_clock_ = 0;
    std::vector<uint8_t> data_;// shared pointer to process data
    std::vector<uint8_t> mask_;// allocated pointer to current mask
};


