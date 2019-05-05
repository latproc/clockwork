Follow MACHINE input, output {
  inactive WHEN SELF IS inactive;
  active WHEN 1==1, TAG output WHEN input IS on;
  active DURING activate { }
  inactive DURING deactivate { }
}

in FLAG;
out FLAG;
follow Follow in, out;
