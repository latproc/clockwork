#ifndef __PersistentStore_h__
#define __PersistentStore_h__

#include <utility>
#include <string>
#include <iostream>
#include <map>
#include <lib_clockwork_client/includes.hpp>

class PersistentStore {

public:
	typedef std::pair<std::string, Value> PropertyPair;

	PersistentStore(const std::string &filename) : file_name(filename), is_dirty(false) { }

	bool dirty() { return is_dirty; }
	void load();
	void save();
    void split(std::string &name, std::string& prop) const;
	std::ostream &operator<<(std::ostream &out) const;
	void insert(std::string machine, std::string property, Value value);


	std::map<std::string, std::map<std::string, Value> >init_values;
private:
	std::string file_name;
	bool is_dirty;
};


#endif
