# this loads properties from a file
#
# to run, prepare a file "property_file.dat" in the current directory
# with the text between the comment markers below:
/*
sample a 100
sample b "loaded value"
sample c 12.34
sample d FALSE
sample name machine_name
*/

Sample MACHINE {
  OPTION a 1;
  OPTION b "string";
  OPTION c 0.2;
  OPTION name x;
  OPTION property_file "property_file.dat";

  COMMAND load {
    LOAD PROPERTIES FROM property_file;
  }
}
sample Sample;

