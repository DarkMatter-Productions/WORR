//===========================================================================
//
// Name:			smoke_s.c
// Function:		rocket route script for Smoke
// Source:			WORR original Q3-style BotLib seed
// Scripter:		Worker R
// Last update:		2026-06-18
// Tab Size:		4 (real tabs)
//===========================================================================

script "main"
{
	//
	point("smoke spawn", 0, 0, 24);
	point("smoke rocket step", 96, 0, 32);
	point("smoke bounce angle", 128, -80, 36);
	point("smoke reload cover", 48, -128, 32);
	//
	box("smoke spawn box", -16, -16, -24, 16, 16, 40);
	box("smoke rocket step box", -18, -18, -24, 18, 18, 44);
	box("smoke bounce angle box", -20, -20, -24, 20, 20, 44);
	box("smoke reload cover box", -18, -18, -24, 18, 18, 44);
	//
	movebox("smoke spawn box", "smoke spawn");
	movebox("smoke rocket step box", "smoke rocket step");
	movebox("smoke bounce angle box", "smoke bounce angle");
	movebox("smoke reload cover box", "smoke reload cover");
	//
	say("Smoke checking rockets.", NULL);
	selectweapon(14);
	moveto("smoke spawn box");
	wait(touch(0, "smoke spawn box"));
	aim("smoke rocket step");
	wait(time(0.20));
	moveto("smoke rocket step box");
	wait(touch(0, "smoke rocket step box"));
	aim("smoke bounce angle");
	fireweapon();
	wait(time(0.28));
	moveto("smoke bounce angle box");
	wait(touch(0, "smoke bounce angle box"));
	say("Reloading under cover.", NULL);
	aim("smoke reload cover");
	moveto("smoke reload cover box");
	wait(touch(0, "smoke reload cover box"));
} //end script main
