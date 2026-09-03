# OPTION PERSISTENT true is redundant/illegal when the class also declares
# per-field PERSISTENT OPTION (flag listed first).

Panel MACHINE {
    OPTION PERSISTENT true;
    PERSISTENT OPTION sp 0;
}
