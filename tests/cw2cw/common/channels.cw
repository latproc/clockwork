Link CHANNEL {
    OPTION PORT 9000;
    UPDATES flasher_a FlasherOneInterface;
    UPDATES flasher_b FlasherOneInterface;
}

FlasherOneInterface INTERFACE {
    inactive INITIAL;
    on STATE;
    off STATE;
}
