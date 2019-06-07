# copy data from record
# create record
# lookup record
# link states to values of fields
# Key fields
# validation
# cleanup


# Use case - logging packages as they pass through an rfid scanner

# There are a couple of ways that KEYVALUE objects might be processed: explicitly or via monitors
#
# Example of explicit package logging; the machine has a queue and once loaded, the
# package is detected by a monitor and the rfid key is pushed onto the logger queue for display.
# This technique does not require that the logger

ItemLog MACHINE queue {
  OPTION key ''; // nonzero value causes a lookup
  queue LIST;
  pack KEYVALUE;
  
  selecting WHEN SELF IS waiting AND SIZE OF queue > 0;
  resetting WHEN SELF IS display or SELF IS error;
  display WHEN pack IS Ready;
  error WHEN pack IS Invalid;
  waiting DEFAULT;
  
  ENTER display{ LOG pack.Value }
  ENTER error{ LOG "Unknown package key: " + pack.Value }
  
  ENTER resetting { CALL clear ON pack; }
  
  ENTER selecting {
    pack.Key := TAKE FIRST FROM queue;
  }

}

# Example of monitor-based logging of packages.
# 
# This method can be used when another machine is already loading packages
# from the keys read from an rfid scanner 
#
# this machine will display the value a package when the package is loaded and validated
# after each check, this machine can to be reset to cause a recheck of the package
# it will self reset once the package is removed, in readiness for the next one.

ValidItemMonitor MACHINE pack {
  done WHEN SELF IS done || SELF IS display AND pack IS NOT Ready;
  display WHEN pack IS Ready;
  idle DEFAULT;
  
  ENTER display{ LOG pack.Value }
  COMMAND reset { SET SELF TO idle;}
}

# This lookup machine takes a key id read from its queue and loads the package record
# from a data source. When all monitors processing the item have finished, the 
# machine resets and looks for another key to process.

Lookup MACHINE queue, monitors {
  OPTION key ''; // nonzero value causes a lookup
  queue LIST;
  pack KEYVALUE;
  
  selecting WHEN ALL monitors ARE idle AND SELF IS idle AND SIZE OF queue > 0;
  resetting WHEN ALL monitors ARE done AND (SELF IS waiting or SELF IS error);
  waiting WHEN pack IS Ready;
  error WHEN pack IS Invalid;

  idle DEFAULT;
  
  ENTER display{ LOG pack.Value }
  ENTER error{ LOG "Unknown package key: " + pack.Value }
  
  ENTER resetting { CALL clear ON pack; }
  
  ENTER selecting {
    pack.Key := TAKE FIRST FROM queue;
  }

}


redis DATASOURCE(driver: 'redis')
pack Package redis, ""


