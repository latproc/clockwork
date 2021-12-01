# This script demonstrates various builtin special values
#

S1 CONSTANT "STR ";
S2 CONSTANT "STR1 ";
S3 CONSTANT "2STR ";

Test MACHINE {

	COMMAND go {
    LOG "NOW " + NOW;
		LOG "random: " + RANDOM;
		LOG "now: " + YEAR + "/" + MONTH + "/" + DAY + " "
			+ HOUR + ":" + MINUTE + ":" + SECONDS + " " + TIMEZONE;
		LOG "seconds today: " + (((HOUR*60)+MINUTE)*60 + SECONDS);
		LOG "timezone: " + TIMEZONE;
		LOG TIMESTAMP + " test"; # make sure timestamp can be the prefix of the line
		LOG "timestamp: " + TIMESTAMP + " test";
		LOG "utc: " + UTCTIMESTAMP;
    LOG "utctime: " + UTCTIME;     # yyyy/mm/dd hh:mm:ss.sss
    LOG "localtime: " + LOCALTIME; # yyyy/mm/dd hh:mm:ss.sss
		LOG "iso: " + ISOTIMESTAMP;    # ISO standard yyyymmddThhmmssZ

    # String concatenation vs numeric addition with implicit string conversion
		a := "12";
		b := "3";
		c := a + b; # string concatenation
		d := 0 + a + b; # numeric sum
		LOG c;
		LOG d;

		LOG "1X" + 2;

    # Appending a special value to a CONSTANT
		LOG S1 + NOW;
		LOG S2 + NOW;
		LOG S3 + NOW;
	}
}
logger Test(tab:tests);


