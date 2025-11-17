#pragma once

#include "Action.h"
#include "value.h"
#include "MachineInstance.h"

class UpdateListItemsAction;

/**
 * Template created by the parser for
 *   DESERIALISE STATE DELIMITED BY "xxx" FROM srcSymbol TO ITEMS IN listSymbol
 *
 * - delimiter      – STRINGVAL literal (e.g. ",")
 * - source_symbol  – SYMBOL naming the property/variable holding the serialised states
 * - list_symbol    – SYMBOL naming the LIST machine
 */
class UpdateListItemsActionTemplate : public ActionTemplate {
public:
    UpdateListItemsActionTemplate(const char *property_name,
                                  const char *delimiter,
                                  const char *sourceSymbol,
                                  const char *listSymbol);

    Action *factory(MachineInstance *mi) override;

    std::ostream &operator<<(std::ostream &out) const override;
    void toC(std::ostream &out, std::ostream &vars) const override;

    Value property;
    Value delimiter;
    Value source_symbol;
    Value list_symbol;
};

/**
 * Runtime action: resolve the string and LIST, then update each entry's state.
 */
class UpdateListItemsAction : public Action {
public:
    UpdateListItemsAction(MachineInstance *owner, UpdateListItemsActionTemplate &tmpl);

    Status run() override;
    Status checkComplete() override { return Action::Complete; }

    std::ostream &operator<<(std::ostream &out) const override;

private:
    // these are copies of the template values bound to this owner
    Value property;
    Value delimiter;
    Value source_symbol;
    Value list_symbol;

    MachineInstance *list_machine;
};