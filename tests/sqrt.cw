# Latproc isn't designed for numeric calculations but it can
# manage some calculations, especially now that it has
# floating point.

# here is a machine that is unstable if it's length is not
# close to the sqrt of its area. The machine trys to find
# an integer value but oscillates if the area is not a 
# perfect square.

SquareRoot MACHINE (area:10) {

    OPTION Area 0.0;
    OPTION Length 0.0;
    OPTION LSquared 0.0;
	OPTION step 0;

    ENTER INIT { Area := area; step := Area / 2.0; }
    
    stable WHEN LSquared - Area < 0.00001 * Length && LSquared - Area > -0.00001 * Length;
    low WHEN LSquared < Area;
    high WHEN LSquared > Area;
    
    calculating DURING recalc_high { Length := Length - step; LSquared := Length * Length; step := step / 2; }
    calculating DURING recalc_low { Length := Length + step; LSquared := Length * Length; step := step / 2; }

    ENTER low { IF (step == 0) {step := 1}; CALL recalc_low ON SELF }
    ENTER high {  IF (step == 0) {step := 1}; CALL recalc_high ON SELF }
}

square_root_140 SquareRoot(area:140);
square_root_10 SquareRoot(area:10);
square_root_1000 SquareRoot(area:1000);
