# Property exchange testing

# These items are shared across a channel. Each side has its 
# own item and a shadow of the other one

Item MACHINE {
    OPTION barcode 0;
    OPTION serial 0;
}

ItemInterface INTERFACE {
    OPTION barcode 0;
    OPTION serial 0;
}

DataChannel CHANNEL {
	OPTION PORT 12000;
	UPDATES item1 ItemInterface;
	UPDATES item2 ItemInterface;
}

Station MACHINE prev, next {

entry ITEM;
exit ITEM;

	working STATE;
	idle STATE;

	RECEIVE prev.working_enter;
	ENTER working { CALL Process ON SELF; }
	ENTER idle { 

COMMAND Process {
	exit.barcode := entry.barcode;
	exit.serial := entry.serial;
	SET SELF TO idle;
}

COMMAND CopyPrev {
	entry.barcode := prev.exit.barcode;
	entry.serial := prev.exit.serial;
}

}

ifdef(`SERVER',`
# items instantiated at the server
a ITEM;
b ITEM;

',`
# items instantiated by the client
c ITEM;
d ITEM;

')

