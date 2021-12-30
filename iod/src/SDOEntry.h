/*
    Copyright (C) 2012 Martin Leadbeater, Michael O'Connor

    This file is part of Latproc

    Latproc is free software; you can redistribute it and/or
    modify it under the terms of the GNU General Public License
    as published by the Free Software Foundation; either version 2
    of the License, or (at your option) any later version.

    Latproc is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with Latproc; if not, write to the Free Software
    Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
*/

#ifndef __SDOEntry
#define __SDOEntry

#include <iostream>
#include <sys/types.h>

#ifndef EC_SIMULATOR
#include <ecrt.h>
#else
class ec_sdo_request_t;
#endif

class ECModule;
#ifdef USE_SDO
class SDOEntry {
  public:
    enum Operation { READ, WRITE };

    //  SDOEntry( std::string name, ec_sdo_request_t *);
    SDOEntry(std::string nam, uint16_t index, uint8_t subindex, const uint8_t *data, size_t size,
             uint8_t offset = 0)
#ifndef EC_SIMULATOR
        ; // implemented in ECInterface.cpp
#else
        : name(nam), module_(0), index_(0), subindex_(0), offset_(0), data_(0), size_(0),
          realtime_request(0), sync_done(false), error_count(0) {
    }
#endif
    ~SDOEntry();

    /* these are the clockwork-defined names for the module */
    void setModuleName(std::string name) { module_name = name; }
    const std::string &getModuleName() const { return module_name; }

    ECModule *getModule();
    void setModule(ECModule *);
    ec_sdo_request_t *prepareRequest(ECModule *module);
    ec_sdo_request_t *getRequest();

    static void resolveSDOModules();
    static SDOEntry *find(std::string name);

    const std::string &getName() const { return name; }
    bool ready() { return sync_done; }
    size_t getSize() { return size_; }
    uint16_t getIndex() { return index_; }
    uint8_t getSubindex() { return subindex_; }
    uint8_t getOffset() { return offset_; }
    unsigned int getErrorCount() { return error_count; }

    void setData(bool val);
    void setData(uint8_t val);
    void setData(int8_t val);
    void setData(uint16_t val);
    void setData(int16_t val);
    void setData(uint32_t val);
    void setData(int32_t val);

    void syncValue();  // read the sdo data and copy it into clockwork
    Value readValue(); // return the current sdo data value

    Operation operation() const { return op; }
    void setOperation(Operation which) { op = which; }

    void setMachineInstance(MachineInstance *m) { machine_instance = m; }
    MachineInstance *machineInstance() { return machine_instance; }

    void failure();
    void success();
    bool ok();

  private:
    SDOEntry(const SDOEntry &);
    SDOEntry &operator=(const SDOEntry &);

    std::string name;
    ECModule *module_;
    uint16_t index_;
    uint8_t subindex_;
    uint8_t offset_;
    uint8_t *data_;
    size_t size_;
    ec_sdo_request_t *realtime_request;
    bool sync_done;           // set value will not change the io until the entre has been synced
    unsigned int error_count; // number of times this request has failed so far
    std::string module_name;
    bool readonly;
    Operation op;
    MachineInstance *machine_instance;
};
#endif //USE_SDO

#endif
