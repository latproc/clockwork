#include "gtest/gtest.h"
#include <ethercat_xml_parser.h>
#include <list>
#include <map>
#include <symboltable.h>
#include <value.h>

namespace {
std::list<DeviceInfo *> collected_configurations;
std::map<unsigned int, DeviceInfo *> slave_configuration;

class ClockworkDeviceConfigurator : public DeviceConfigurator {
  public:
    bool configure(DeviceInfo *dev) {
        std::cout << "collected configuration for device " << std::hex << " 0x" << dev->product_code
                  << " " << std::hex << " 0x" << dev->revision_no << "\n";
        std::list<DeviceInfo *>::iterator iter = collected_configurations.begin();
        while (iter != collected_configurations.end()) {
            const DeviceInfo *di = *iter++;
            if (*dev == *di) {
                std::cout << " using item already found\n";
                return true;
            }
        }
        collected_configurations.push_back(dev);
        return true;
    }
};

class EtherCatXMLTest : public ::testing::Test {
  protected:
    EtherCatXMLTest() : parser(configurator) {}

    void SetUp() override {}

    ClockworkDeviceConfigurator configurator;
    EtherCATXMLParser parser;
};

TEST_F(EtherCatXMLTest, LoadDeviceConfigurationXML) {
    EXPECT_TRUE(parser.loadDeviceConfigurationXML("test.xml")) << "it parses the test XML";
}

} // namespace
