/*
 This demonstrates a method of handling numeric data from device connector.
 It assumes device connector is configured to push numbers in string form 
 with format "999.9" onto a queue.
This program 
	* pulls the items off, 
	* prepends a "1" to the number to retain the zero digits,
	* copies the digits into a new string to discard the decimal point
	* and subtracts 10000 from the numeric value to offset the "1" prepended earlier 
*/ 
Adder MACHINE {
queue LIST;

  getWeight WHEN SELF IS idle && SIZE OF queue > 0;
  idle DEFAULT;

ENTER getWeight {
	    raw := TAKE FIRST FROM queue;
    tmp := COPY `[0-9][0-9]+` FROM raw;
    n_tmp := "1" + tmp;
    tmp := n_tmp;
    n_tmp := COPY ALL `[0-9]` FROM tmp;
    Weight := 0 + n_tmp - 10000;
}

