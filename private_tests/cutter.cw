/*
 management of a cutting process

 pick a cutting pattern: 18 bits, first 9 are shallow, next are deep
 
 define POS1..POSn as 1 << (n-1)
 
 cut1 := POS1 | POS3 | POS14;
 cut2 := POS2 | POS4 | POS15;
 
 shallow_mask := POS1 | POS2 | POS3 | POS4 | POS5 | POS6 | POS7 | POS8 | POS9;
 deep_mask    :=  POS10 | POS11 | POS12 | POS13 | POS14 | POS15 | POS16 | POS17 | POS18;
 carriage1_mask = POS1 | POS2 | POS3 | POS4 | POS5 | POS6 | POS7 
                  | POS10 | POS11 | POS12 | POS13 | POS14 | POS15 | POS16
 carriage2_mask = POS8 | POS9 | POS17 | POS18

 cutting instructions: 

*/

one FLAG (tab:Test);
two FLAG (tab:Test);

checker Check(tab:Test) one, two;

Check MACHINE in, out {

    OPTION in_code 0;
    OPTION out_code 0;
    OPTION code 0;
    
    output_failed_to_change_fault WHEN in_code == 3;   /* input came on and off without seeing an output */
    input_failed_to_change_fault WHEN out_code == 12;  /* output came on and off without seeing an input */
    
    ready WHEN code == 9                   /* in came on, out went off */
            || code == 6;                  /* in went off, out came on */
    waiting WHEN in_code == 0 || out_code == 0;

    fault DEFAULT;                         /* everything else is a fault */
    
    ENTER ready { 
        CALL reset ON SELF;
    }
    COMMAND reset { in_code := 0; out_code := 0; code := 0; }
    
    RECEIVE in.on_enter { 
        in_code := in_code + 1; 
        code := in_code + out_code; 
    }
    RECEIVE in.off_enter { 
        in_code := in_code + 2; 
        code := in_code + out_code;
    }
    RECEIVE out.on_enter { 
        out_code := out_code + 4;
        code := in_code + out_code;
    }
    RECEIVE out.off_enter { 
        out_code := out_code + 8; 
        code := in_code + out_code;
    }
}

Checker2 MACHINE in, out {

    OPTION code 0;

    waiting WHEN code == 0;
    started WHEN code == 1 || code == 2 || code == 4 || code == 8;
    ready WHEN code == 6 || code == 9;
    fault DEFAULT;

    ENTER ready { CALL reset ON SELF; }

    COMMAND reset { code := 0; }
    
    RECEIVE in.on_enter {
        code := code + 1
    }
    RECEIVE in.off_enter {
        code := code + 2
    }
    RECEIVE out.on_enter {
        code := code + 4
    }
    RECEIVE out.off_enter {
        code := code + 8
    }
}

checker2 Checker2 (tab:Test) one, two;
