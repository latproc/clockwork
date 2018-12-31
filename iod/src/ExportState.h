#ifndef __export_state_h__
#define __export_state_h__

#include <string>
#include <map>
#include "value.h"
#include "symboltable.h"

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
  static int lookup(const std::string name);
  static void add_message(const std::string name, int value = -1);
  static std::map<std::string, int> &all_messages() { return string_ids; }
  static int lookup_symbol(const std::string name);
  static void add_symbol(const std::string name, int value = -1);
  static std::map<std::string, int> &all_symbols() { return symbols; }
  static std::map<std::string, PredicateSymbolDetails> &all_symbol_names() { return symbol_names; }

  void set_prefix(std::string prefix) { variable_prefix = prefix; }
  const std::string &prefix() { return variable_prefix; }

private:
  SymbolTable messages;
  std::string variable_prefix;
  static ExportState *_instance;
  static std::map<std::string, int> string_ids;
  static std::map<std::string, int> message_ids;
  static std::map<std::string, int> symbols;
  static std::map<std::string, PredicateSymbolDetails> symbol_names;
};

#endif
