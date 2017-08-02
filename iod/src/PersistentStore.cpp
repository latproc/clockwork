#include <sys/time.h>
#include <string.h>
#include <boost/foreach.hpp>
#include <errno.h>
#include <iomanip>
#include <utility>
#include <string>
#include <iostream>
#include <map>
#include <sstream>
#include <fstream>
#include "value.h"
#include "PersistentStore.h"
#include "regular_expressions.h"


void PersistentStore::insert(std::string machine, std::string key, Value value) {

	std::map<std::string, std::map<std::string, Value> >::iterator found = init_values.find(machine);
	if (found == init_values.end()) {
		std::map<std::string, Value> entry;
		entry[key] = value;
		init_values[machine] = entry;
	}
	else {
		std::map<std::string, Value> &entry = (*found).second;
		entry[key] = value;
	}
	is_dirty = true;
}

void PersistentStore::load() {
	// load the store into a map
	//typedef std::pair<std::string, Value> PropertyPair;
	std::ifstream store(file_name.c_str());
	if (!store.is_open()) return;
	char buf[400];
	std::cout << std::fixed;
	while (store.getline(buf, 400, '\n')) {
		char value_buf[400];
		std::istringstream in(buf);
		std::string name, property, value_str;
		in >> name >> property;
		in.get(value_buf, 400, '\n');
		char *vp = value_buf; 
		while (*vp == ' ') ++vp; // skip space delimiter
		size_t n = strlen(vp);
		Value::Kind kind = Value::t_symbol;
		if (vp[0] == '"' && n>2 && vp[n-1] == '"')
		{
			vp[n-1] = 0;
			value_str = vp+1;
			kind = Value::t_string;
		}
		else
			value_str = vp;

		long i_value;
		double d_value;
		char *endp;
		i_value = strtol(value_str.c_str(), &endp, 10);
		if (*endp == 0) {
			insert(name, property, i_value);
		}
		else {
			d_value = strtod(value_str.c_str(), &endp);
			if (*endp == 0) {
					insert(name, property, d_value);
			}
			else {
				insert(name, property, Value(value_str.c_str(), kind).asString());
				std::cout << name << "." << property << ":" << value_str << "\n";
			}
		}
	}
	store.close();
	is_dirty = false;
}

/* split prop into name, property */
void PersistentStore::split(std::string &name, std::string& prop) const {
	name = prop;
	size_t pos = name.rfind('.');
	name.erase(pos);
	prop = prop.substr(prop.rfind('.') + 1);
}

std::ostream &PersistentStore::operator<<(std::ostream &out) const {
	std::pair<std::string, std::map<std::string, Value> >prop;
	int result = 1;
	const char *symbol_pattern = "^[\"]{0,1}[A-Za-z][A-Za-z0-9_.]*[\"]{0,1}$";
	const char *number_pattern = "^[-]{0,1}[0-9]+$";
	rexp_info *sym_info = create_pattern(symbol_pattern);
	if (sym_info->compilation_result != 0) {
		fprintf(stderr, "failed to compile regexp /%s/\n", symbol_pattern);
		release_pattern(sym_info);
		sym_info = 0;
	}
	rexp_info *num_info = create_pattern(number_pattern);
	if (num_info->compilation_result != 0) {
		fprintf(stderr, "failed to compile regexp /%s/\n", number_pattern);
		release_pattern(num_info);
		num_info = 0;
	}
	BOOST_FOREACH(prop, init_values) {
		std::map<std::string, Value> &entries(prop.second);
		PersistentStore::PropertyPair entry;
		BOOST_FOREACH(entry, entries) {
			if (entry.second.kind == Value::t_string) {
				//std::cout << prop.first << " " << entry.first << " " << entry.second.quoted() << " string\n";
				out << prop.first << " " << entry.first << " " << entry.second.quoted() << "\n";
			}
			else if (entry.second.kind == Value::t_bool
					 || entry.second.kind == Value::t_integer 
					 || entry.second.kind == Value::t_float
					) {
				out << prop.first << " " << entry.first << " " << entry.second << "\n";
			}
			else if (sym_info && execute_pattern(sym_info, entry.second.asString().c_str()) == 0) {
				//std::cout << prop.first << " " << entry.first << " " << entry.second.quoted() << " symbol pattern\n";
				out << prop.first << " " << entry.first << " " << entry.second << "\n";
			}
			else if (num_info && execute_pattern(num_info, entry.second.asString().c_str()) == 0) {
				//std::cout << prop.first << " " << entry.first << " " << entry.second.quoted() << " number pattern\n";
				out << prop.first << " " << entry.first << " " << entry.second << "\n";
			}
			else {
				//std::cout << prop.first << " " << entry.first << " " << entry.second  << " string (pattern)\n";
				out << prop.first << " " << entry.first << " " << entry.second.quoted() << "\n";
			}
		}
	}
	
	if (num_info) release_pattern(num_info);
	if (sym_info) release_pattern(sym_info);
	return out;
}

std::ostream &operator<<(std::ostream &out, const PersistentStore &store) {
	return store.operator<<(out);
}

void PersistentStore::save() {
	std::stringstream ss;
	struct timeval now;
	gettimeofday(&now, 0);
	ss << "persist_scratch_" <<now.tv_usec;
	std::string scratchfile = ss.str();
	std::ofstream out(scratchfile.c_str());
	if (!out) {
		std::cerr << "failed to open " << scratchfile << " for write\n";
	}
	else {
		try {
			out << *this << std::flush;
			out.close();
		}
		catch (std::exception e) {
			std::cerr << "exception " << e.what() << " writing data store\n";
		}
	}
	if (rename(scratchfile.c_str(), file_name.c_str())) {
		std::cerr << "rename: " << strerror(errno) << "\n";
		if (unlink(scratchfile.c_str())) {
			std::cerr << "unlink: " << strerror(errno) << "\n";
		}
	}

}
