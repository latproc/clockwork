# dynamic value tests

# various predicate tests involving list operations
name_list LIST "Sally", "Jim", "Mary";
Test1 MACHINE names{
	enabled FLAG;
	# use the pop command to activate the following
	s1 WHEN FIRST OF names == "Sally" AND SIZE OF names ==1; 
	# use the pop command to activate the following
	s2 WHEN FIRST OF names == "Sally" AND SIZE OF names ==2;
	# toggle the enabled flag to activate this
	s3 WHEN enabled IS on AND FIRST OF names == "Sally" AND SIZE OF names ==3;
	# this should be true initially
	s3 WHEN names INCLUDES "Mary" AND FIRST OF names == "Sally" AND SIZE OF names ==3;
	# use the pull command to activate this
	j WHEN FIRST OF names == "Jim";

	COMMAND pop { name := TAKE LAST FROM names; }
	COMMAND pull { name := TAKE FIRST FROM names; }
}

test1 Test1 name_list;

# An example of stepping through a list by removing items one at at time

Accumulator MACHINE {
	OPTION sum 0;

	numbers LIST 1,2,3,4,5,6,7,8,9,10;
	timer FLAG; # used to measure the time taken for the operation

	start WHEN SIZE OF numbers > 0 && timer IS off;
	working WHEN SIZE OF numbers > 0;
	done DEFAULT;
	checking STATE;

	ENTER start { SET timer TO on; }
	ENTER working { 
		x:= TAKE ITEM 0 FROM numbers; 
		sum := sum + x; 
		# trigger a state change to test reexecute this if there is more to do
		SET SELF TO checking;
	}
	ENTER done { LOG "Sum: " + sum + " time taken: " + timer.TIMER + "ms" }
}
accumulator Accumulator;

# an example of stepping through a list using an index to 
# examine items one at a time

Accumulator2 MACHINE {
	OPTION i 0, sum 10; # multiple properties can be initialised at once

	numbers LIST 1,2,3,4,5,6,7,8,9,10;
	timer FLAG; # used to measure the time taken for the operation
	working WHEN i< SIZE OF numbers;
	done DEFAULT;
	checking STATE;

	ENTER start { SET timer TO on; }
	ENTER working { 
		sum := sum + ITEM i OF numbers; 
		i:= i+1; 
		# trigger a state change to test reexecute this if there is more to do
		SET SELF TO checking  
	}
	ENTER done { LOG "Sum: " + sum + " time taken: " + timer.TIMER + "ms" }
}
accumulator2 Accumulator;


