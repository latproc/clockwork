# do commands execute on transitions

x MACHINE {
a STATE;
b STATE;

f FLAG;

RECEIVE A { SET SELF TO a; }
RECEIVE B { SET SELF TO b; }

ENTER a { LOG "a" }
ENTER b { LOG "b" }

TRANSITION a TO b USING logAB;
TRANSITION b TO a USING logBA;
TRANSITION a TO b USING f.on_enter;

COMMAND logAB { LOG "a to b" }
COMMAND logBA { LOG "b to a" }
}

xx x;
