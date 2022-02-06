#include "ResetAction.h"
#include "Logger.h"
#include "MachineInstance.h"

void disable_all_machines();
void enable_all_machines();

Action *ResetActionTemplate::factory(MachineInstance *mi) { return new ResetAction(mi); }

std::ostream &ResetAction::operator<<(std::ostream &out) const {
    return out << "Reset Action " << "\n";
}

Action::Status ResetAction::run() {
    owner->start(this);
    disable_all_machines();
    enable_all_machines();

    status = Complete;
    result_str = "OK";
    owner->stop(this);
    return status;
}

Action::Status ResetAction::checkComplete() {
    status = Complete;
    result_str = "OK";
    return status;
}
