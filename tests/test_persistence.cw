# Used to give the sampler time to connect
ChannelMonitor MACHINE {
    check WHEN SELF IS waiting AND TIMER >= 1000;
    ready WHEN SELF IS ready OR COUNT ACTIVE FROM CHANNELS > 1;
    waiting DEFAULT;

    COMMAND reset WITHIN ready { SET SELF TO waiting; }
}

Data MACHINE {
	OPTION PERSISTENT true;
  OPTION x 123;
}

Setup MACHINE data {
  monitor ChannelMonitor;
  waiting WHEN monitor IS NOT ready;
  update DEFAULT;
  ENTER update {
    LOG "data before update: " + data.x;
    data.x := 456;
    LOG "updated data.x: " + data.x;
  }
  ENTER INIT {
    LOG "data.x: " + data.x;
  }
}

x Data;
s Setup x;
