#pragma once

#include "Action.h"
#include "Expression.h"
#include "symboltable.h"

class MachineInstance;

enum SetOperation { soIntersect, soUnion, soDifference, soSelect };

struct FileOperationActionTemplate : public ActionTemplate {

    // the bitset property named in the source parameter are used to set the state of
    // entries in the list named in the dest parameter
    // The names are to be evaluated in the
    // scope where the command is used.

    FileOperationActionTemplate(Value num, Value a, Value b, Value destination, Value property,
                                SetOperation op, Predicate *pred, bool remove, Value start = -1,
                                Value end = -1);
    ~FileOperationActionTemplate();

    virtual Action *factory(MachineInstance *mi);
    std::ostream &operator<<(std::ostream &out) const {
        return out << "FileOperationAction " << src_a_name << ", src_b_name "
                   << " to " << dest_name << " using " << property_name << "\n";
    }

    Value count;
    std::string src_a_name;
    std::string src_b_name;
    std::string dest_name;
    std::string property_name;
    SetOperation operation;
    Condition condition;
    bool remove_selected;
    Value start_pos;
    Value end_pos;
};

struct FileOperationAction : public Action {
    FileOperationAction(MachineInstance *m, const FileOperationActionTemplate *dat);
    FileOperationAction();
    Status run();
    Status checkComplete();
    virtual Status doOperation();
    virtual std::ostream &operator<<(std::ostream &out) const;

  protected:
    MachineInstance *scope;
    Value count;
    Value source_a;
    Value source_b;
    Value dest;
    Condition condition;
    std::string property_name;
    MachineInstance *source_a_machine;
    MachineInstance *source_b_machine;
    MachineInstance *dest_machine;
    SetOperation operation;
    bool remove_selected;
    Value start_pos;
    Value end_pos;
    long sp;
    long ep;
};

struct IntersectSetOperation : public FileOperationAction {
    IntersectSetOperation(MachineInstance *m, const FileOperationActionTemplate *dat);
    Status doOperation();
};

struct UnionSetOperation : public FileOperationAction {
    UnionSetOperation(MachineInstance *m, const FileOperationActionTemplate *dat);
    Status doOperation();
};

struct DifferenceSetOperation : public FileOperationAction {
    DifferenceSetOperation(MachineInstance *m, const FileOperationActionTemplate *dat);
    Status doOperation();
};

struct SelectSetOperation : public FileOperationAction {
    SelectSetOperation(MachineInstance *m, const FileOperationActionTemplate *dat);
    Status doOperation();
};

#endif
