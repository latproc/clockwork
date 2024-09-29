sample SampleSignConversion;

SampleSignConversion MACHINE {
    OPTION unsigned 0;
    OPTION signed 0;
    OPTION VALUE 4294967275;

    LOCAL OPTION lastSigned 0;
    LOCAL OPTION lastUnsigned 0;

    recalcSigned WHEN unsigned != lastUnsigned;
    recalcUnsigned WHEN signed != lastSigned;
    idle DEFAULT;

    recalcSigned LOCAL STATE;
    recalcUnsigned LOCAL STATE;
    idle LOCAL STATE;

    COMMAND toSigned {
       IF (unsigned > 0x7FFFFFFF) {
           signed :=  (unsigned - 0x100000000);
       } ELSE {
           signed := unsigned;
       };
       lastSigned := signed;
       LOG "signed: " + signed;
    }

    COMMAND toUnsigned {
       IF (signed < 0) {
           unsigned :=  0x100000000 + signed;
       } ELSE {
           unsigned := signed;
       };
       lastUnsigned := unsigned;
       LOG "unsigned: " + unsigned;
    }

    ENTER recalcSigned { lastUnsigned := unsigned; CALL toSigned; }
    ENTER recalcUnsigned { lastSigned := signed; CALL toUnsigned; }
}

