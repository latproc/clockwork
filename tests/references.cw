# These machines demonstrate the use of references

Sample1 MACHINE {

  content REFERENCE(tab:Refs);

  holding WHEN content IS ASSIGNED;
  waiting WHEN content IS EMPTY;
  unknown DEFAULT;

  RECEIVE content.ASSIGNED_enter { LOG "detected new assignment" }
  RECEIVE content.EMPTY_enter { LOG "detected clear" }
  RECEIVE content.changed { LOG "detected change of assignment" }

}

item1 FLAG(tab:Refs);
item2 FLAG(tab:Refs);

Manager MACHINE sampler {

	COMMAND one { LOG "item 1 assigned to sampler"; ASSIGN item1 TO sampler.content }
	COMMAND two { LOG "item 2 assigned to samlper"; ASSIGN item2 TO sampler.content }
	COMMAND clear { LOG "cleared assignment"; CLEAR sampler.content; }
}
sampler Sample1(tab:Refs);
manager Manager(tab:Refs) sampler;

# test sending messages to a reference
f1 FLAG;
f2 FLAG;
f3 FLAG;
f4 FLAG;
all_flags LIST f1,f2,f3,f4;

Messages MACHINE flags {
  OPTION i 0;
  ref REFERENCE;
  ENTER INIT { x := ITEM i OF flags; ASSIGN x TO ref; }
  COMMAND clear { CLEAR ref; }
  COMMAND next { i := (i + 1) % (SIZE OF flags); x := ITEM i OF flags; ASSIGN x TO ref; }
  COMMAND call { CALL turnOn ON ref.ITEM }
  COMMAND on { SEND turnOn TO ref }
  COMMAND off { SEND turnOff TO ref }

  RECEIVE ref.changed { LOG "ref changed" }
}
msg Messages all_flags;

Pulser2 MACHINE {
    pulse FLAG;
	waiting WHEN SELF IS waiting;
    on WHEN pulse IS on;
    running WHEN TIMER >= 0, TAG pulse WHEN TIMER >= 1000;
	waiting INITIAL;
	running DURING start {}
}

TimeClock MACHINE target{
    pulser Pulser2;
	
    RECEIVE pulser.on_enter { CALL  next ON target; LOG "tick"; CALL call ON target }
}
clock TimeClock msg;


