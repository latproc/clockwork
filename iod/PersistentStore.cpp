#include <sys/time.h>
#include <string.h>
#include <boost/foreach.hpp>
#include <errno.h>

#include <utility>
#include <string>
#include <iostream>
#include <map>
#include <sstream>
#include <fstream>
#include "value.h"
#include "PersistentStore.h"


void PersistentStore::insert(std::string key, std::string value) {
	init_values[key] = value;
	is_dirty = true;
}

void PersistentStore::load() {
    // load the store into a map
    typedef std::pair<std::string, Value> PropertyPair;
    std::ifstream store(file_name.c_str());
    if (!store.is_open()) return;
    char buf[200];
    while (store.getline(buf, 200, '\n')) {
        char value_buf[200];
        std::istringstream in(buf);
        std::string name, property, value_str;
        in >> name >> property;
        in.get(value_buf, 200, '\n');
        size_t n = strlen(value_buf);
        char *vp = value_buf+1; // skip space delimiter
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
        char *endp;
        i_value = strtol(value_str.c_str(), &endp, 10);
        name = name + "." + property;
        if (*endp == 0)
            init_values[name] = i_value;
        else
            init_values[name] = Value(value_str.c_str(), kind);
        std::cout << name << ":" << value_str << "\n";
    }
    store.close();
	is_dirty = false;
}

std::ostream &PersistentStore::operator<<(std::ostream &out) const {
	std::pair<std::string, Value>prop;
	BOOST_FOREACH(prop, init_values) {
		std::string name = prop.first;
		std::cerr << name << "\n";
		size_t pos = name.rfind('.');
		name.erase(pos);
		std::string property = prop.first.substr(prop.first.rfind('.') + 1);
		out << name << " " << property << " " << prop.second << "\n";
	}
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
			std::cerr << "num entries: " << init_values.size() << "\n";
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
