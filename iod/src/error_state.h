#pragma once
#include <State.h>
#include <cassert>

template <typename Base>
class ErrorStateBase : public Base {
public:
    const State *findState(const std::string &state_name) { return Base::findState(state_name); }
    void setState(const State &state) { Base::setState(state); }
    void setInitialState() { Base::setInitialState; }
    void pushCurrentState(State &newState) {
        return Base::pushCurrentState();// ie: saved_tate = current_state;
    }
};

template <typename Base>
class ErrorState : public ErrorStateBase<Base> {
    // error states are outside of the normal processing for a state machine;
    // they cause other processing to halt and trigger receipt of a message: ERROR
    void setError(int val) {
        if (error_state) {
            return;
        }
        error_state = val;
        if (val) {
            Base::pushCurrentState();
            const State *err = Base::findState("ERROR");
            assert(err && "class implementing ErrorState does not have a state named ERROR");
            Base::setState(*err);
        }
    }

    // Error states (not used yet)
    bool inError() { return error_state != 0; }

    void resetError() {
        error_state = 0;
        Base::setInitialState();
    }

    void ignoreError() { error_state = 0; }


private:
    int error_state;   // error number of the current error if any
};
