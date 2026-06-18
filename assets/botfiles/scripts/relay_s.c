//===========================================================================
//
// Name:			relay_s.c
// Function:		support patrol script for Relay
// Source:			WORR original Q3-style BotLib seed
// Scripter:		Worker R
// Last update:		2026-06-18
// Tab Size:		4 (real tabs)
//===========================================================================

script "main"
{
	//
	point("relay support start", 32, -96, 24);
	point("relay cell stack", 128, -64, 32);
	point("relay health return", 160, -144, 32);
	point("relay teammate lane", 64, -192, 28);
	//
	box("relay support start box", -20, -20, -24, 20, 20, 44);
	box("relay cell stack box", -20, -20, -24, 20, 20, 44);
	box("relay health return box", -20, -20, -24, 20, 20, 44);
	box("relay teammate lane box", -22, -22, -24, 22, 22, 44);
	//
	movebox("relay support start box", "relay support start");
	movebox("relay cell stack box", "relay cell stack");
	movebox("relay health return box", "relay health return");
	movebox("relay teammate lane box", "relay teammate lane");
	//
	say("Relay rotating for cells.", NULL);
	selectweapon(15);
	moveto("relay support start box");
	wait(touch(0, "relay support start box"));
	aim("relay cell stack");
	wait(time(0.30));
	moveto("relay cell stack box");
	wait(touch(0, "relay cell stack box"));
	say("Supplies on me.", NULL);
	aim("relay health return");
	moveto("relay health return box");
	wait(touch(0, "relay health return box"));
	aim("relay teammate lane");
	fireweapon();
	moveto("relay teammate lane box");
	wait(touch(0, "relay teammate lane box"));
} //end script main
