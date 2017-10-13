#include "script.h"

#include <sstream>
#include <simpleini/SimpleIni.h>
#include <menu.h>

#include "Patching/pattern.h"
#include "Patching/Hooking.h"

#include "Util/Paths.h"
#include "Util/Util.hpp"
#include "Util/Logger.hpp"

#include "presets.h"
#include "settings.h"

#include "../../GTAVManualTransmission/Gears/Memory/VehicleExtensions.hpp"
#include "Util/Versions.h"

NativeMenu::Menu menu;

std::string settingsGeneralFile;
std::string settingsMenuFile;
std::string savedCarsFile;
std::string presetCarsFile;

Hash model;
Vehicle vehicle;
Vehicle prevVehicle;
VehicleExtensions ext;
Player player;
Ped playerPed;

int prevNotification;

std::vector<Preset> presets;
std::vector<Preset> saved;

Settings settings;

// The current values, updated by getStats
float g_frontCamber;
float g_frontTrackWidth;
float g_frontHeight;

float g_rearCamber;
float g_rearTrackWidth;
float g_rearHeight;

float g_visualHeight;
int slamLevel = 0;

bool autoApplied = false;

//0x000 - ?
//0x004 - toe
const int offsetCamber = 0x008;
//0x00C - padding? Can't see difference + no change @ bump
const int offsetCamberInv = 0x010; // 
//0x014 - offsetYPos but also affects camber?
//0x018 - height-related?
//0x01C - padding? Can't see difference + no change @ bump
const int offsetTrackWidth2 = 0x020; // Same as 0x030, can't see difference tho
//0x024 - ???
//0x02C - padding? Can't see difference + no change @ bump
//0x028 - height + 0x128?
const int offsetTrackWidth = 0x030;
const int offsetYPos = 0x034;
const int offsetHeight = 0x038; // affected by hydraulics! 0x028 also.
//0x03C - padding? Can't see difference + no change @ bump
//0x128 - physics-related?
//0x12C - ??
//0x130 - stiffness?


// Keep track of menu highlight for control disable while typing
bool showOnlyCompatible = false;

// Instructions that access suspension members, 1.0.1032.1
// GTA5.exe + F1023B - F3 0F11 43 28	- movss[rbx + 28], xmm0		; ???
// GTA5.exe + F10240 - F3 44 0F11 63 20 - movss[rbx + 20], xmm12	; ???
// GTA5.exe + F10246 - F3 44 0F11 4B 24 - movss[rbx + 24], xmm9		; ???
// GTA5.exe + F1024C - F3 0F11 73 30	- movss[rbx + 30], xmm6		; track width
// GTA5.exe + F10251 - F3 0F11 5B 34	- movss[rbx + 34], xmm3		; ???
// GTA5.exe + F10256 - F3 0F11 63 38	- movss[rbx + 38], xmm4		; height

// FiveM (1.0.505.2)
// FiveM... + F01E9B - F3 0F11 43 24	- movss[rbx + 28], xmm0		; ???
// FiveM... + F01EA0 - F3 0F11 5B 28	- movss[rbx + 20], xmm3		; ???
// FiveM... + F01EA5 - F3 44 0F11 63 20 - movss[rbx + 24], xmm12	; ???
// FiveM... + F01EAB - F3 0F11 4B 30	- movss[rbx + 30], xmm1		; track width
// FiveM... + F01EB0 - F3 0F11 5B 34	- movss[rbx + 34], xmm6		; ???
// FiveM... + F01EB5 - F3 0F11 63 38	- movss[rbx + 38], xmm4		; height
bool patched = false;

typedef void(*SetHeight_t)();

CallHookRaw<SetHeight_t> * g_SetHeight;

extern "C" void compare_height();
//extern "C" void original_thing();

void SetHeight_Stub() {
	compare_height();
	//original_thing();
}

void patchHeightReset() {
	if (patched)
		return;

	uintptr_t result;
	uintptr_t offset;
	if (getGameVersion() == VER_1_0_877_1_NOSTEAM) {
		result = BytePattern((BYTE*)
							 "\xF3\x0F\x11\x43\x24"
							 "\xF3\x0F\x11\x5B\x28"
							 "\xF3\x44\x0F\x11\x63\x20"
							 "\xF3\x0F\x11\x4B\x30"
							 "\xF3\x0F\x11\x73\x34"
							 "\xF3\x0F\x11\x63\x38",
							 "xxx?x"
							 "xxx?x"
							 "xxxx?x"
							 "xxxxx"
							 "xxxxx"
							 "xxxxx").get();
		offset = 26;
	}
	else {
		result = BytePattern((BYTE*)
							 "\xF3\x0F\x11\x43\x28"
							 "\xF3\x44\x0F\x11\x63\x20"
							 "\xF3\x44\x0F\x11\x4B\x24"
							 "\xF3\x0F\x11\x73\x30"
							 "\xF3\x0F\x11\x5B\x34"
							 "\xF3\x0F\x11\x63\x38",
							 "xxx?x"
							 "xxxx?x"
							 "xxxx?x"
							 "xxx?x"
							 "xxx?x"
							 "xxx?x").get();
		offset = 27;
	}


	if (result) {
		auto address = result + offset;

		std::stringstream addressFormatted;
		addressFormatted << std::hex << std::uppercase << (uint64_t)address;

		logger.Write("Patch: Patching            @ 0x" + addressFormatted.str());

		g_SetHeight = HookManager::SetCallRaw<SetHeight_t>(address, SetHeight_Stub, 5);
		logger.Write("Patch: SetCall success     @ 0x" + addressFormatted.str());

		addressFormatted.str(std::string());
		addressFormatted << std::hex << std::uppercase << (uint64_t)g_SetHeight->fn;
		logger.Write("Patch: g_SetHeight address @ 0x" + addressFormatted.str());
		patched = true;
	}
	else {
		logger.Write("Patch: No pattern found, aborting");
		patched = false;
	}
}

void unloadPatch() {
	if (!patched)
		return;

	if (g_SetHeight)
	{
		delete g_SetHeight;
		g_SetHeight = nullptr;
		logger.Write("Patch: Unloaded");
		patched = false;
	}
}

/*
 *  Update wheels info, not sure if I should move this into vehExt.
 *  It's not really useful info aside from damage.
 *  Only the left front and and left rear wheels are used atm.
 */
void getStats(Vehicle handle) {
	auto numWheels = ext.GetNumWheels(handle);
	if (numWheels < 4)
		return;

	auto wheelPtr = ext.GetWheelsPtr(handle);
	auto wheelAddr0 =	*reinterpret_cast< uint64_t *    >(wheelPtr + 0x008 * 0);
	g_frontCamber =		*reinterpret_cast< const float * >(wheelAddr0 + offsetCamber);
	g_frontTrackWidth =	   -*reinterpret_cast< const float * >(wheelAddr0 + offsetTrackWidth);
	g_frontHeight =		*reinterpret_cast< const float * >(wheelAddr0 + offsetHeight);

	auto wheelAddr2 =	*reinterpret_cast< uint64_t *    >(wheelPtr + 0x008 * 2);
	g_rearCamber =		*reinterpret_cast< const float * >(wheelAddr2 + offsetCamber);
	g_rearTrackWidth =	   -*reinterpret_cast< const float * >(wheelAddr2 + offsetTrackWidth);
	g_rearHeight =		*reinterpret_cast< const float * >(wheelAddr2 + offsetHeight);

	g_visualHeight = ext.GetVisualHeight(handle);
}

/*
 * Write new values. getStats should be called after running this with fresh
 * new values. Otherwise this should be called constantly unless I get to patching stuff.
 * Can't camber trikes and stuff for now
 */
void ultraSlam(Vehicle handle, float frontCamber, float rearCamber, float frontTrackWidth, float rearTrackWidth, float frontHeight, float rearHeight) {
	auto numWheels = ext.GetNumWheels(handle);
	if (numWheels < 4)
		return;

	auto wheelPtr = ext.GetWheelsPtr(handle);

	for (auto i = 0; i < numWheels; i++) {
		float camber;
		float trackWidth;
		float height;

		if (i == 0 || i ==  1) {
			camber = frontCamber;
			trackWidth = frontTrackWidth;
			height = frontHeight;
		} else {
			camber = rearCamber;
			trackWidth = rearTrackWidth;
			height = rearHeight;
		}

		float flip = i % 2 == 0 ? 1.0f : -1.0f; // cuz the wheels on the other side
		auto wheelAddr = *reinterpret_cast<uint64_t *>(wheelPtr + 0x008 * i);
		*reinterpret_cast<float *>(wheelAddr + offsetCamber) = camber * flip;
		*reinterpret_cast<float *>(wheelAddr + offsetCamberInv) = -camber * flip;
		*reinterpret_cast<float *>(wheelAddr + offsetTrackWidth) = -trackWidth * flip;
		*reinterpret_cast<float *>(wheelAddr + offsetHeight) = height;
	}
}

/*
 * Old "Damage the wheels" thing:
 */
void oldSlam(Vehicle vehicle, int slamLevel) {
	switch (slamLevel) {
		case (2):
			ext.SetWheelsHealth(vehicle, 0.0f);
			break;
		case (1):
			ext.SetWheelsHealth(vehicle, 400.0f);
			break;
		default:
		case (0):
			ext.SetWheelsHealth(vehicle, 1000.0f);
			break;
	}
}

void init() {
	settings.ReadSettings();
	menu.ReadSettings();
	logger.Write("Settings read");

	// Depending on how crappy the XML is this shit might crash and burn.
	try {
		presets = settings.ReadPresets(presetCarsFile);
		saved = settings.ReadPresets(savedCarsFile);
	}
	catch (...) {
		showSubtitle("Unknown XML read error!");
		logger.Write("Unknown XML read error!");
	}
	logger.Write("Initialization finished");

}

void savePreset(bool asPreset, std::string presetName) {
	std::string name = VEHICLE::GET_DISPLAY_NAME_FROM_VEHICLE_MODEL(model);
	std::string plate;
	struct Preset::WheelInfo front = { g_frontCamber, g_frontTrackWidth, g_frontHeight};
	struct Preset::WheelInfo rear = { g_rearCamber, g_rearTrackWidth, g_rearHeight };

	if (asPreset) {
		GAMEPLAY::DISPLAY_ONSCREEN_KEYBOARD(true, "Preset Name", "", "", "", "", "", 127);
		while (GAMEPLAY::UPDATE_ONSCREEN_KEYBOARD() == 0) WAIT(0);
		if (!GAMEPLAY::GET_ONSCREEN_KEYBOARD_RESULT()) {
			showNotification("Cancelled save");
			return;
		}
		presetName = GAMEPLAY::GET_ONSCREEN_KEYBOARD_RESULT();
		
	}

	if (asPreset) {
		if (presetName.empty()) {
			plate = Preset::ReservedPlate();
		} else {
			plate = presetName;
		}
	} else {
		plate = VEHICLE::GET_VEHICLE_NUMBER_PLATE_TEXT(vehicle);
	}

	bool alreadyPresent = false;

	for (auto preset : asPreset ? presets : saved) {
		if (plate == preset.Plate() &&
			name == preset.Name()) {
			alreadyPresent = true;
		}
	}

	if (alreadyPresent) {
		settings.OverwritePreset(Preset(front, rear, name, plate, g_visualHeight), asPreset ? presetCarsFile : savedCarsFile);
		showNotification(asPreset ? "Updated preset" : "Updated car", &prevNotification);
	}
	else {
		try {
			settings.AppendPreset(Preset(front, rear, name, plate, g_visualHeight), asPreset ? presetCarsFile : savedCarsFile);
			showNotification(asPreset ? "Saved new preset" : "Saved new car", &prevNotification);
		}
		catch (std::runtime_error ex) {
			logger.Write(ex.what());
			logger.Write("Saving of " + plate + " to " + (asPreset ? presetCarsFile : savedCarsFile) + " failed!");
			showNotification("Saving to xml failed!");
		}
	}
	init();
}

/*
 * Scan current configs and remove. Since I cba to scan two lists again and the caller
 * should probably know which list it is from anyway that list is passed.
 */
void deletePreset(Preset preset, const std::vector<Preset> &fromWhich) {
	std::string fromFile;
	std::string message = "Couldn't find " + preset.Name() + " " + preset.Plate() + " :(";
	if (fromWhich == presets) {
		fromFile = presetCarsFile;
	}
	if (fromWhich == saved) {
		fromFile = savedCarsFile;
	}
	if (fromFile.empty()) {
		message = "File empty?";
		showNotification(message.c_str(), &prevNotification);
		return;
	}

	if (settings.DeletePreset(preset, fromFile)) {
		message = "Deleted preset " + preset.Name() + " " + preset.Plate();
		init();
	}
	showNotification(message.c_str(), &prevNotification);
}

void choosePresetMenu(std::string title, std::vector<Preset> whichPresets) {
	menu.Title(title);
	menu.Subtitle(title);

	std::string currentName = VEHICLE::GET_DISPLAY_NAME_FROM_VEHICLE_MODEL(model);
	std::string compatibleString = "Show only " + currentName;
	menu.BoolOption(compatibleString, showOnlyCompatible);
	for (auto preset : whichPresets) {
		if (showOnlyCompatible) {
			if (preset.Name() != currentName) {
				continue;
			}
		}
		std::string label = preset.Name() + " " + preset.Plate();
		std::vector<std::string> info;
		info.push_back("Press RIGHT to delete preset");
		info.push_back("Front Camber\t\t" + std::to_string(preset.Front.Camber));
		info.push_back("Front Track width\t" + std::to_string(preset.Front.TrackWidth));
		info.push_back("Front Height\t\t" + std::to_string(preset.Front.Height));
		info.push_back("Rear Camber\t\t" + std::to_string(preset.Rear.Camber));
		info.push_back("Rear Track width\t" + std::to_string(preset.Rear.TrackWidth));
		info.push_back("Rear Height\t\t" + std::to_string(preset.Rear.Height));
		std::string heightDisplay = preset.VisualHeight == -1337.0f ? "Missing Value" : std::to_string(preset.VisualHeight);
		info.push_back("Visual height\t\t" + heightDisplay);

		if (menu.OptionPlus(label, info, nullptr, std::bind(deletePreset, preset, whichPresets), nullptr, "Preset data")) {
			ultraSlam(vehicle,
			          preset.Front.Camber,
			          preset.Rear.Camber,
			          preset.Front.TrackWidth,
			          preset.Rear.TrackWidth,
			          preset.Front.Height,
			          preset.Rear.Height);
			if (preset.VisualHeight != -1337.0f)
				ext.SetVisualHeight(vehicle, preset.VisualHeight);

			getStats(vehicle);
			showNotification("Applied preset!", &prevNotification);
		}
	}
}


/*
 * I got the menu class from "Unknown Modder", he got it from SudoMod.
 */
void update_menu() {
	menu.CheckKeys();

	if (menu.CurrentMenu("mainmenu")) {
		menu.Title("VStancer");
		menu.Subtitle(DISPLAY_VERSION);
		if (menu.BoolOption("Enable mod", settings.enableMod, { "Enables or disables the entire mod." })) {
			settings.SaveSettings();
			if (settings.enableMod) {
				patchHeightReset();
			}
			else {
				unloadPatch();
			}
		}
		if (menu.BoolOption("Auto-apply", settings.autoApply, { "Automatically apply the car-specific preset if "
			"the licence plate and car model match." })) {
			settings.SaveSettings();
		}

		menu.MenuOption("Suspension menu", "suspensionmenu", { "Change camber, height, track width and overall height." });
		menu.MenuOption("Load a preset", "presetmenu", { "Load and manage a generic preset." } );
		menu.MenuOption("List car configs", "carsmenu", { "Show and manage car-specific presets." });
		if (menu.Option("Save as car", { "Save as car-specific preset. This loads the current setting when you get in "
			"this car with this licence plate." } )) {
			savePreset(false,"");
		}
		if (menu.Option("Save as preset", { "Save as generic preset." } )) {
			savePreset(true , "");
		}

		menu.MenuOption("Other stuff", "othermenu", { "\"Slam It\" is here at the moment." });
		menu.MenuOption("Tyres", "tyremenu", { "View and edit vehicle tyre settings. Unfinished, only physically affects the car. "
		"Visuals stay the same. Settings are not saved (yet)."});
	}

	if (menu.CurrentMenu("suspensionmenu")) {
		menu.Title("Suspension menu");
		menu.Subtitle("");

		menu.FloatOption( "Front Camber\t\t",	g_frontCamber, -2.0f, 2.0f, 0.01f);
		menu.FloatOption( "Front Track Width", g_frontTrackWidth, -2.0f, 2.0f, 0.01f);
		if (menu.FloatOption( "Front Height\t\t",   g_frontHeight, -2.0f, 2.0f, 0.01f) ) {
			ENTITY::APPLY_FORCE_TO_ENTITY_CENTER_OF_MASS(vehicle, 1, 0.0f, 0.1f, 0.0f, true, true, true, true);
		}
							 											   
		menu.FloatOption( "Rear Camber\t\t",   g_rearCamber, -2.0f, 2.0f, 0.01f); 
		menu.FloatOption( "Rear Track Width", g_rearTrackWidth, -2.0f, 2.0f, 0.01f);
		if (menu.FloatOption( "Rear Height\t\t",   g_rearHeight, -2.0f, 2.0f, 0.01f) ) {
			ENTITY::APPLY_FORCE_TO_ENTITY_CENTER_OF_MASS(vehicle, 1, 0.0f, 0.1f, 0.0f, true, true, true, true);
		}

		if (menu.FloatOption("Visual Lowering", g_visualHeight, -0.5f, 0.5f, 0.01f, { "This changes the same value tuning the suspension in mod shops does." })) {
			ext.SetVisualHeight(vehicle, g_visualHeight);
		}
	}

	if (menu.CurrentMenu("presetmenu")) {
		choosePresetMenu("Load preset", presets);
	}

	if (menu.CurrentMenu("carsmenu")) {
		choosePresetMenu("Car overview", saved);
	}

	if (menu.CurrentMenu("othermenu")) {
		menu.Title("Other options");
		menu.Subtitle("");
		if (menu.IntOption("Slam", slamLevel, 0, 2, 1, { "This damages the suspension/wheel so the car drops. Effect is removed upon vehicle repair."})) {
			oldSlam(vehicle, slamLevel);
			CONTROLS::_SET_CONTROL_NORMAL(0, ControlVehicleAccelerate, 0.3f);
		}
	}

	if (menu.CurrentMenu("tyremenu")) {
		menu.Title("");
		menu.Subtitle("");

		
		//for (int i = 0; i < ext.GetNumWheels(vehicle); i++) {
		//	std::string wheelNr = "Wheel " + std::to_string(i);
		//	menu.MenuOption(wheelNr, wheelNr);
		//}
		menu.MenuOption("Front", "wheelsizefrontmenu");
		menu.MenuOption("Rear", "wheelsizerearmenu");
	}

	if (menu.CurrentMenu("wheelsizefrontmenu")) {
		menu.Title("Front");
		menu.Subtitle("");

		int numWheels = ext.GetNumWheels(vehicle);
		if (numWheels < 4) {
			menu.Option("Vehicle has < 4 wheels");
			return;
		}

		auto wheels = ext.GetWheelPtrs(vehicle);
		for (int i = 0; i < 2; i++) {
			auto wheelAddr = wheels[i];
			int offTyreRadius = 0x110;
			int offRimRadius = 0x114;
			int offTyreWidth = 0x118;

			menu.FloatOption(std::to_string(i) + ". Tyre radius", *reinterpret_cast<float *>(wheelAddr + offTyreRadius), 0.0f, 10.0f, 0.01f);
			menu.FloatOption(std::to_string(i) + ". Rim radius", *reinterpret_cast<float *>(wheelAddr + offRimRadius), 0.0f, 10.0f, 0.01f);
			menu.FloatOption(std::to_string(i) + ". Tyre width", *reinterpret_cast<float *>(wheelAddr + offTyreWidth), 0.0f, 10.0f, 0.01f);

		}
	}

	if (menu.CurrentMenu("wheelsizerearmenu")) {
		menu.Title("Rear");
		menu.Subtitle("");

		int numWheels = ext.GetNumWheels(vehicle);
		if (numWheels < 4) {
			menu.Option("Vehicle has < 4 wheels");
			return;
		}

		auto wheels = ext.GetWheelPtrs(vehicle);
		for (int i = 2; i < numWheels; i++) {
			auto wheelAddr = wheels[i];
			int offTyreRadius = 0x110;
			int offRimRadius = 0x114;
			int offTyreWidth = 0x118;

			menu.FloatOption(std::to_string(i) + ". Tyre radius", *reinterpret_cast<float *>(wheelAddr + offTyreRadius), 0.0f, 10.0f, 0.01f);
			menu.FloatOption(std::to_string(i) + ". Rim radius", *reinterpret_cast<float *>(wheelAddr + offRimRadius), 0.0f, 10.0f, 0.01f);
			menu.FloatOption(std::to_string(i) + ". Tyre width", *reinterpret_cast<float *>(wheelAddr + offTyreWidth), 0.0f, 10.0f, 0.01f);

		}
	}


	menu.EndMenu();
}

void update_game() {
	player = PLAYER::PLAYER_ID();
	playerPed = PLAYER::PLAYER_PED_ID();

	if (!ENTITY::DOES_ENTITY_EXIST(playerPed) || !PLAYER::IS_PLAYER_CONTROL_ON(player) ||
		ENTITY::IS_ENTITY_DEAD(playerPed) || PLAYER::IS_PLAYER_BEING_ARRESTED(player, TRUE)) {
		return;
	}

	vehicle = PED::GET_VEHICLE_PED_IS_IN(playerPed, false);

	if (!ENTITY::DOES_ENTITY_EXIST(vehicle)) {
		prevVehicle = 0;
		autoApplied = false;
		return;
	}

	model = ENTITY::GET_ENTITY_MODEL(vehicle);
	if (!VEHICLE::IS_THIS_MODEL_A_CAR(model) && !VEHICLE::IS_THIS_MODEL_A_QUADBIKE(model)) {
		unloadPatch();
		return;
	}

	if (prevVehicle != vehicle) {
		if (!ext.GetAddress(vehicle)) {
			return;
		}
		getStats(vehicle);
		prevVehicle = vehicle;
		autoApplied = false;
		return;
	}

	update_menu();

	if (!settings.enableMod) {
		return;
	}

	if (settings.autoApply && !autoApplied) {
		for (auto preset : saved) {
			if (VEHICLE::GET_VEHICLE_NUMBER_PLATE_TEXT(vehicle) == preset.Plate() &&
				VEHICLE::GET_DISPLAY_NAME_FROM_VEHICLE_MODEL(model) == preset.Name()) {
				ultraSlam(vehicle, preset.Front.Camber, preset.Rear.Camber, preset.Front.TrackWidth, preset.Rear.TrackWidth, preset.Front.Height, preset.Rear.Height);
				if (preset.VisualHeight != -1337.0f)
					ext.SetVisualHeight(vehicle, preset.VisualHeight);
				autoApplied = true;
				getStats(vehicle);
				showNotification("Applied preset automatically!", &prevNotification);
			}
		}
	}

	ultraSlam(vehicle, g_frontCamber, g_rearCamber, g_frontTrackWidth, g_rearTrackWidth, g_frontHeight, g_rearHeight);
}

void main() {
	logger.Write("Script started");

	settingsGeneralFile = Paths::GetModuleFolder(Paths::GetOurModuleHandle()) + modDir + "\\settings_general.ini";
	settingsMenuFile = Paths::GetModuleFolder(Paths::GetOurModuleHandle()) + modDir + "\\settings_menu.ini";
	savedCarsFile = Paths::GetModuleFolder(Paths::GetOurModuleHandle()) + modDir + "\\car_preset.xml";
	presetCarsFile = Paths::GetModuleFolder(Paths::GetOurModuleHandle()) + modDir + "\\car_saved.xml";
	settings.SetFiles(settingsGeneralFile);
	menu.SetFiles(settingsMenuFile);
	menu.RegisterOnMain(std::bind(init));

	logger.Write("Loading " + settingsGeneralFile);
	logger.Write("Loading " + settingsMenuFile);
	logger.Write("Loading " + savedCarsFile);
	logger.Write("Loading " + presetCarsFile);

	init();

	if (settings.enableMod) {
		patchHeightReset();
	}

	while (true) {
		update_game();
		WAIT(0);
	}
}

void ScriptMain() {
	srand(GetTickCount());
	main();
}
