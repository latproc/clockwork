# various modbus interface tests
#
# if using device connector to update values in B, 
# run it as follows:
#

/*

$HOME/projects/latproc/github/latproc/iod/device_connector \
	--serial_port /dev/tty.usbmodem1421 --serial_settings 9600 \
  --name dev --queue queue --pattern '[0-9]+[^0-9.]+'

*/


B VARIABLE(export:float) 0.0;

DeviceInterface MACHINE output {
	queue LIST;
	busy WHEN queue IS NOT empty;
	idle DEFAULT;

	ENTER busy { 
		tmp := TAKE FIRST FROM queue; 
		x := COPY ALL `[0-9.]` FROM tmp;
		output.VALUE := 0.0 + x;
		SET SELF TO idle;
	}
}
dev DeviceInterface B;

