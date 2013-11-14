# This script demonstrates various builtin special values
#
Test MACHINE {

	ENTER INIT { 
		LOG "one"; 
		LOG "two " + 2; 
		LOG "now: " + YEAR + "/" + MONTH + "/" + DAY + " " + HOUR + ":" + MINUTE + ":" + SECONDS + " " + TIMEZONE;
		LOG "now: " +YR +MONTH+" "+ (((HOUR*60)+MINUTE)*60 + SECONDS);
		LOG TIMESTAMP + " test"; 
	}
}
logger Test(tab:tests);
