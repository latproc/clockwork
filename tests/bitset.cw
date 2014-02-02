f1 FLAG;
f2 FLAG;
l1 LIST f1,f2;
x1 FLAG;
x2 FLAG;
l2 LIST x1,x2;

BitsetTest MACHINE a,b{
    both WHEN BITSET FROM a == 3 && BITSET FROM a == BITSET FROM b;
    one WHEN (BITSET FROM a == 1 || BITSET FROM a == 2) && BITSET FROM a == BITSET FROM b;
    none WHEN BITSET FROM a == 0 && BITSET FROM a == BITSET FROM b;
    different WHEN BITSET FROM a != BITSET FROM b;
}
bitset_test BitsetTest l1,l2;
