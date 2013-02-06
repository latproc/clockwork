Sample MACHINE {
	ENTER INIT {
		LOG "CRLF: \r\n";
		LOG "Octal 2,3,8,255: \002\003\010\377";
        x := SELF.NAME + " test";
        LOG x; # this needs to be fixed
        SHUTDOWN;
	}
}
sample Sample; 
