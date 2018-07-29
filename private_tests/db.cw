# this is an example of a machine that turns on when an input
# has been on for at least 10ms and turns off as soon as it 
# sees the input off.

DebounceOn MACHINE input {
    on WHEN input IS on AND input.TIMER >10;
    off DEFAULT;
}
raw_in FLAG;
x DebounceOn raw_in;


