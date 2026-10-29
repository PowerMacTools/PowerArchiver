#include "Finder.r"
#include "Processes.r"

resource 'SIZE' (-1) {
	reserved,
	ignoreSuspendResumeEvents,
	reserved,
	canBackground,
	needsActivateOnFGSwitch,
	backgroundAndForeground,
	dontGetFrontClicks,
	ignoreChildDiedEvents,
	is32BitCompatible,
	isHighLevelEventAware,
	onlyLocalHLEvents,
	notStationeryAware,
	dontUseTextEditServices,
	reserved,
	reserved,
	reserved,
	64 * 1024 * 1024,
	24 * 1024 * 1024
};

/* The Finder icon. The icon family (ID 128) is generated from
   icons/PowerArchiver_Icon.png by mac_icon.py, and the creator code has to
   match the one CMakeLists.txt gives add_application(). */
resource 'BNDL' (128) {
	'PwAr',
	0,
	{
		'ICN#', { 0, 128 },
		'FREF', { 0, 128, 1, 129 }
	}
};

resource 'FREF' (128) {
	'APPL',
	0,
	""
};

/* Any file can be dropped on the application, since the type of an archive
   is checked when it's opened. */
resource 'FREF' (129) {
	'****',
	0,
	""
};

/* The owner resource, named after the creator code, which the BNDL needs. */
data 'PwAr' (0, "Owner resource") {
	$"00"
};
