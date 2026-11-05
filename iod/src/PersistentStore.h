#ifndef __PersistentStore_h__
#define __PersistentStore_h__

#include <map>
#include <string>
#include <sys/stat.h>
#include <utility>
#include <value.h>

class PersistentStore {

  public:
    typedef std::pair<std::string, Value> PropertyPair;

    PersistentStore(const std::string &filename)
        : file_name(filename), is_dirty(false), primary_valid(false), primary_stat_valid(false) {}

    bool dirty() const { return is_dirty; }
    bool load(std::string *error = 0);
    bool save(std::string *error = 0);
    void split(std::string &name, std::string &prop) const;
    std::ostream &operator<<(std::ostream &out) const;
    void insert(std::string machine, std::string property, Value value);

    std::map<std::string, std::map<std::string, Value>> init_values;

  private:
    bool validate_file(const std::string &path, std::string *error) const;
    bool load_file(const std::string &path, bool &missing, std::string *error,
                   std::map<std::string, std::map<std::string, Value>> &values) const;

    std::string file_name;
    bool is_dirty;
    bool primary_valid;
    bool primary_stat_valid;
    struct stat primary_stat;
};

#endif
