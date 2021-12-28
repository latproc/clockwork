Sample MACHINE {
OPTION x "";
OPTION v 0;
OPTION buf "";
OPTION result "";

	ENTER INIT {
		LOG "CRLF: \r\n";
		LOG "Octal 2,3,8,255: \002\003\010\377";
        x := SELF.NAME + " test";
        LOG x; # this needs to be fixed
        LOG -(1 + 1);
        LOG (1 + 1) AS FLOAT;
        LOG (1 + 1) AS STRING;

        v := 123;
        d := 3;
        buf := "z       " + v + "." + d;
        result := COPY `.{7}$` FROM buf;
        #SHUTDOWN;
	}
}
sample Sample(tab:Test);
