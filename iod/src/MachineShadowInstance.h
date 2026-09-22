#ifndef __MACHINESHADOWINSTANCE_H__
#define __MACHINESHADOWINSTANCE_H__

#include "MachineInstance.h"
#include <map>
#include <set>
#include <string>

class MachineShadowInstance : public MachineInstance {
  protected:
    MachineShadowInstance(InstanceType instance_type = MACHINE_INSTANCE);
    MachineShadowInstance(CStringHolder name, const char *type,
                          InstanceType instance_type = MACHINE_INSTANCE);

  private:
    MachineShadowInstance &operator=(const MachineShadowInstance &orig);
    MachineShadowInstance(const MachineShadowInstance &other);
    MachineShadowInstance *settings;

  public:
    MachineShadowInstance();
    ~MachineShadowInstance();
    virtual void idle();
    virtual bool isShadow() { return true; }

    virtual Action::Status setState(const State &new_state, uint64_t authority = 0,
                                    bool resume = false);
    virtual Action::Status setState(const char *new_state, uint64_t authority = 0,
                                    bool resume = false);

    // Remote state and properties learned while the channel is not ACTIVE.
    // Applied together when the channel becomes ACTIVE.
    void stageRemoteState(const std::string &state_name);
    void stageRemoteProperty(const std::string &name, const Value &value);
    void rememberPropertyDefault(const std::string &name);
    bool hasStagedRemote() const;
    void applyStagedRemote(uint64_t authority);
    void revertShadowToDefaults(uint64_t authority);

    friend class MachineInstanceFactory;

  private:
    bool has_staged_state_;
    std::string staged_state_;
    std::map<std::string, Value> staged_properties_;
    std::map<std::string, Value> property_defaults_;
    std::set<std::string> null_property_defaults_;
};

#endif
