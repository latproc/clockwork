# This script demonstrates various builtin special values
#

S1 CONSTANT "STR ";
S2 CONSTANT "STR1 ";
S3 CONSTANT "2STR ";

Test MACHINE {

	COMMAND go { 
		LOG "one"; 
		LOG "two " + 2; 
		LOG "now: " + YEAR + "/" + MONTH + "/" + DAY + " " 
			+ HOUR + ":" + MINUTE + ":" + SECONDS + " " + TIMEZONE;
		LOG "now: " +YR +MONTH+" "+ (((HOUR*60)+MINUTE)*60 + SECONDS);
		LOG "timezone: " + TIMEZONE;
		LOG "random: " + RANDOM;
		LOG TIMESTAMP + " test"; # make sure timestamp can be the prefix of the line
		LOG "timestamp: " + TIMESTAMP + " test"; 
		LOG "utc: " + UTCTIMESTAMP;
		LOG "iso: " + ISOTIMESTAMP;

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
    ts := TIMESEQ;
	}
}
logger Test(tab:tests);


