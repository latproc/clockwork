# OPTION PERSISTENT true (machine-level flag): valid on a plain MACHINE.

BALEPANELDETAIL MACHINE {
    OPTION PERSISTENT true;
    OPTION GrabsPerBale 0;
    OPTION LotSize 0;
    OPTION BaleNo 1;
    OPTION CoresPerBale 0;
    EXPORT RW 32BIT GrabsPerBale, LotSize, BaleNo, CoresPerBale;
    Idle INITIAL;
}
b BALEPANELDETAIL;
