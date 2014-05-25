# This script demonstrates various builtin special values
#

S1 CONSTANT "STR ";
S2 CONSTANT "STR1 ";
S3 CONSTANT "2STR ";

Test MACHINE {

	ENTER INIT { 
		LOG "one"; 
		LOG "two " + 2; 
		LOG "now: " + YEAR + "/" + MONTH + "/" + DAY + " " + HOUR + ":" + MINUTE + ":" + SECONDS + " " + TIMEZONE;
		LOG "now: " +YR +MONTH+" "+ (((HOUR*60)+MINUTE)*60 + SECONDS);
		LOG TIMESTAMP + " test"; 

		a := "12";
		b := "3";
		c := a + b;
		d := 0 + a + b;
		LOG c;
		LOG d;

		LOG "1X" + 2;
		LOG S1 + NOW;
		LOG S2 + NOW;
		LOG S3 + NOW;
	}
}
logger Test(tab:tests);
