Fixing MACHINE {
    OPTION val 0;
    OPTION pos 3;
    fixing WHEN val != pos*3 * 50/80;
    idle DEFAULT;
    
    ENTER fixing { val := pos*3 * 50/80 }
}
fixing Fixing;
