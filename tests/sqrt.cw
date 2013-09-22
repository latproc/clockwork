# Latproc isn't designed for numeric calculations, for one thing
# it has only integer numbers.

# here is a machine that is unstable if it's length is not
# close to the sqrt of its area. The machine trys to find
# an integer value but oscillates if the area is not a 
# perfect square.

SquareRoot MACHINE (area:10) {

    OPTION Area 0;
    OPTION Length 0;
    OPTION LSquared 0;

    ENTER INIT { Area := area * 1000; step := Area / 2; }
    
    stable WHEN LSquared - Area < 1 && LSquared - Area > -1;
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
