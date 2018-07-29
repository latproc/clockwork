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

