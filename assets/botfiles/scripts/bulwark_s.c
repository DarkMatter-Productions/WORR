//===========================================================================
//
// Name:			bulwark_s.c
// Function:		armor anchor script for Bulwark
// Source:			WORR original Q3-style BotLib seed
// Scripter:		Worker R
// Last update:		2026-06-18
// Tab Size:		4 (real tabs)
//===========================================================================

script "main"
{
	//
	point("bulwark armor anchor", -160, 64, 32);
	point("bulwark cover lane", -96, 160, 40);
	point("bulwark close door", -184, 144, 36);
	point("bulwark fallback corner", -224, 80, 32);
	//
	box("bulwark armor anchor box", -24, -24, -24, 24, 24, 48);
	box("bulwark cover lane box", -24, -24, -24, 24, 24, 48);
	box("bulwark close door box", -22, -22, -24, 22, 22, 46);
	box("bulwark fallback corner box", -24, -24, -24, 24, 24, 48);
	//
	movebox("bulwark armor anchor box", "bulwark armor anchor");
	movebox("bulwark cover lane box", "bulwark cover lane");
	movebox("bulwark close door box", "bulwark close door");
	movebox("bulwark fallback corner box", "bulwark fallback corner");
	//
	say("Bulwark holding armor.", NULL);
	wave("affirmative");
	selectweapon(8);
	moveto("bulwark armor anchor box");
	wait(touch(0, "bulwark armor anchor box"));
	aim("bulwark cover lane");
	wait(time(0.25));
	moveto("bulwark cover lane box");
	wait(touch(0, "bulwark cover lane box"));
	aim("bulwark close door");
	fireweapon();
	wait(time(0.40));
	say("Armor side is covered.", NULL);
	moveto("bulwark close door box");
	wait(touch(0, "bulwark close door box"));
	aim("bulwark fallback corner");
	moveto("bulwark fallback corner box");
	wait(touch(0, "bulwark fallback corner box"));
} //end script main

script "anchor_hold"
{
	//
	point("bulwark armor stack", -168, 56, 32);
	point("bulwark door check", -128, 128, 40);
	point("bulwark reserve step", -216, 112, 32);
	//
	box("bulwark armor stack box", -24, -24, -24, 24, 24, 48);
	box("bulwark door check box", -22, -22, -24, 22, 22, 46);
	box("bulwark reserve step box", -24, -24, -24, 24, 24, 48);
	//
	movebox("bulwark armor stack box", "bulwark armor stack");
	movebox("bulwark door check box", "bulwark door check");
	movebox("bulwark reserve step box", "bulwark reserve step");
	//
	say("Bulwark anchoring armor.", NULL);
	wave("affirmative");
	selectweapon(8);
	moveto("bulwark armor stack box");
	wait(touch(0, "bulwark armor stack box"));
	aim("bulwark door check");
	wait(time(0.30));
	fireweapon();
	moveto("bulwark door check box");
	wait(touch(0, "bulwark door check box"));
	aim("bulwark reserve step");
	wait(time(0.20));
	moveto("bulwark reserve step box");
	wait(touch(0, "bulwark reserve step box"));
} //end script anchor_hold
