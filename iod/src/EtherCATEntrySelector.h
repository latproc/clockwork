/*
    Copyright (C) 2012 Martin Leadbeater, Michael O'Connor

    This file is part of Latproc and is distributed under the terms of the
    GNU General Public License version 2 or (at your option) any later version.
*/

#ifndef __ETHERCAT_ENTRY_SELECTOR_H_
#define __ETHERCAT_ENTRY_SELECTOR_H_

#include <cstddef>
#include <cstdint>
#include <vector>

struct EtherCATEntryIdentity {
    unsigned int position;
    uint16_t index;
    uint8_t subindex;
    uint16_t pdo_index;
};

enum class EtherCATEntryMatch {
    found,
    not_found,
    ambiguous
};

EtherCATEntryMatch resolveEtherCATEntry(
    const std::vector<EtherCATEntryIdentity> &entries, uint16_t index, uint8_t subindex,
    bool match_pdo, uint16_t pdo_index, unsigned int &position);

#endif
