# The aim of this test is to try to break clockwork by a convoluted
# set of actions that use CALL statements to invoke actions on 
# a machines owner even though that machine may be busy working
# on another action.

# a Check is a machine that looks at it's x value and asks it's owner for a 'fix' 
# if this value is not 0

Check MACHINE {
    OPTION x 0;
    Fix WHEN x != 0; ENTER Fix { CALL fix ON OWNER; }
    Done WHEN x == 0;
}

# The CommandTest machine owns a couple of Checks and accepts a 
# fix command that updates a counter but otherwise does not
# actually apply a fix.

CommandTest MACHINE {

    a Check; b Check;
    OPTION count 0;
    one STATE; 
    
    # when we enter state one, we cause our a Check to enter a fix state and then forceably move it back
    # do Done. The a check should notice that it still has a non-zero x and should move back to 
    # the fix state again.  
    ENTER one { a.x := 1; WAIT 100; SET a TO Done; b.x := 1; b.x := 0;}  
     
    COMMAND fix { LOG "fix"; count := count + 1; }    

    RECEIVE a.Fix_enter { LOG "a -> fix" }
    RECEIVE b.Fix_enter { LOG "b -> fix" }
    RECEIVE a.Done_enter { LOG "a -> done" }
    RECEIVE b.Done_enter { LOG "b -> done" }
 }
test CommandTest;

# A static machine has an x value and an initial state but otherwise
# does nothing.

Static MACHINE { OPTION x 0;  idle INITIAL; }

# We log Check transtions to fix or done
   
