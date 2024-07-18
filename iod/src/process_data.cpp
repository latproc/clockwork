#include "process_data.h"
#include <sstream>
#include <cassert>
#include <iostream>
#include <string.h>

ProcessData::ProcessData(): data_size(0), process_data(0), process_mask(0), update_data(0), update_mask(0),
      min_io_index(0), max_io_index(0), app_process_mask(0)
      {
}

void ProcessData::setMinIOIndex(unsigned int new_val) {
    assert(min_io_index == 0); // other values untested
    min_io_index = new_val;
}

void ProcessData::setMaxIOIndex(unsigned int new_val) { max_io_index = new_val; }

uint8_t *ProcessData::getProcessData() { return process_data; }
uint32_t ProcessData::getProcessDataSize() { return max_io_index - min_io_index + 1; }

void ProcessData::setDataSize(size_t ds) {
    if (data_size == 0 || data_size != ds) {
        std::stringstream ss;
        ss << "setting process data size: " << data_size << " -> " << ds;
        std::cout << ss.str() << "\n";
        data_size = ds;
    }
}

void ProcessData::setProcessData(uint8_t *pd, size_t new_size) {
    if (process_data) {
        delete[] process_data;
    }
    assert("setting process data without setting the size first" && new_size == data_size);
    process_data = pd;
#if VERBOSE_DEBUG
    if (process_data) {
        DBG_ETHERCAT_PACKETS << "ecrt_domain_size: set process data (" << ecrt_domain_size(domain1)
                             << ") ";
        display(process_data, ecrt_domain_size(domain1));
        DBG_ETHERCAT_PACKETS << "\n";
    }
#endif
}

void ProcessData::setAppProcessMask(uint8_t *new_mask, size_t new_size) {
    if (app_process_mask) {
        delete app_process_mask;
    }
    assert("setting app process mask without setting the size first" && new_size == data_size);
    app_process_mask = new uint8_t[new_size];
    memcpy(app_process_mask, new_mask, new_size);
}

uint8_t *ProcessData::getProcessMask() { return app_process_mask; }

void ProcessData::setProcessMask(uint8_t *m, size_t new_size) {
    if (process_mask) {
        delete[] process_mask;
    }
    assert("setting process mask without setting the size first" && new_size == data_size);
    process_mask = m;
}

void ProcessData::setUpdateData(uint8_t *ud, size_t new_size) {
    if (update_data) {
        delete[] update_data;
    }
    assert("setting update data without setting the size first" && new_size == data_size);
    update_data = ud;
}
/*
void ProcessData::setUpdateMask (uint8_t *m){
    if (update_mask) { delete[] update_mask;
    update_mask = m;
}
*/
uint8_t *ProcessData::getUpdateData() { return update_data; }
uint8_t *ProcessData::getUpdateMask() { return update_mask; }

void ProcessData::reallocate_update_data_and_mask(size_t new_size) {
    unsigned int max = max_io_index;
    unsigned int min = min_io_index;
    assert(new_size >= (size_t)max - min + 1);
    if (!update_data) {
        update_data = new uint8_t[new_size];
    }
    if (!update_mask) {
        update_mask = new uint8_t[new_size];
    }
    data_size = new_size;
    memset(update_data, 0, new_size);
    memset(update_mask, 0, new_size);
}

