#pragma once

#include <string>

class MachineClass;

// RECORD schema lives on MachineClass *properties* (RECORD, TABLE, VIEW, KEY,
// UNIQUE, NOT_NULL), not extra C++ fields. Those names are private so they are
// not treated as row columns.
namespace RecordClass {

enum ColumnFlag { COL_NONE = 0, COL_KEY = 1, COL_UNIQUE = 2, COL_NOT_NULL = 4 };

void mark(MachineClass *mc);
void setTable(MachineClass *mc, const std::string &name);
void setView(MachineClass *mc, const std::string &name);
void addKey(MachineClass *mc, const std::string &column);
void addUnique(MachineClass *mc, const std::string &column);
void addNotNull(MachineClass *mc, const std::string &column);
void addFlags(MachineClass *mc, const std::string &column, unsigned flags);

bool isRecord(const MachineClass *mc);
bool isView(const MachineClass *mc);
std::string tableName(const MachineClass *mc);
std::string keyColumn(const MachineClass *mc);
unsigned keyColumnCount(const MachineClass *mc);
unsigned columnFlags(const MachineClass *mc, const std::string &column);

} // namespace RecordClass
