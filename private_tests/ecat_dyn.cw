#LOAD "ethercat.so";

EL1814 MACHINE(position:0) EXTENDS MODULE {
    sm0 STRUCT(index:0, direction:OUTPUT);
    pdo0 STRUCT (index:0x1608) sm0;
    out1 ETHERCAT_ENTRY (index:0x7080, subindex:1, bitlen:1) pdo0;
}

PinballParts MACHINE(tab:pinball);
Flipper MACHINE EXTENDS PinballParts {
    # this machine automatically has a 'tab' property with the value 'pinball'
}
