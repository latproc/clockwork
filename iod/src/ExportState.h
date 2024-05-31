#ifndef __export_state_h__
#define __export_state_h__

#include "symboltable.h"
#include "value.h"
#include <map>
#include <string>

class PredicateSymbolDetails {
  public:
    std::string name;
    std::string type;
    std::string export_name;
    PredicateSymbolDetails();
    PredicateSymbolDetails(std::string name, std::string type, std::string export_name);
    bool operator<(const PredicateSymbolDetails &other) const;
};

class ExportState {
  private:
    ExportState() {}

  public:
    static ExportState *instance();
    const Value &symbol(const char *name);
    const Value &create_symbol(const char *name);
    static void add_state(const std::string name);
    static size_t lookup(const std::string name);
    static void add_message(const std::string name, int value = -1);
    static std::map<std::string, size_t> &all_messages() { return string_ids; }
    static size_t lookup_symbol(const std::string name);
    static void add_symbol(const std::string name, int value = -1);
    static std::map<std::string, size_t> &all_symbols() { return symbols; }
    static std::map<std::string, PredicateSymbolDetails> &all_symbol_names() {
        return symbol_names;
    }

    void set_prefix(std::string prefix) { variable_prefix = prefix; }
    const std::string &prefix() { return variable_prefix; }

    std::set<std::string> &remotes() { return remote_properties; }

  private:
    SymbolTable messages;
    std::string variable_prefix;
    static ExportState *_instance;
    static std::map<std::string, size_t> string_ids;
    static std::map<std::string, size_t> message_ids;
    static std::map<std::string, size_t> symbols;
    static std::map<std::string, PredicateSymbolDetails> symbol_names;
    std::set<std::string> remote_properties;
};

#endif
