#include "LoadPropertiesAction.h"
#include "Logger.h"
#include "MachineInstance.h"

void load_properties_file(const std::string &filename, bool only_persistent);

Action *LoadPropertiesActionTemplate::factory(MachineInstance *mi) {
    return new LoadPropertiesAction(mi, *this);
}

std::ostream &LoadPropertiesAction::operator<<(std::ostream &out) const {
    return out << "Load Properties Action "
               << "\n";
}

Action::Status LoadPropertiesAction::run() {
    owner->start(this);
    std::string filename = values_file;
    if (machine && property_name.kind == Value::t_symbol) {
        const Value &val = machine->getValue(property_name.sValue);
        if (val.kind == Value::t_string) {
            filename = val.sValue;
        }
    }
    if (!filename.empty()) {
        load_properties_file(filename, false);
    }
    status = Complete;
    result_str = "OK";
    owner->stop(this);
    return status;
}

Action::Status LoadPropertiesAction::checkComplete() {
    status = Complete;
    result_str = "OK";
    return status;
}
