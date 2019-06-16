Follow MACHINE input, output {
  inactive WHEN SELF IS inactive;
  active WHEN 1==1, TAG output WHEN input IS on;
  active DURING activate { }
  inactive DURING deactivate { }
}

in FLAG;
out FLAG;
follow Follow in, out;

Pulse MACHINE {
  idle WHEN 1==1,
	TAG flag WHEN flag IS off;

  flag FLAG;
}
pulse Pulse;
