Follow MACHINE input, output {
  active WHEN 1==1, TAG output WHEN input IS on;
}

in FLAG;
out FLAG;
follow Follow in, out;
