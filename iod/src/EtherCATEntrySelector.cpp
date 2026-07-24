/*
    Copyright (C) 2012 Martin Leadbeater, Michael O'Connor

    This file is part of Latproc and is distributed under the terms of the
    GNU General Public License version 2 or (at your option) any later version.
*/

#include "EtherCATEntrySelector.h"

EtherCATEntryMatch resolveEtherCATEntry(
    const std::vector<EtherCATEntryIdentity> &entries, uint16_t index, uint8_t subindex,
    bool match_pdo, uint16_t pdo_index, unsigned int &position) {
    bool found = false;

    for (const EtherCATEntryIdentity &entry : entries) {
        if (entry.index != index || entry.subindex != subindex ||
            (match_pdo && entry.pdo_index != pdo_index)) {
            continue;
        }
        if (found) {
            return EtherCATEntryMatch::ambiguous;
        }
        position = entry.position;
        found = true;
    }

    return found ? EtherCATEntryMatch::found : EtherCATEntryMatch::not_found;
}
