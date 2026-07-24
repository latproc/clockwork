#include "EtherCATEntrySelector.h"
#include "gtest/gtest.h"

namespace {

const std::vector<EtherCATEntryIdentity> entries = {
    {0, 0x7000, 1, 0x1600},
    {1, 0x7000, 2, 0x1600},
    {2, 0x7000, 1, 0x1601},
};

TEST(EtherCATEntrySelector, ResolvesUniqueObjectIdentity) {
    unsigned int position = 99;
    EXPECT_EQ(EtherCATEntryMatch::found,
              resolveEtherCATEntry(entries, 0x7000, 2, false, 0, position));
    EXPECT_EQ(1u, position);
}

TEST(EtherCATEntrySelector, RejectsMissingObjectIdentity) {
    unsigned int position = 99;
    EXPECT_EQ(EtherCATEntryMatch::not_found,
              resolveEtherCATEntry(entries, 0x7010, 1, false, 0, position));
    EXPECT_EQ(99u, position);
}

TEST(EtherCATEntrySelector, RejectsAmbiguousObjectIdentity) {
    unsigned int position = 99;
    EXPECT_EQ(EtherCATEntryMatch::ambiguous,
              resolveEtherCATEntry(entries, 0x7000, 1, false, 0, position));
}

TEST(EtherCATEntrySelector, PdoIndexDisambiguatesDuplicateObjectIdentity) {
    unsigned int position = 99;
    EXPECT_EQ(EtherCATEntryMatch::found,
              resolveEtherCATEntry(entries, 0x7000, 1, true, 0x1601, position));
    EXPECT_EQ(2u, position);
}

}
