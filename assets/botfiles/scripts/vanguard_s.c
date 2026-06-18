//===========================================================================
//
// Name:			vanguard_s.c
// Function:		front-line route script for Vanguard
// Source:			WORR original Q3-style BotLib seed
// Scripter:		Worker R
// Last update:		2026-06-18
// Tab Size:		4 (real tabs)
//===========================================================================

script "main"
{
	//
	point("vanguard rally", 160, 48, 32);
	point("vanguard choke", 224, 112, 40);
	point("vanguard high pressure", 288, 144, 48);
	point("vanguard quad door", 352, 96, 40);
	//
	box("vanguard rally box", -22, -22, -24, 22, 22, 48);
	box("vanguard choke box", -22, -22, -24, 22, 22, 48);
	box("vanguard high pressure box", -24, -24, -24, 24, 24, 52);
	box("vanguard quad door box", -22, -22, -24, 22, 22, 48);
	//
	movebox("vanguard rally box", "vanguard rally");
	movebox("vanguard choke box", "vanguard choke");
	movebox("vanguard high pressure box", "vanguard high pressure");
	movebox("vanguard quad door box", "vanguard quad door");
	//
	say("Vanguard moving to pressure.", NULL);
	selectweapon(11);
	moveto("vanguard rally box");
	wait(touch(0, "vanguard rally box"));
	aim("vanguard choke");
	wait(time(0.18));
	moveto("vanguard choke box");
	wait(touch(0, "vanguard choke box"));
	aim("vanguard high pressure");
	fireweapon();
	wait(time(0.20));
	say("Choke is open.", NULL);
	moveto("vanguard high pressure box");
	wait(touch(0, "vanguard high pressure box"));
	aim("vanguard quad door");
	fireweapon();
	moveto("vanguard quad door box");
	wait(touch(0, "vanguard quad door box"));
} //end script main
