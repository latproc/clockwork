
#include <iostream>
#include "Action.h"

std::ostream &operator<<(std::ostream &out, Action::Status state) {
	switch(state) {
		case Action::New: return out << "New";
		case Action::Running: return out << "Running";
		case Action::Complete: return out << "Failed";
		case Action::Suspended: return out << "Suspended";
		case Action::NeedsRetry: return out << "NeedsRetry";
		default: ;
	}
	return out << "Unknown";
}

