# basic condition machine

Flasher MACHINE {

	on_cond CONDITION WHEN ((OWNER IS on AND OWNER.TIMER < 1000) OR (OWNER IS off AND OWNER.TIMER > 1000));

	on WHEN on_cond IS true;
	off DEFAULT;
}
flasher Flasher;
