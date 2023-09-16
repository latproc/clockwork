#include "ethercat_xml_parser.h"
#include <ECInterface.h>
#include <Statistics.h>
#include <algorithm>
#include <assert.h>
#include <ecrt.h>
#include <errno.h>
#include <fstream>
#include <iostream>
#include <iterator>
#include <libxml/xmlreader.h>
#include <map>

using namespace std;

//Statistics *statistics = NULL;
//std::list<Statistic *> Statistic::stats;
//ec_pdo_entry_info_t *c_entries = 0;
//ec_pdo_info_t *c_pdos = 0;
//ec_sync_info_t *c_syncs = 0;
//EntryDetails *c_entry_details = 0;

ConfigurationDetails::ConfigurationDetails()
    : c_entries(0), c_syncs(0), num_syncs(0), num_entries(0) {}

ConfigurationDetails::~ConfigurationDetails() {
    //if (c_entries) delete[] c_entries;
    c_entries = 0;
    //if (c_entry_details) delete[] c_entry_details;
    c_entry_details = 0;
    //if (c_syncs) delete[] c_syncs;
    c_syncs = 0;
}

void ConfigurationDetails::init() {
    // add pdo entries for this slave
    // note the assumptions here about the maximum number of entries, pdos and syncs we expect
    //if (c_entries) delete[] c_entries;
    const int c_entries_size = sizeof(ec_pdo_entry_info_t) * estimated_max_entries;
    c_entries = (ec_pdo_entry_info_t *)malloc(c_entries_size);
    memset(c_entries, 0, c_entries_size);

    //if (c_entry_details) delete[] c_entry_details;
    c_entry_details = new EntryDetails[estimated_max_entries];

    //if (c_syncs) delete[] c_syncs;
    const int c_syncs_size = sizeof(ec_sync_info_t) * estimated_max_syncs;
    c_syncs = (ec_sync_info_t *)malloc(c_syncs_size);
    memset(c_syncs, 0, c_syncs_size);
    for (unsigned int i = 0; i < estimated_max_syncs; ++i) {
        c_syncs[i].index = 0xff;
    }

    num_syncs = 0;
    assert(num_syncs < estimated_max_syncs);
}
// SmMapping carries a list of pdo indexes that are included in the mapping

SmMapping::SmMapping() : num(0), n_pdos(0), pdos(0) { init(); }
SmMapping::~SmMapping() {
    if (pdos) {
        delete pdos;
    }
}

void SmMapping::init() {
    pdos = new unsigned int[30]; // plenty of pdo space?
    memset(pdos, 0, 30 * sizeof(int));
}

// An AltSm may be selected by the user or may be selected by default
// It contains a list of sync manager mappings that each have a list of PDO references
AltSm::AltSm() : is_default(false), is_selected(false) {}

/*  The parser collects information to complete a DeviceInfo structure
    which provides the information needed to configure the device process
    data exchange objects
*/
DeviceInfo::DeviceInfo() : product_code(0), revision_no(0), selected_alt_sm(-1) {}
DeviceInfo::DeviceInfo(uint64_t pc, uint64_t rn)
    : product_code(pc), revision_no(rn), selected_alt_sm(-1) {}
DeviceInfo::DeviceInfo(uint64_t pc, uint64_t rn, const char *which)
    : product_code(pc), revision_no(rn), selected_alt_sm(-1), selected_alt_sm_name(which) {}

DeviceInfo::~DeviceInfo() {}
std::ostream &operator<<(std::ostream &out, const DeviceInfo &dev) { return dev.operator<<(out); }

static const std::string deviceKey("Device");
static const std::string moduleKey("Module");
static const std::string typeKey("Type");
static const std::string nameKey("Name");
static const std::string pdoKey("Pdo");
static const std::string txPdoKey("TxPdo");
static const std::string rxPdoKey("RxPdo");
static const std::string dataTypeKey("DataType");
static const std::string alternateSmMappingKey("AlternativeSmMapping");
static const std::string indexKey("Index");
static const std::string entryKey("Entry");
static const std::string smKey("Sm");
static const std::string altSmMappingKey("AlternativeSmMapping");

EtherCATXMLParser::EtherCATXMLParser(DeviceConfigurator &dc)
    : state(skipping), configurator(dc), current_depth(0), current_device(0), matched_device(0),
      current_alt_sm(0), selected_alt_sm(0), current_sm_mapping(0), num_device_entries(0),
      current_sm_index(-1), capturing_pdo(false), current_alt_sm_mapping_index(-1),
      current_pdo_index(0), current_pdo_start_entry(0) {}

void EtherCATXMLParser::init() {
    state = skipping;
    current_depth = 0;
    current_device = 0;
    matched_device = 0;
    current_alt_sm = 0;
    selected_alt_sm = 0;
    current_sm_mapping = 0;
    num_device_entries = 0;
    current_sm_index = -1;
    capturing_pdo = false;
    current_alt_sm_mapping_index = -1;
    current_pdo_index = 0;
    current_pdo_start_entry = 0;
}

uint64_t EtherCATXMLParser::intFromHex(const char *s) {
    char *rem = 0;
    errno = 0;
    uint64_t res = strtoll(s, &rem, 16);
    if (errno != 0) {
        std::cout << " error " << strerror(errno) << " converting " << s << " to an integer\n";
        return 0;
    }
    return res;
}

uint64_t EtherCATXMLParser::intFromStr(const char *s) {
    char *r = 0;
    long val;
    if (strncmp("#x", s, 2) == 0 || strncmp("0x", s, 2) == 0) {
        val = strtol(s + 2, &r, 16);
    }
    else {
        val = strtol(s, &r, 10);
    }
    assert(errno == 0);
    return val;
}

std::string EtherCATXMLParser::entry_name(const std::string &s) {
    if (s.length() > 9 && s.substr(0, 9) == "Control__") {
        return s.substr(9);
    }
    else if (s.length() > 8 && s.substr(0, 8) == "Status__") {
        return s.substr(8);
    }
    return s;
}

void EtherCATXMLParser::enter(ParserState new_state) {
    //std::cout << state << "->" << new_state << "\n";
    state = new_state;
}

void EtherCATXMLParser::reenter(ParserState new_state) {
    //std::cout << state << "->" << new_state << "\n";
    state = new_state;
}

void EtherCATXMLParser::displayAttribute(xmlTextReaderPtr reader, const char *attr_name,
                                         const char *label) {
    char *attr = (char *)xmlTextReaderGetAttribute(reader, BAD_CAST attr_name);
    if (attr) {
        if (label) {
            std::cout << label << attr;
        }
        else {
            std::cout << " " << attr_name << ": " << attr;
        }
        free(attr);
    }
}

void EtherCATXMLParser::captureAttribute(xmlTextReaderPtr reader, const char *attr_name,
                                         std::map<std::string, std::string> &attributes) {
    char *attr = (char *)xmlTextReaderGetAttribute(reader, BAD_CAST attr_name);
    if (attr) {
        attributes[attr_name] = attr;
    }
}

void EtherCATXMLParser::displayPath() {
    std::list<std::string>::iterator iter = current_path.begin();
    while (iter != current_path.end()) {
        const std::string &s = *iter++;
        std::cout << s;
        if (iter != current_path.end()) {
            std::cout << "/";
        }
    }
    std::cout << "\n";
}

/*
    Find devices matching specifications in the xml_configuration list.

    Collect Device/Name, VendorSpecifc/TwinCAT/AlternativeSMMapping where Name is "Extended info data"
    Collect Dictionary DataTypes and Objects: Index, Name, Type, BitSize, BitOffs

    Collect PDOs Index, Name, Entries

    Current issues with this parser:

    1. the generated sync manager configurations include entries with zero pdos
    Not sure if this is a problem

    2. It is not possible to load two altrnative configurations for the same device
    in the same pass

*/

void EtherCATXMLParser::processToken(xmlTextReaderPtr reader) {
    static std::map<std::string, std::string> attributes;

    const xmlChar *name_xml;
    name_xml = BAD_CAST xmlTextReaderConstName(reader);
    const char *name = (const char *)name_xml;
    std::ostream_iterator<char> out(std::cout, "");
    if (name) {
        int kind = xmlTextReaderNodeType(reader);
        //      if (matched_device)std::cout << state << " " << kind << " " << name << "\n";
        if (kind == XML_READER_TYPE_END_ELEMENT) {
            if (current_path.size()) {
                current_path.pop_back();
            }
            --current_depth;
            //if (xmlTextReaderDepth(reader) < 6) displayPath();
        }
        switch (state) {
        case skipping: {
            if (kind == XML_READER_TYPE_ELEMENT && (deviceKey == name || moduleKey == name)) {
                enter(in_device);
                if (current_alt_sm) {
                    delete current_alt_sm;
                }
                current_alt_sm = 0;
                num_device_entries = 0;
            }
        } break;
        case in_device: {
            if (kind == XML_READER_TYPE_END_ELEMENT && (deviceKey == name || moduleKey == name)) {
                // finished the device, report the sync managers collected if any
                if (current_device && current_device->config.num_entries) {
                    unsigned int n = current_device->config.num_syncs;
                    std::cout << n << " sync managers in configuration\n";
                    ec_sync_info_t *si = current_device->config.c_syncs;
                    for (unsigned int i = 0; i < n; ++i) {
                        std::cout << "sm entry: " << i << " index: " << (int)si->index
                                  << " direction: " << si->dir << " num pdos: " << si->n_pdos
                                  << "\n";
                        ++si;
                    }
                }
                if (current_device) {
                    if (!configurator.configure(current_device)) {
                        delete current_device;
                    }
                    current_device = 0;
                }
                enter(skipping);
                break;
            }
            else if (kind == XML_READER_TYPE_ELEMENT) {
                if (current_path.back() != deviceKey && current_path.back() != moduleKey) {
                    enter(in_subnode);
                }
                else if (typeKey == name) {
                    enter(in_device_type);
                    matched_device = 0;
                    attributes.clear();
                    uint64_t pc = 0, rn = 0;
                    if (xmlTextReaderHasAttributes(reader)) {
                        captureAttribute(reader, "ProductCode", attributes);
                        captureAttribute(reader, "ModuleIdent", attributes);
                        captureAttribute(reader, "RevisionNo", attributes);
                        pc = intFromHex((const char *)attributes["ProductCode"].c_str() + 2);
                        uint64_t mi =
                            intFromHex((const char *)attributes["ModuleIdent"].c_str() + 2);
                        rn = intFromHex((const char *)attributes["RevisionNo"].c_str() + 2);

                        // check whether this device matches on the user is looking for
                        for (unsigned int i = 0; i < xml_configured.size(); ++i) {
                            DeviceInfo *info;
                            if ((info = xml_configured[i]) &&
                                (info->product_code == pc || info->product_code == mi) &&
                                (info->revision_no == 0 || info->revision_no == rn)) {
                                matched_device = info;
                            }
                        }
                    }
                    if (matched_device) {
                        current_device = new DeviceInfo;
                        if (attributes.find("ProductCode") != attributes.end()) {
                            current_device->product_code =
                                intFromStr(attributes["ProductCode"].c_str());
                        }
                        else if (attributes.find("ModuleIdent") != attributes.end()) {
                            current_device->product_code =
                                intFromStr(attributes["<ModuleIdent"].c_str());
                        }
                        if (attributes.find("RevisionNo") != attributes.end()) {
                            current_device->revision_no =
                                intFromStr(attributes["RevisionNo"].c_str());
                        }
                        else {
                            current_device->revision_no = 0;
                        }
                    }
                    else {
                        std::cout << "no match for device with pc: " << std::hex << pc << "/" << rn
                                  << std::dec << " (" << attributes["ProductCode"] << "/"
                                  << attributes["RevisionNo"] << std::dec << ")\n";
                    }
                }
                else if (nameKey == name) {
                    enter(in_device_name);
                    attributes.clear();
                    if (xmlTextReaderHasAttributes(reader)) {
                        captureAttribute(reader, "LcId", attributes);
                    }
                }
                else if (matched_device && (rxPdoKey == name || txPdoKey == name)) {
                    assert(current_device);
                    enter(in_pdo);
                    attributes.clear();
                    captureAttribute(reader, "Fixed", attributes);
                    captureAttribute(reader, "Sm", attributes);
                    capturing_pdo = false;
                    current_pdo_start_entry = current_device->config.num_entries;
                }
                else if (matched_device && smKey == name) { // Active SM
                    // TBD if no alt sm has been selected, choose the one marked as default
                    enter(in_sm);
                    if (xmlTextReaderHasAttributes(reader)) {
                        captureAttribute(reader, "MinSize", attributes);
                        captureAttribute(reader, "MaxSize", attributes);
                        captureAttribute(reader, "DefaultSize", attributes);
                        captureAttribute(reader, "StartAddress", attributes);
                        captureAttribute(reader, "ControlByte", attributes);
                        captureAttribute(reader, "Enable", attributes);
                    }
                }
                else {
                }
            }
        } break;
        case in_device_type: {
            if (kind == XML_READER_TYPE_ELEMENT && current_path.back() != deviceKey &&
                current_path.back() != moduleKey) {
                enter(in_subnode);
                break;
            }
            if (kind == XML_READER_TYPE_TEXT && matched_device) {
                if (matched_device) {
                    std::cout << "MATCHED Type: ";
                }
                if (xmlTextReaderHasValue(reader)) {
                    std::cout << xmlTextReaderConstValue(reader);
                }
                std::cout << " ProductCode: " << attributes["ProductCode"] << " ";
                std::cout << " RevisionNo: " << attributes["RevisionNo"] << " ";
                std::cout << "\n";
            }
            if (kind == XML_READER_TYPE_END_ELEMENT && typeKey == name) {
                enter(in_device);
            }
        } break;
        case in_device_name: {
            if (kind == XML_READER_TYPE_ELEMENT) {
                enter(in_subnode);
                break;
            }
            if (matched_device && (kind == XML_READER_TYPE_TEXT || kind == XML_READER_TYPE_CDATA)) {
                //if (attributes.find("LcId") == attributes.end())
                std::cout << " Name: ";
                if (xmlTextReaderHasValue(reader)) {
                    std::cout << xmlTextReaderConstValue(reader);
                }
                std::cout << "\n";
            }
            if (kind == XML_READER_TYPE_END_ELEMENT && nameKey == name) {
                enter(in_device);
            }
        } break;
        case in_subnode: {
            if (kind == XML_READER_TYPE_ELEMENT) {
                // tbd we aren't checking the full path to the AlternativeSmMapping
                // node but since we test matched_device we know it is occuring
                // within a device
                if (matched_device && altSmMappingKey == name) {
                    std::cout << "Found alternative SM mapping\n";
                    int num_alts = current_device->sm_alternatives.size();
                    AltSm *sm = new AltSm;
                    if (xmlTextReaderHasAttributes(reader)) {
                        captureAttribute(reader, "Default", attributes);
                        if (attributes.find("Default") != attributes.end()) {
                            sm->is_default = attributes["Default"] == "1";
                            if (current_device->selected_alt_sm == -1) {
                                current_device->selected_alt_sm = num_alts + 1;
                            }
                        }
                    }
                    current_alt_sm = sm;
                    matched_device->sm_alternatives.push_back(sm);
                    enter(in_alt_sm_mapping);
                }
            }
            else if (kind == XML_READER_TYPE_END_ELEMENT) {
                if (current_path.back() == deviceKey || current_path.back() == moduleKey) {
                    enter(in_device);
                }
                else if (current_path.back() == rxPdoKey || current_path.back() == txPdoKey) {
                    if (current_device) {
                        reenter(in_pdo);
                    }
                }
            }
        } break;
        case in_sm: {
            // we don't expect subnodes within the Sm nodes, just need to collect the
            // value (name) and attributes of the Sm node.
            if (matched_device && current_path.back() == smKey &&
                (kind == XML_READER_TYPE_TEXT || kind == XML_READER_TYPE_CDATA)) {
                if (xmlTextReaderHasValue(reader)) {
                    attributes["Name"] = (const char *)xmlTextReaderConstValue(reader);
                }
            }
            else if (kind == XML_READER_TYPE_END_ELEMENT && smKey == name) {
                assert(current_device);
                ConfigurationDetails &config(current_device->config);
                if (!config.c_syncs) {
                    config.init();
                }
                std::string cb = attributes["ControlByte"];
                uint8_t b_cb = 0xff & intFromStr(cb.c_str());
                std::cout << "SM. Name: " << attributes["Name"] << " ControlByte: " << cb << " "
                          << (b_cb & 0x0c ? "Output" : "Input") << "\n";
                unsigned int &ns = current_device->config.num_syncs;
                ec_sync_info_t *si = &current_device->config.c_syncs[ns];
                si->index = ns++;
                si->dir = b_cb & 0x0c ? EC_DIR_OUTPUT : EC_DIR_INPUT;
                si->n_pdos = 0;
                const int c_pdos_size =
                    sizeof(ec_pdo_info_t) * ConfigurationDetails::estimated_max_pdos;
                si->pdos = (ec_pdo_info_t *)malloc(c_pdos_size);
                memset(si->pdos, 0, c_pdos_size);

                si->watchdog_mode = EC_WD_DEFAULT;
                enter(in_device);
            }
        }
        case in_pdo: {
            // whether PDOs are included in the current device configuration depends
            // on the Sm tag. If it exists the PDO is added to the given SM, otherwise
            // it is included in the SM that is given in the alternative sm that references it

            if (kind == XML_READER_TYPE_ELEMENT) {
                if (matched_device && indexKey == name) {
                    enter(in_pdo_index);
                }
                else if (matched_device && nameKey == name) {
                    enter(in_pdo_name);
                }
                else if (matched_device && entryKey == name) {
                    entry_attributes.clear();
                    enter(in_pdo_entry);
                }
                else {
                    enter(in_subnode);
                }
                break;
            }
            else if (kind == XML_READER_TYPE_END_ELEMENT &&
                     (current_path.back() == deviceKey || current_path.back() == moduleKey)) {
                // finished a pdo, if we were capturing it, save the data
                if (capturing_pdo) {
                    if (current_sm_index >= 0) {
                        unsigned int &n = current_device->config.c_syncs[current_sm_index].n_pdos;
                        ec_pdo_info_t *pd =
                            &current_device->config.c_syncs[current_sm_index].pdos[n];
                        pd->index = current_pdo_index;
                        pd->n_entries =
                            current_device->config.num_entries - current_pdo_start_entry;
                        pd->entries = &current_device->config.c_entries[current_pdo_start_entry];

                        std::cout << "Added pdo entry " << std::hex << "0x" << pd->index << std::dec
                                  << ", " << pd->n_entries << " entries to sm " << current_sm_index
                                  << "\n";
                        ++n;
                    }
                }
                enter(in_device);
            }
        } break;
        case in_pdo_name: {
            if (kind == XML_READER_TYPE_END_ELEMENT) {
                reenter(in_pdo);
            }

            if (matched_device && (kind == XML_READER_TYPE_TEXT || kind == XML_READER_TYPE_CDATA)) {
                std::cout << "PDO: ";
                if (xmlTextReaderHasValue(reader)) {
                    current_pdo_name = (const char *)xmlTextReaderConstValue(reader);
                    std::cout << current_pdo_name;
                }
                else {
                    current_pdo_name = "UNNAMED PDO"; // unexpected.. TBD can this happen?
                }
                std::cout << "\n";
            }

        } break;
        case in_pdo_index: {
            if (kind == XML_READER_TYPE_END_ELEMENT) {
                reenter(in_pdo);
            }

            if (matched_device && (kind == XML_READER_TYPE_TEXT || kind == XML_READER_TYPE_CDATA)) {
                assert(current_device);
                if (xmlTextReaderHasValue(reader)) {
                    current_pdo_index = intFromStr((const char *)xmlTextReaderConstValue(reader));
                    capturing_pdo = false;
                    current_sm_index = -1;
                    current_alt_sm_mapping_index = -1;
                    if (selected_alt_sm) {
                        for (unsigned int i = 0; i < selected_alt_sm->mappings.size(); ++i) {
                            for (unsigned int j = 0; j < selected_alt_sm->mappings[i]->n_pdos;
                                 ++j) {
                                if (selected_alt_sm->mappings[i]->pdos[j] == current_pdo_index) {
                                    current_sm_index = selected_alt_sm->mappings[i]->num;
                                    current_alt_sm_mapping_index = i;
                                    capturing_pdo = true;
                                    break;
                                }
                            }
                        }
                    }
                    else if (attributes.find("Sm") != attributes.end()) { // have a SM number
                        current_sm_index = intFromStr(attributes["Sm"].c_str());
                        capturing_pdo = true;
                    }
                    std::cout << "PDO Index: ";
                    std::cout << "0x" << std::hex << current_pdo_index << std::dec << " sm index "
                              << current_alt_sm_mapping_index << " (" << attributes["Sm"] << ") "
                              << (capturing_pdo ? " (USE)" : "");
                }
                std::cout << "\n";
            }
        } break;
        case in_pdo_entry: {
            if (kind == XML_READER_TYPE_END_ELEMENT && entryKey == name) {
                if (capturing_pdo) {
                    // && entry_attributes.find("DataType") != entry_attributes.end()) {
                    unsigned int &n(current_device->config.num_entries);
                    std::cout << "\n";
                    std::cout << "Entry " << num_device_entries << "\n";

                    std::map<std::string, std::string>::iterator iter = entry_attributes.begin();
                    while (iter != entry_attributes.end()) {
                        const std::pair<std::string, std::string> &item = *iter++;
                        std::cout << "\t" << item.first << " " << item.second << "\n";
                    }
                    ec_pdo_entry_info_t *e = &current_device->config.c_entries[n];
                    e->index = intFromStr(entry_attributes["Index"].c_str());
                    e->subindex = intFromStr(entry_attributes["SubIndex"].c_str());
                    e->bit_length = intFromStr(entry_attributes["BitLen"].c_str());
                    std::cout << "Added entry: " << std::hex << "0x" << e->index << std::dec << ", "
                              << (int)e->subindex << ", " << (int)e->bit_length << "\n";

                    EntryDetails *ed = &current_device->config.c_entry_details[n];
                    ed->name = current_pdo_name + " " + entry_name(entry_attributes["Name"]);
                    ed->entry_index = n;
                    unsigned int &npdos = current_device->config.c_syncs[current_sm_index].n_pdos;
                    ed->pdo_index = npdos;
                    ed->sm_index = current_sm_index;

                    std::cout << "Added entry detail: " << ed->name << ", " << ed->entry_index
                              << ", " << ed->pdo_index << ", " << ed->sm_index << "\n";
                    ++n;
                }
                ++num_device_entries;
                reenter(in_pdo);
            }
            else if (kind == XML_READER_TYPE_ELEMENT) {
                //std::cout << "\t" << name << " ";
                if (xmlTextReaderHasValue(reader)) {
                    std::cout << xmlTextReaderConstValue(reader);
                }
            }
            else if (matched_device &&
                     (kind == XML_READER_TYPE_TEXT || kind == XML_READER_TYPE_CDATA)) {
                if (xmlTextReaderHasValue(reader)) {
                    entry_attributes[current_path.back()] =
                        (const char *)xmlTextReaderConstValue(reader);
                }
            }
        } break;
        case in_alt_sm_mapping: {
            if (kind == XML_READER_TYPE_ELEMENT && smKey == name) {
                if (xmlTextReaderHasAttributes(reader)) {
                    captureAttribute(reader, "No", attributes);
                    if (attributes.find("No") != attributes.end()) {
                        current_sm_mapping = new SmMapping();
                        current_alt_sm->mappings.push_back(current_sm_mapping);
                        current_sm_mapping->num = intFromStr(attributes["No"].c_str());
                    }
                }
            }
            else if (matched_device &&
                     (kind == XML_READER_TYPE_TEXT || kind == XML_READER_TYPE_CDATA)) {
                if (current_path.back() == nameKey) {
                    if (xmlTextReaderHasValue(reader)) {
                        attributes["Name"] = (const char *)xmlTextReaderConstValue(reader);
                    }
                }
                else if (current_alt_sm && current_path.back() == pdoKey) {
                    if (xmlTextReaderHasValue(reader)) {
                        //if (!current_alt_sm->pdos) current_alt_sm->pdos = new unsigned int(current_alt_sm->alloc_size);
                        current_sm_mapping->pdos[current_sm_mapping->n_pdos++] =
                            0xffff & intFromStr((const char *)xmlTextReaderConstValue(reader));
                    }
                }
            }
            else if (kind == XML_READER_TYPE_END_ELEMENT && altSmMappingKey == name) {
                std::string sm_name = attributes["Name"];
                if (matched_device->selected_alt_sm_name.length() &&
                    matched_device->selected_alt_sm_name == sm_name) {
                    current_device->selected_alt_sm = current_alt_sm->mappings.size() - 1;
                    current_alt_sm->is_selected = true;
                    selected_alt_sm = current_alt_sm;
                }
                std::cout << "SM Name: " << sm_name;
                if (current_alt_sm && current_alt_sm->is_default) {
                    std::cout << " (Default) ";
                }

                unsigned int curr_mapping = intFromStr(attributes["No"].c_str());
                std::cout << " No: " << curr_mapping
                          << (matched_device->selected_alt_sm_name.length() &&
                                      matched_device->selected_alt_sm_name == sm_name
                                  ? " [selected]"
                                  : "")
                          << "\n";
                int count = (int)current_alt_sm->mappings[0]->n_pdos;
                if (count) {
                    unsigned int *i = current_alt_sm->mappings[0]->pdos;
                    while (count--) {
                        std::cout << "0x" << std::hex << *i << std::dec << (count ? ", " : "\n");
                        ++i;
                    }
                }
                reenter(in_subnode);
            }
        } break;
        default:;
        }

#if 0
        if (kind != XML_READER_TYPE_SIGNIFICANT_WHITESPACE) {
            const char *spaces40 = "                                        ";
            int depth;
            depth = xmlTextReaderDepth(reader);
            value = xmlTextReaderConstValue(reader);
            std::copy(spaces40, spaces40 + (depth > 40 ? 41 : depth), out);
            std::cout << xmlTextReaderNodeType(reader)
                    << " name: " << name
                    << " empty: " << xmlTextReaderIsEmptyElement(reader);
            if (xmlTextReaderHasValue(reader)) {
                std::cout << value;
            }
            std::cout << "\n";
        }
#endif

        if (kind == XML_READER_TYPE_ELEMENT) {
            unsigned int depth = xmlTextReaderDepth(reader);
            if (depth > current_depth) {
                current_path.push_back((const char *)name);
                current_depth = depth;
                assert(current_path.size() == depth);
            }
        }
    }
}

bool EtherCATXMLParser::loadDeviceConfigurationXML(const char *filename) {
    xmlTextReaderPtr reader;
    int res;

    // check whether this device matches on the user is looking for
    for (unsigned int i = 0; i < xml_configured.size(); ++i) {
        DeviceInfo *info = xml_configured[i];
        if (!info) {
            continue;
        }
        std::cout << "looking for : " << std::hex << "0x" << info->product_code << "/"
                  << info->revision_no << std::dec << "\n";
    }
    reader = xmlReaderForFile(filename, 0, 0);
    if (reader) {
        while ((res = xmlTextReaderRead(reader)) == 1) {
            processToken(reader);
        }
        if (res) {
            std::cerr << "Parse error reading " << filename << "\n";
        }
        xmlFreeTextReader(reader);
        return true;
    }
    else {
        std::cerr << "Failed to open an xml reader on file " << filename << "\n";
        return false;
    }
}
