
SimpleSelector MACHINE {
    f1 FLAG(n:1); f2 FLAG(n:2); f3 FLAG(n:3);
    in LIST f1,f2,f3;
    out LIST;
    COMMAND run { COPY ALL FROM in TO out WHERE in.ITEM.n == 2 }
}
#simple SimpleSelector;

JoinSelector MACHINE {
    f1 FLAG(n:1); f2 FLAG(n:2); f3 FLAG(n:3);
    in LIST f1,f2,f3;
    sel LIST f2;
    out LIST;
    COMMAND run { COPY ALL FROM in TO out SELECT USING sel WHERE in.ITEM.n == sel.ITEM.n }
}
join JoinSelector;
