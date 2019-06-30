# A basic one-shot timer with reset
# 
# use start to start the timer
# after running for timeout ms the timer becomes expired
OneShotTimer MACHINE {
	OPTION timeout 100;
	expired WHEN SELF IS expired || (SELF IS running && TIMER >= timeout);
	running STATE;
	stopped INITIAL;

	TRANSITION expired,stopped TO running ON start;
	TRANSITION running,expired,stopped TO stopped ON reset;
}

