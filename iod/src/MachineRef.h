//
// Created by Martin Leadbeater on 1/10/2022.
//

#ifndef LATPROC_MACHINEREF_H
#define LATPROC_MACHINEREF_H

#include <string>

class MachineInterface;
class MachineInstance;

struct MachineRef {
public:
    static MachineRef *create(const std::string name, MachineInstance *machine,
                              MachineInterface *iface);
    MachineRef *ref() {
        ++refs;
        return this;
    }
    void release() {
        --refs;
        if (!refs) {
            delete this;
        }
    }
    std::ostream &operator<<(std::ostream &out) const;
    bool operator==(const MachineRef &other);

private:
    std::string key;
    std::string name;
    std::string interface_name;
    MachineInstance *item;
    MachineInterface *interface;
    int refs;
    MachineRef();
    MachineRef(const MachineRef &orig);
    MachineRef &operator=(const MachineRef &other);
};

std::ostream &operator<<(std::ostream &out, const MachineRef &m);

#endif //LATPROC_MACHINEREF_H
