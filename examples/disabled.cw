# test the use of the DISABLED operator

Idle MACHINE { 
	OPTION startup_enabled false;
}
idle Idle;

Test MACHINE idler {
	inactive WHEN idler DISABLED;
	active WHEN NOT idler DISABLED;
	invalid DEFAULT;
}
test Test idle;

# we can also test whether any machines in a list are 
# enabled or disabled:
#

m1 Idle; m2 Idle;
idle_list LIST m1, m2;

ListTest MACHINE idlers {
    inactive WHEN ANY idlers DISABLED;
    active WHEN ALL idlers ENABLED;
    invalid DEFAULT;
}
list_test ListTest idle_list;

