#include <list>
#include <algorithm>
#include "symboltable.h"
#include "Parameter.h"
#include "MachineInstance.h"
#include "MachineDetails.h"

MachineDetails::MachineDetails(const char *nam, const char *cls, std::list<Parameter> &params,
			   const char *sf, int sl, SymbolTable &props, MachineInstance::InstanceType kind)
: machine_name(nam), machine_class(cls), source_file(sf), source_line(sl),
instance_type(kind)
{
	std::copy(parameters.begin(), parameters.end(),  back_inserter(parameters));

	SymbolTableConstIterator iter = props.begin();
	while (iter != props.end()) {
		const std::pair<std::string, Value> &p = *iter;
		properties.push_back(p);
		iter++;
	}
}

MachineInstance *MachineDetails::instantiate() {
	MachineInstance *machine = MachineInstanceFactory::create(machine_name.c_str(),
		machine_class.c_str(), instance_type);
	machine->setDefinitionLocation(source_file.c_str(), source_line);
	std::copy(parameters.begin(), parameters.end(),  back_inserter(machine->parameters));
	machine->setProperties(properties);
	return machine;
}
