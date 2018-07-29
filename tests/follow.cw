# examples of machines following each other

# a follower with set states directly monitoring another machine
FollowOne MACHINE other {
	on WHEN other IS on;
	off WHEN other IS off;
	unknown DEFAULT;
}

master1 FLAG;
slave1 FollowOne master1;


#a helper that tries to make a machine follow another
FollowHelper MACHINE leader, follower {
	update WHEN follower.STATE != leader.STATE;
	idle DEFAULT;
	ENTER update { SET follower TO leader.STATE; }
}

helper FollowHelper leader1, follower1;
leader1 FLAG;
follower1 FLAG;

#saver - a helper machine that saves the state of the leader and 
# applies it to a follower

StateSaver MACHINE leader, follower {
	OPTION saved_state "INIT";

	update WHEN follower.STATE != saved_state;
	ENTER update { LOG "setting follower"; SET follower TO saved_state; LOG "done"; }
	idle DEFAULT;

	ENTER INIT { saved_state := leader.STATE; }

	RECEIVE leader.on_enter { saved_state := leader.STATE; }
	RECEIVE leader.off_enter { saved_state := leader.STATE; }

}
saver StateSaver leader2, follower2;
leader2 FLAG;
follower2 FLAG;

