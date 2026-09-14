# Diagnostic probes

Standalone programs used to settle design questions by measurement instead of
assumption. They are **not** registered with CTest and are not part of the test
suite; they take seconds to build and are kept because their results changed
decisions and would otherwise have to be rediscovered.

Build and run one directly, for example:

    g++ -std=c++17 -o /tmp/zap_status_probe zap_status_probe.cpp -L/usr/local/lib -lzmq -lpthread
    /tmp/zap_status_probe

| Probe | Question it answers | Result |
|---|---|---|
| `zap_status_probe.cpp` | Does a ZAP handler see the client's key, and can status 300 hold a connection pending approval? | The handler sees the client's 32-byte CURVE public key. Status **300 behaves identically to 400**: no reply and no re-handshake. 400, not 300, is the right code for "not yet approved". |
| `zap_identity_probe.cpp` | Does the application layer see who connected? | The ZAP handler receives the full permanent public key. What the application sees as its routing identity was **not** conclusively determined: an earlier version of this probe printed a verdict against a hard-coded key and was wrong. The hex dump it prints is the sound output. |

Results are written up in `PLAN.md` §20.
