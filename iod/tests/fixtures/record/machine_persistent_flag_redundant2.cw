# OPTION PERSISTENT true is redundant/illegal when the class also declares
# per-field PERSISTENT OPTION (per-field listed first; order-independent).

Panel MACHINE {
    PERSISTENT OPTION sp 0;
    OPTION PERSISTENT true;
}
