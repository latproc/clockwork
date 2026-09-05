# LOCAL and PERSISTENT are mutually exclusive: declaring the same field both
# ways is a load-time error. Parse test: must fail (non-zero exit).

Bad MACHINE {
    LOCAL OPTION x 0;
    PERSISTENT OPTION x 1;
}

b Bad;
