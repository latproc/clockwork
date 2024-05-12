#include "DebugExtra.h"
#include "ECInterface.h"
#include "gtest/gtest.h"
#include <DebugExtra.h>
#include <ethercat_xml_parser.h>
#include <list>
#include <map>
#include <symboltable.h>
#include <utility>
#include <value.h>
#include <DebugExtra.h>

namespace {
std::vector<DeviceInfo *> collected_configurations;
std::map<unsigned int, DeviceInfo *> slave_configuration;

class ClockworkDeviceConfigurator : public DeviceConfigurator {
  public:
    bool configure(DeviceInfo *dev) {
        std::cout << "collected configuration for device " << std::hex << " 0x" << dev->product_code
                  << " " << std::hex << " 0x" << dev->revision_no << "\n";
        std::vector<DeviceInfo *>::iterator iter = collected_configurations.begin();
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

    void SetUp() override {
        LogState::instance()->insert(DebugExtra::instance()->DEBUG_ETHERCAT);
        parser.init();
    }

    void TearDown() override {
        for (size_t i = 0; i < collected_configurations.size(); ++i) {
            delete collected_configurations[i];
        }
    };

    ClockworkDeviceConfigurator configurator;
    EtherCATXMLParser parser;
};

TEST_F(EtherCatXMLTest, LoadDeviceConfigurationXML) {
    EXPECT_TRUE(parser.loadDeviceConfigurationXML("test.xml")) << "it parses the test XML";
}

TEST_F(EtherCatXMLTest, FindADevice) {
    EXPECT_EQ(collected_configurations.size(), 0);
    DeviceInfo di;
    di.product_code = 0x0000ffff;
    di.revision_no = 0x0010000;
    parser.xml_configured.push_back(&di);
    EXPECT_TRUE(parser.loadDeviceConfigurationXML("test.xml")) << "it parses the test XML";
    EXPECT_EQ(collected_configurations.size(), 1) << "it loads a device";
    if (collected_configurations.size() > 0) {
        std::cout << *collected_configurations[0] << "\n";
    }
}

} // namespace
