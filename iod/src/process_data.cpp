#include "process_data.h"
#include <sstream>
#include <cassert>
#include <iostream>
#include <string.h>

ProcessData::ProcessData(): data_size(0), process_data(0), process_mask(0), update_data(0), update_mask(0),
      min_io_index(0), max_io_index(0), app_process_mask(0)
      {
}

void ProcessData::clear() {
    process_data.clear();
    process_mask.clear();
    update_data.clear();
}

void ProcessData::setMinIOIndex(unsigned int new_val) {
    assert(min_io_index == 0); // other values untested
    min_io_index = new_val;
}

void ProcessData::setMaxIOIndex(unsigned int new_val) { max_io_index = new_val; }

std::vector<uint8_t> &ProcessData::getProcessData() { return process_data; }
uint32_t ProcessData::getProcessDataSize() { return max_io_index - min_io_index + 1; }

std::vector<uint8_t> &ProcessData::getDefaultData() { return default_data; }
std::vector<uint8_t> &ProcessData::getDefaultMask() { return default_mask; }

void ProcessData::setDataSize(size_t ds) {
    if (data_size == 0 || data_size != ds) {
        std::stringstream ss;
        ss << "setting process data size: " << data_size << " -> " << ds;
        std::cout << ss.str() << "\n";
        data_size = ds;
    }
}

void ProcessData::setProcessData(uint8_t *pd, size_t new_size) {
    process_data.clear();
    process_data.resize(new_size);
    process_data.insert(process_data.begin(), pd, pd + new_size);
}

void ProcessData::setProcessMask(uint8_t *new_mask, size_t new_size) {
    process_mask.clear();
    process_mask.resize(new_size);
    process_mask.insert(process_mask.begin(), new_mask, new_mask + new_size);
}

std::vector<uint8_t> &ProcessData::getProcessMask() { return process_mask; }

void ProcessData::setUpdateData(uint8_t *pd, size_t new_size) {
    process_data.clear();
    process_data.resize(new_size);
    process_data.insert(process_data.begin(), pd, pd + new_size);
}

void ProcessData::setUpdateMask(uint8_t *new_mask, size_t new_size) {
    process_mask.clear();
    process_mask.resize(new_size);
    process_mask.insert(process_mask.begin(), new_mask, new_mask + new_size);
}

/*
void ProcessData::setUpdateMask (uint8_t *m){
    if (update_mask) { delete[] update_mask;
    update_mask = m;
}
*/
std::vector<uint8_t> &ProcessData::getUpdateData() { return update_data; }
std::vector<uint8_t> &ProcessData::getUpdateMask() { return update_mask; }

void ProcessData::reallocate_update_data_and_mask(size_t new_size) {
    unsigned int max = max_io_index;
    unsigned int min = min_io_index;
    assert(new_size >= (size_t)max - min + 1);
    update_data.resize(new_size);
    update_data.resize(new_size);
}

