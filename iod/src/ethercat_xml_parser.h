#ifndef __ethercat_xml_parser_h__
#define __ethercat_xml_parser_h__

#include <ECInterface.h>
#include <Statistics.h>
#include <algorithm>
#include <assert.h>
#include <ecrt.h>
#include <fstream>
#include <iostream>
#include <iterator>
#include <libxml/xmlreader.h>
#include <map>
#include <tool/MasterDevice.h>

struct ConfigurationDetails {
    EntryDetails *c_entry_details;
    ec_pdo_entry_info_t *c_entries;
    ec_sync_info_t *c_syncs;
    unsigned int num_syncs;
    unsigned int num_entries;

    static const unsigned int estimated_max_entries = 128;
    static const unsigned int estimated_max_pdos = 32;
    static const unsigned int estimated_max_syncs = 32;

    ConfigurationDetails();
    ~ConfigurationDetails();
    void init();
};

/*  The AltSm structure supports collecting the AlternativeSmMapping items from the
    VendorSpecific/TwinCAT section of the configuration.

    Each AltSm contains several sync manager mappings which have a list of PDO references (indexes)
*/

// SmMapping carries a list of pdo indexes that are included in the mapping
class SmMapping {
  public:
    unsigned int num;
    unsigned int n_pdos;
    unsigned int *pdos;
    SmMapping();
    ~SmMapping();

    void init();

  private:
    SmMapping(const SmMapping &);
    SmMapping &operator=(const SmMapping &);
};

// An AltSm may be selected by the user or may be selected by default
// It contains a list of sync manager mappings that each have a list of PDO references
struct AltSm {
    static const int alloc_size = 32;
    bool is_default;
    bool is_selected;
    std::vector<SmMapping *> mappings;
    AltSm();

  private:
    AltSm(const AltSm &other);
    AltSm &operator=(const AltSm &other);
};

/*  The parser collects information to complete a DeviceInfo structure
    which provides the information needed to configure the device process
    data exchange objects
*/
struct DeviceInfo {
    uint64_t product_code;
    uint64_t revision_no;
    int selected_alt_sm;              // -1 for not specified
    std::string selected_alt_sm_name; // empty for not specified
    std::vector<AltSm *> sm_alternatives;
    ConfigurationDetails config;
    DeviceInfo();
    DeviceInfo(uint64_t pc, uint64_t rn);
    DeviceInfo(uint64_t pc, uint64_t rn, const char *which);
    ~DeviceInfo();
    bool operator==(const DeviceInfo &other) {
        return other.product_code == product_code && other.revision_no == revision_no;
        //&& other.selected_alt_sm_name == selected_alt_sm_name;
    }
    std::ostream &operator<<(std::ostream &out) const {
        out << "DeviceInfo: 0x" << std::hex << product_code << ":" << revision_no;
        if (selected_alt_sm >= -1) {
            out << "/" << selected_alt_sm_name;
        }
        return out;
    }
};

std::ostream &operator<<(std::ostream &out, const DeviceInfo &dev);

class DeviceConfigurator {
  public:
    // the configure function return false if it does not wish to
    // retain the passed-in object
    virtual bool configure(DeviceInfo *dev) = 0;
};

class EtherCATXMLParser {
  public:
    std::vector<DeviceInfo *> xml_configured;

    enum ParserState {
        skipping,
        in_device,
        in_device_type,
        in_device_name,
        in_subnode,
        in_pdo,
        in_pdo_index,
        in_pdo_name,
        in_pdo_entry,
        in_alt_sm_mapping,
        in_sm
    };

    ParserState state;
    DeviceConfigurator &configurator;

    unsigned int current_depth;
    DeviceInfo *current_device;
    DeviceInfo *matched_device;

    EtherCATXMLParser(DeviceConfigurator &dc);

    void init();

    uint64_t intFromHex(const char *s);

    uint64_t intFromStr(const char *s);

    std::string entry_name(const std::string &s);

    void enter(ParserState new_state);

    void reenter(ParserState new_state);

    void displayAttribute(xmlTextReaderPtr reader, const char *attr_name, const char *label = NULL);

    void captureAttribute(xmlTextReaderPtr reader, const char *attr_name,
                          std::map<std::string, std::string> &attributes);

    std::list<std::string> current_path;

    void displayPath();

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

    std::string current_pdo_name;

    AltSm *current_alt_sm;         // valid when reading an AlternativeSm tag only
    AltSm *selected_alt_sm;        // valid when reading an AlternativeSm tag only
    SmMapping *current_sm_mapping; // valid when adding pdo ids to an alt sm tag
    std::map<std::string, std::string> entry_attributes;
    int num_device_entries;
    int current_sm_index;
    bool capturing_pdo;               // true when within a pdo that is used within the selected sm
    int current_alt_sm_mapping_index; // valid when capturing a pdo specified in an alt sm
    unsigned int current_pdo_index;
    unsigned int current_pdo_start_entry; // used to calculate the pdo length

    void processToken(xmlTextReaderPtr reader);

    bool loadDeviceConfigurationXML(const char *filename);
};
#endif
