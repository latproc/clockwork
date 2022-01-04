#include "ExportState.h"

ExportState *ExportState::_instance = 0;

ExportState *ExportState::instance() {
    if (_instance) {
        return _instance;
    }
    _instance = new ExportState;
    return _instance;
}
