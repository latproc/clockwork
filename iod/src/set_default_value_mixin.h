#include <functional>
#include <string>
#include <value.h>
#include <symboltable.h>

template <typename Base>
struct IOValueMixIn : public Base {
    void setValue(int32_t val) { Base::setValue(val); }
    void setValue(uint32_t val) { Base::setValue(val); }
    void turnOff() { Base::turnOff(); }
    bool is_signed() const { return Base::is_signed(); }
};

template <typename Base>
void set_default_value_and_update_io(std::function<void(const std::string &, const Value &)> set_value,
                                     const SymbolTable &properties,
                                     IOValueMixIn<Base> *io_interface) {
    // Restore the default value for certain IO.
    const Value &val = properties.lookup("default");
    if (val != SymbolTable::Null) {
        // FIXME: it is wierd to set a default property that doesn't have a VALUE
        //        property; perhaps there should be a warning when the machine is
        //        loaded.
        set_value("VALUE", val);

        // TODO: Deal with IO data sizes and non-integer IO
        if (val.kind == Value::t_integer) {
            int64_t i_val = 0;
            if (val.asInteger(i_val)) {
                if (io_interface) {
                    if (io_interface->is_signed()) {
                        io_interface->setValue((int32_t)(i_val & 0xffffffff));
                    }
                    else {
                        io_interface->setValue((uint32_t)(i_val & 0xffffffff));
                    }
                }
            }
        }
        else {
            // Legacy behaviour: call turnOff() if the default value is not an integer.
            // TODO: Log an error or warning?
            if (io_interface) {
                io_interface->turnOff();
            }
        }
    }
    else { // No default set so turn off the point when it is disabled?
        if (io_interface) {
            // TODO: Does this make sense for non-boolean IO?
            io_interface->turnOff();
        }
    }
}

