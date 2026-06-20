//===========================================================================
//
// Name:			teamplay.h
// Function:		shared WORR team chat fragments and role responses
// Source:			WORR original Q3-style BotLib seed
// Tab Size:		4 (real tabs)
//===========================================================================

#define WORR_HELLO0			"Ready."
#define WORR_GOODBYE0		"Leaving the arena."
#define WORR_READY0			"Match clock is live."
#define WORR_VICTORY0		"Better routes, better timing."
#define WORR_LOSS0			"Good match. I need a cleaner line."
#define WORR_SELFOWN0		"That one was all yours."
#define WORR_TAUNT0			"You are making this route easy."
#define WORR_MISC0			"Check armor, check exits."
#define WORR_ACK0			"Copy."
#define WORR_DENY0			"Cannot commit to that route."
#define WORR_LOCATION0		"Tracking from the last safe lane."

type "whois"
{
	"WORR bot profile ", 0, ".";
	"Server-side route bot, ", 0, ".";
} //end type

type "whereis"
{
	"Last seen near ", 0, ".";
	"Check ", 0, " and the adjacent armor route.";
} //end type

type "whereareyou"
{
	WORR_LOCATION0;
	"Near ", 0, ", watching the next exit.";
} //end type

type "cannotfind"
{
	"No line to ", 0, " yet.";
	WORR_DENY0;
} //end type

type "location"
{
	"Holding near ", 0, ".";
	"Current route marker is ", 0, ".";
} //end type

type "teamlocation"
{
	"Near ", 0, " on the ", 1, " side.";
	"Watching ", 0, " from ", 1, " control.";
} //end type

type "help_start"
{
	WORR_ACK0;
	"Moving to cover ", 0, ".";
} //end type

type "accompany_start"
{
	"Escorting ", 0, ".";
	"Staying with you, ", 0, ".";
} //end type

type "accompany_stop"
{
	"Breaking escort.";
	"Escort done.";
} //end type

type "accompany_cannotfind"
{
	"I do not have your lane, ", 0, ".";
	"Cannot find your route yet, ", 0, ".";
} //end type

type "accompany_arrive"
{
	"On your shoulder, ", 0, ".";
	"Escort position reached.";
} //end type

type "accompany_flagcarrier"
{
	"Covering the carrier.";
	"Staying with our flag route.";
} //end type

type "defend_start"
{
	"Defending ", 0, ".";
	"Setting a hold at ", 0, ".";
} //end type

type "defend_stop"
{
	"Defense released.";
	"Leaving the hold.";
} //end type

type "getitem_start"
{
	"Routing to ", 0, ".";
	"Taking item route for ", 0, ".";
} //end type

type "getitem_notthere"
{
	0, " is gone.";
	"Item route is dry.";
} //end type

type "getitem_gotit"
{
	"Item secured.";
	0, " is handled.";
} //end type

type "kill_start"
{
	"Pressuring ", 0, ".";
	"Target set: ", 0, ".";
} //end type

type "kill_done"
{
	"Target down.";
	0, " is cleared.";
} //end type

type "camp_start"
{
	"Holding ", 0, ".";
	"Anchoring the lane.";
} //end type

type "camp_stop"
{
	"Leaving camp.";
	"Rotating out of the hold.";
} //end type

type "camp_arrive"
{
	"Camp set at ", 0, ".";
	"Hold point reached.";
} //end type

type "patrol_start"
{
	"Patrolling ", 0, ".";
	"Starting route sweep.";
} //end type

type "patrol_stop"
{
	"Patrol ended.";
	"Route sweep complete.";
} //end type

type "captureflag_start"
{
	"Going for the flag.";
	"Opening the capture route.";
} //end type

type "returnflag_start"
{
	"Returning our flag.";
	"Recovering the base flag.";
} //end type

type "attackenemybase_start"
{
	"Attacking enemy base.";
	"Opening pressure on their base.";
} //end type

type "harvest_start"
{
	"Collecting skulls.";
	"Harvest route started.";
} //end type

type "dismissed"
{
	"Going independent.";
	"Roaming until the next call.";
} //end type

type "joinedteam"
{
	"Joined team ", 0, ".";
	"Subteam ", 0, " acknowledged.";
} //end type

type "leftteam"
{
	"Leaving team ", 0, ".";
	"Subteam ", 0, " cleared.";
} //end type

type "inteam"
{
	"Currently with team ", 0, ".";
	"Still assigned to ", 0, ".";
} //end type

type "noteam"
{
	"No subteam assigned.";
	"Running free of subteam orders.";
} //end type

type "checkpoint_invalid"
{
	"Checkpoint is invalid.";
	"That checkpoint has no route.";
} //end type

type "checkpoint_confirm"
{
	"Checkpoint ", 0, " confirmed at ", 1, ".";
	"Route marker ", 0, " set to ", 1, ".";
} //end type

type "followme"
{
	"Moving to you, ", 0, ".";
	"Lead route accepted, ", 0, ".";
} //end type

type "lead_stop"
{
	"Lead released, ", 0, ".";
	"Breaking from your route, ", 0, ".";
} //end type

type "helping"
{
	"Helping ", 0, ".";
	"Covering support for ", 0, ".";
} //end type

type "accompanying"
{
	"Accompanying ", 0, ".";
	"Following ", 0, "'s lane.";
} //end type

type "defending"
{
	"Defending ", 0, ".";
	"Guarding the route at ", 0, ".";
} //end type

type "gettingitem"
{
	"Getting ", 0, ".";
	"Item route is set for ", 0, ".";
} //end type

type "killing"
{
	"Hunting ", 0, ".";
	"Pressure target remains ", 0, ".";
} //end type

type "camping"
{
	"Camping the assigned lane.";
	"Holding camp position.";
} //end type

type "patrolling"
{
	"Patrolling.";
	"Sweeping the assigned route.";
} //end type

type "capturingflag"
{
	"Trying to capture the flag.";
	"Flag route is active.";
} //end type

type "rushingbase"
{
	"Rushing base.";
	"Fast route to base is active.";
} //end type

type "returningflag"
{
	"Returning the flag.";
	"Recover route is active.";
} //end type

type "attackingenemybase"
{
	"Attacking enemy base.";
	"Enemy base pressure is active.";
} //end type

type "harvesting"
{
	"Harvesting.";
	"Skull route is active.";
} //end type

type "roaming"
{
	"Roaming for resources.";
	"Free route sweep is active.";
} //end type

type "wantoffence"
{
	"I can take offense.";
	"Put me on attack routes.";
} //end type

type "wantdefence"
{
	"I can take defense.";
	"Put me on hold routes.";
} //end type

type "keepinmind"
{
	"Keeping that in mind, ", 0, ".";
	"Preference noted, ", 0, ".";
} //end type

type "cmd_defendbase"
{
	"Base defense acknowledged.";
	WORR_ACK0;
} //end type

type "cmd_getflag"
{
	"Flag route acknowledged.";
	WORR_ACK0;
} //end type

type "cmd_accompany"
{
	"Accompany order acknowledged.";
	WORR_ACK0;
} //end type

type "cmd_accompanyme"
{
	"Accompany-me order acknowledged.";
	WORR_ACK0;
} //end type

type "cmd_attackenemybase"
{
	"Enemy-base attack acknowledged.";
	WORR_ACK0;
} //end type

type "cmd_returnflag"
{
	"Return order acknowledged.";
	WORR_ACK0;
} //end type

type "cmd_harvest"
{
	"Harvest order acknowledged.";
	WORR_ACK0;
} //end type

type "ctf_gotflag"
{
	"Flag taken. Cover the exit.";
	"Enemy flag is moving.";
} //end type

type "ctf_captureflag"
{
	"Capture scored.";
	"Flag route closed clean.";
} //end type

type "ctf_returnflag"
{
	"Flag returned.";
	"Our flag is safe.";
} //end type

type "ctf_defendbase"
{
	"Base defense is set.";
	"Keeping pressure out of base.";
} //end type

type "ctf_flagcarrierdeath"
{
	"Carrier down. Recover the flag.";
	"Flag route is loose.";
} //end type

type "ctf_flagcarrierkill"
{
	"Carrier killer spotted. Return the flag.";
	"Base flag carrier cleared.";
} //end type

type "whoisteamleader"
{
	"Who is leading this team?";
	"Confirm team lead.";
} //end type

type "iamteamleader"
{
	"I am taking lead.";
	"Team lead is active here.";
} //end type

type "death_teammate"
{
	"Friendly down.";
	"Lost a teammate near the route.";
} //end type

type "kill_teammate"
{
	"Bad friendly fire.";
	"Cease fire on teammates.";
} //end type
