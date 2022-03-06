# Read a file

data VARIABLE (export:str, strlen:2000) "";
reader Reader data;

Reader MACHINE data {
  OPTION filename "";

  read_command SystemExec;

  error WHEN read_command IS Error && read_command.Errors != "";
  done WHEN read_command IS Done;
  busy WHEN read_command IS NOT Idle;
  idle DEFAULT;

  COMMAND read WITHIN idle {
    read_command.Command := "/bin/cat " + filename;
    SEND start TO read_command;
  }
  ENTER done {
    data := read_command.Result;
    SET read_command TO Idle;
  }
}

