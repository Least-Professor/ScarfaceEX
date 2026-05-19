#include <Windows.h>
#include <Psapi.h>
#include <Xinput.h>
#define DIRECTINPUT_VERSION 0x0800
#include <dinput.h>
#include <fstream>
#include <string>
#include <sstream>

#pragma comment(lib, "Psapi.lib")
#pragma comment(lib, "Xinput.lib")
#pragma comment(lib, "dinput8.lib")
#pragma comment(lib, "dxguid.lib")

typedef void* CharacterObject;
typedef int (__thiscall *PlayAnimationFunc)(CharacterObject*, unsigned int*, int, int);
typedef const char* (__cdecl *CVM_Get_Main_Character_Package_Function)();
typedef void (__cdecl *Run_Script_Function)(const char*, int, int, int, int);
typedef int (__cdecl *Global_Sound_Get_Current_Function)();

#define ADDR_VehicleState 0x007BC51C
#define CONFIG_FILE "ScarfaceEX_Configuration.ini"
#define README_FILE "ScarfaceEX_Instructions.txt"

// Character Switching Tracker
struct Main_Character_Switching
{
	bool animationPlayed, phoneCallTriggered;
	std::string lastPackage;
	int currentIteration, lastIteration;
	
	void Reset()
	{
		animationPlayed = false;
		phoneCallTriggered = false;
		lastPackage = "";
		currentIteration = 0;
		lastIteration = 0;
	}
}
MCS_Struct;

// Health Recovery Tracker
struct Health_Recovery_Structure
{
	int lastHealth;
	bool animationPlayed;
	float lastX, lastY, lastZ;
	
	void Reset()
	{
		lastHealth = -1;
		animationPlayed = false;
		lastX = 0.0;
		lastY = 0.0;
		lastZ = 0.0;
	}
}
HRS_Struct;

// 1st Person Camera Mode
struct Vector 
{
    float X, Y, Z;
    
	Vector() : X(0), Y(0), Z(0) 
	{
	}
    
	Vector(float x, float y, float z) : X(x), Y(y), Z(z) 
	{
	}
    
	Vector operator+(const Vector& other) const 
	{
        return Vector(X + other.X, Y + other.Y, Z + other.Z);
    }
    
	Vector operator*(float scalar) const 
	{
        return Vector(X * scalar, Y * scalar, Z * scalar);
    }
};

struct Matrix 
{
    float M[4][4];
    
    Vector GetForward() const 
	{ 
		return Vector(M[2][0], M[2][1], M[2][2]); 
	}
    
	Vector GetRight() const 
	{ 
		return Vector(M[0][0], M[0][1], M[0][2]); 
	}
    
	Vector GetUp() const 
	{ 
		return Vector(M[1][0], M[1][1], M[1][2]); 
	}
};

// 1st Person Camera Configuration
struct FPSCameraConfig 
{
    bool enabled, footOnly;
	int bonePosition;
    float heightAdjust, sideAdjust, distanceAdjust; // Y-Offset \ X-Offset \ Z-Offset respectively
} 
g_FPSConfig;

// Dodges & Evades Configuration
struct Config 
{
    // Keyboard & Mouse Bindings
	int frontKey, leftKey, rightKey, backKey, leftPeekShootKey, rightPeekShootKey; 								 			   
    
	// Microsoft Xinput Controller Bindings
	WORD frontButton, leftButton, rightButton, backButton, leftPeekShootButton, rightPeekShootButton;

	// Direct-Input Controller Bindings
    int diFrontButton, diLeftButton, diRightButton, diBackButton, diLeftPeekShootButton, diRightPeekShootButton; 			   
	
	// Animation Names
    std::string frontAnimation, leftAnimation, rightAnimation, backAnimation, leftPeekShootAnimation, rightPeekShootAnimation, waterVehicleDriver, landVehicleDriver, landVehicleDriverReverse, waterVehicleIdles, landVehicleIdles, landVehicleDamageDriver, landVehicleDamageDriverReverse, waterVehicleDamageDriver, landVehicleDamagePassenger, waterVehicleDamagePassenger, Damage_50_Calibers, HealthRecovery, SwitchingCharacters;
    
	// Press Mode
	int pressMode;

	// Multi-Press Timing Window \ Cooldown Between Dodges [Milliseconds]
    unsigned long pressWindow, cooldown;																					   
} 
g_Config;

// Vehicle Animation State Tracking
struct VehicleAnimState
{
    bool idle, active, wasInVehicle, wasInputActive;
	unsigned long entryTime, inputReleasedAt;
	
    void Reset()
    {
        idle = false;
        active = false;
		wasInVehicle = false;
		wasInputActive = false;
        entryTime = 0;
        inputReleasedAt = 0;
    }
}
g_VehicleAnim;

// NPC Vehicle Animation Tracking
unsigned long g_LastNPCAnimCheck = 0;
static const unsigned long NPC_ANIM_INTERVAL = 732UL;
static const int MAX_NPCS = 64; 

// Pattern Scanning
uintptr_t FindPattern(const char* pattern, const char* mask) 
{
    MODULEINFO modInfo;
    GetModuleInformation(GetCurrentProcess(), GetModuleHandle(NULL), &modInfo, sizeof(MODULEINFO));
    uintptr_t base = (uintptr_t)modInfo.lpBaseOfDll;
    size_t size = modInfo.SizeOfImage, patternLen = strlen(mask);
    
    for (size_t i = 0; i < size - patternLen; i++) 
	{
        bool found = true;
        
		for (size_t j = 0; j < patternLen; j++) 
		{
            if (mask[j] != '?' && pattern[j] != *(char*)(base + i + j)) 
			{
                found = false;
                break;
            }
        }
        
		if (found) 
			return base + i;
    }
    
	return 0;
}

void ReadCall(uintptr_t callAddr, uintptr_t* outAddr) 
{
    int offset = *(int*)(callAddr + 1);
    *outAddr = callAddr + 5 + offset;
}

// Hashing Function
unsigned int Hash(char* input) 
{
    if (!input) 
		return 0;
    
	char curChar = *input;
    
	if (!curChar) 
		return 0;
    
	int v4 = 0;
    
	do 
	{
        int v5 = (65599 * v4) & 0x7FFFFFFF, temp = curChar;
        if (curChar < 'a') 
			temp = curChar + ' ';
        v4 = temp ^ v5;
        curChar = *(++input);
    } 
	while (*input);
    
	return v4 | 0x80000000;
}

// Characters' Weapon State Detector
int Get_Weapon_State(CharacterObject* npc)
{
	__try
	{
		int weaponState = *(int*)((uintptr_t)npc + 0x2AC + 0xC);
		return weaponState;
	}
    
	__except(EXCEPTION_EXECUTE_HANDLER) 
	{ 
		return 0; 
	}
} 

// Character's Driving Direction Detector
int Land_Vehicle_Reverse_Driving(CharacterObject* characters)
{
	__try
	{
		int reverseDirection = *(int*)((uintptr_t)characters + 0x2E8 + 0x8);
		return (reverseDirection == 3) ? 1 : 0;
	}
    
	__except(EXCEPTION_EXECUTE_HANDLER) 
	{ 
		return 0; 
	}
}

// Characters' 50 Calibers Weapon State Detector
int Get_50_Calibers(CharacterObject* npc)
{
	__try
	{
		int calibers50 = *(int*)((uintptr_t)npc + 0x2E8 + 0x8);
		return (calibers50 == 6) ? 1 : 0;
	}
    
	__except(EXCEPTION_EXECUTE_HANDLER) 
	{ 
		return 0; 
	}
}

// Vehicle Damage Animation Tracking
struct DamageAnimState
{
    int lastHealth;  
    bool animPlayed; 

    void Reset()
    {
        lastHealth = -1; 
        animPlayed = false;
    }
};

static const int MAX_NPC_DAMAGE = 64;
DamageAnimState g_NPCDamageStates[MAX_NPC_DAMAGE];

// 50 Caliber Damage Animation Tracking
static const int MAX_NPC_50CAL = 64;
DamageAnimState g_NPC50CalStates[MAX_NPC_50CAL];

struct NPCAnimState
{
    float lastX, lastZ;
    bool idlePlayed, activePlayed;

    void Reset()
    {
		lastX = 0.0f;
        lastZ = 0.0f;
        idlePlayed = false;
        activePlayed = false;
    }
}
g_NPCStates[MAX_NPCS];

// Press Tracking Structure
struct PressTracker 
{
    unsigned long lastPressTime;
    int pressCount;
    
    void Reset() 
	{
        lastPressTime = 0;
        pressCount = 0;
    }
    
    bool CheckPress(unsigned long now, unsigned long window) 
	{
        if (now - lastPressTime > window)
            pressCount = 1;
        
		else
            pressCount++;
        
		lastPressTime = now;
        
		return true;
    }
    
    bool ShouldTrigger(int requiredPresses)
	{
        return pressCount >= requiredPresses;
	}
};

// Separate Bindings' Trackers
struct DirectionalTrackers 
{
    PressTracker front, left, right, back, leftPeekShoot, rightPeekShoot;
} 
g_Trackers;

// Global Variables
unsigned long g_LastDodgeTime = 0;
uintptr_t g_PlayerPointerAddress = 0, g_CVManagerAddress = 0;
PlayAnimationFunc g_PlayAnimationFunc = NULL;
CVM_Get_Main_Character_Package_Function CVM_Get_Main_Character_Package_Pointer = (CVM_Get_Main_Character_Package_Function)0x004f4d60;
Run_Script_Function Run_Script_Pointer = NULL;
uintptr_t Run_Script_Address = 0;
Global_Sound_Get_Current_Function Global_Sound_Get_Current_Pointer = (Global_Sound_Get_Current_Function)0x0049a420;

// Neonix Remastered Project 1.1 Tracker
static int NRMA_Cached = -1;

// Keyboard \ Mouse \ Microsoft Xinput Controller \ Direct-Input Controller States Tracker; Direct-Input Flag
bool g_KeyboardState[6] = { false, false, false, false, false, false }, g_ControllerState[6] = { false, false, false, false, false, false }, g_DirectInputState[6] = { false, false, false, false, false, false }, g_DirectInputEnabled = false;

// DirectInput Globals
LPDIRECTINPUT8 g_pDI = NULL;
LPDIRECTINPUTDEVICE8 g_pJoystick = NULL;

// Thread Control
volatile bool g_Running = true;
HANDLE g_InputThreadHandle = NULL;

// First Person Camera Globals
typedef void (__thiscall *SetPositionFunc)(void* camera, Vector* pos);
typedef Vector* (__thiscall *GetBonePositionFunc)(CharacterObject* character, int boneId);

SetPositionFunc g_OriginalSetPosition = NULL;
GetBonePositionFunc g_GetBonePosition = NULL;

// Global_Sound_Tracker
int GlobalSoundGetCurrentMs()
{
    if (!Global_Sound_Get_Current_Pointer)
        return -1;

    __try
    {
        return Global_Sound_Get_Current_Pointer();
    }

    __except(EXCEPTION_EXECUTE_HANDLER)
    {
        return -1;
    }
}

// Run Script Pointer Locator
bool Initialise_Run_Script()
{
    const char* pattern = "\xE8\x00\x00\x00\x00\x8B\x4C\x24\x28\x51\x6A\x4C\xE8\x00\x00\x00\x00\x83\xC4\x1C\x85\xC0\x5F\x5E";
    const char* mask = "x????xxxxxxxx????xxxxxxx";
	uintptr_t addr = FindPattern(pattern, mask);

    if (!addr)
        return false;

    Run_Script_Address = addr;
    uintptr_t Function_Address = 0;
    ReadCall(Run_Script_Address, &Function_Address);
    Run_Script_Pointer = (Run_Script_Function)Function_Address;

    return (Run_Script_Pointer != NULL);
} 

// Run Script Wrapper
void RunScript(const char* script)
{
    if (!script || !Run_Script_Pointer)
        return;

    __try
    {
        Run_Script_Pointer(script, 0, 0, 0, -1);
    }

    __except(EXCEPTION_EXECUTE_HANDLER)
    {
    }
}

// CVM_Get_Main_Character_Package Wrapper
const char* CVM_Get_Main_Character_Package_Wrapper()
{
    if (!CVM_Get_Main_Character_Package_Pointer)
        return NULL;

    __try
    {
        return CVM_Get_Main_Character_Package_Pointer();
    }

    __except(EXCEPTION_EXECUTE_HANDLER)
    {
        return NULL;
    }
}

// String Trimmer
std::string Trim(const std::string& str) 
{
    size_t first = str.find_first_not_of(" \t\r\n");
    
	if (first == std::string::npos) 
		return "";
    
	size_t last = str.find_last_not_of(" \t\r\n");
    
	return str.substr(first, last - first + 1);
}

// String -> Integer Conversion
int ParseInt(const std::string& str, int defaultValue) 
{
    try 
	{
        return std::stoi(str);
    } 
	
	catch (...) 
	{
        return defaultValue;
    }
}

// String -> Float Conversion
float ParseFloat(const std::string& str, float defaultValue) 
{
    try 
	{
        return std::stof(str);
    } 
	
	catch (...) 
	{
        return defaultValue;
    }
}

// Microsoft Xinput Bindings Parsing
WORD ParseButton(const std::string& name) 
{
    if (name == "A") 
		return XINPUT_GAMEPAD_A;
    
	if (name == "B") 
		return XINPUT_GAMEPAD_B;
    
	if (name == "X") 
		return XINPUT_GAMEPAD_X;
    
	if (name == "Y") 
		return XINPUT_GAMEPAD_Y;
    
	if (name == "LB") 
		return XINPUT_GAMEPAD_LEFT_SHOULDER;
    
	if (name == "RB") 
		return XINPUT_GAMEPAD_RIGHT_SHOULDER;
    
	if (name == "L3") 
		return XINPUT_GAMEPAD_LEFT_THUMB;
    
	if (name == "R3") 
		return XINPUT_GAMEPAD_RIGHT_THUMB;
    
	if (name == "UP") 
		return XINPUT_GAMEPAD_DPAD_UP;
    
	if (name == "DOWN") 
		return XINPUT_GAMEPAD_DPAD_DOWN;
    
	if (name == "LEFT") 
		return XINPUT_GAMEPAD_DPAD_LEFT;
    
	if (name == "RIGHT") 
		return XINPUT_GAMEPAD_DPAD_RIGHT;
    
	if (name == "START") 
		return XINPUT_GAMEPAD_START;
    
	if (name == "BACK") 
		return XINPUT_GAMEPAD_BACK;
    
	return XINPUT_GAMEPAD_RIGHT_THUMB; // Default
}

// Parse key name to Virtual Key Code
int ParseKey(const std::string& name) {
    
	// Single Character Keys : A - Z \ 0-9
    if (name.length() == 1) 
	{
        char c = toupper(name[0]);
        
		if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))
			return c;
    }
	
	// Shifted Symbol Keys
	if (name == "!") 
		return '1';                          
    
	if (name == "@") 
		return '2';
    
	if (name == "#") 
		return '3';
    
	if (name == "$" || name == "₹") 
		return '4';
    
	if (name == "%") 
		return '5';
    
	if (name == "^") 
		return '6';
    
	if (name == "&") 
		return '7';
    
	if (name == "*") 
		return '8';
    
	if (name == "(") 
		return '9';
    
	if (name == ")") 
		return '0';
	
	// Punctuation & Symbol Keys
    if (name == "[" || name == "{") 
		return VK_OEM_4;
    
	if (name == "]" || name == "}") 
		return VK_OEM_6;
    
	if (name == ";" || name == ":") 
		return VK_OEM_1;
    
	if (name == "'" || name == "\"") 
		return VK_OEM_7;
    
	if (name == "," || name == "<") 
		return VK_OEM_COMMA;
    
	if (name == "." || name == ">") 
		return VK_OEM_PERIOD;
    
	if (name == "/" || name == "?") 
		return VK_OEM_2;
    
	if (name == "`" || name == "~") 
		return VK_OEM_3;
    
	if (name == "-" || name == "_") 
		return VK_OEM_MINUS;
    
	if (name == "=" || name == "+") 
		return VK_OEM_PLUS;
    
	if (name == "\\" || name == "|") 
		return VK_OEM_5;
    
    // Function keys
    if (name == "F1") 
		return VK_F1;
    
	if (name == "F2") 
		return VK_F2;
    
	if (name == "F3") 
		return VK_F3;
    
	if (name == "F4") 
		return VK_F4;
    
	if (name == "F5") 
		return VK_F5;
    
	if (name == "F6") 
		return VK_F6;
    
	if (name == "F7") 
		return VK_F7;
    
	if (name == "F8") 
		return VK_F8;
    
	if (name == "F9") 
		return VK_F9;
    
	if (name == "F10") 
		return VK_F10;
    
	if (name == "F11") 
		return VK_F11;
    
	if (name == "F12") 
		return VK_F12;
    
    // Arrow keys
    if (name == "UP") 
		return VK_UP;
    
	if (name == "DOWN") 
		return VK_DOWN;
    
	if (name == "LEFT") 
		return VK_LEFT;
    
	if (name == "RIGHT") 
		return VK_RIGHT;
    
    // Modifier keys
    if (name == "SHIFT") 
		return VK_SHIFT;
    
	if (name == "LSHIFT") 
		return VK_LSHIFT;
    
	if (name == "RSHIFT") 
		return VK_RSHIFT;
    
	if (name == "CTRL") 
		return VK_CONTROL;
    
	if (name == "LCTRL") 
		return VK_LCONTROL;
    
	if (name == "RCTRL") 
		return VK_RCONTROL;
    
	if (name == "ALT") 
		return VK_MENU;
    
	if (name == "LALT") 
		return VK_LMENU;
    
	if (name == "RALT") 
		return VK_RMENU;
    
    // Special keys
    if (name == "SPACE") 
		return VK_SPACE;
    
	if (name == "ENTER") 
		return VK_RETURN;
    
	if (name == "TAB") 
		return VK_TAB;
    
	if (name == "BACKSPACE") 
		return VK_BACK;
    
	if (name == "DELETE") 
		return VK_DELETE;
    
	if (name == "INSERT") 
		return VK_INSERT;
    
	if (name == "HOME") 
		return VK_HOME;
    
	if (name == "END") 
		return VK_END;
    
	if (name == "PAGEUP") 
		return VK_PRIOR;
    
	if (name == "PAGEDOWN") 
		return VK_NEXT;
    
	if (name == "ESC") 
		return VK_ESCAPE;
    
	if (name == "CAPSLOCK") 
		return VK_CAPITAL;
    
    // Numpad keys
    if (name == "NUMPAD0") 
		return VK_NUMPAD0;
    
	if (name == "NUMPAD1") 
		return VK_NUMPAD1;
    
	if (name == "NUMPAD2") 
		return VK_NUMPAD2;
    
	if (name == "NUMPAD3") 
		return VK_NUMPAD3;
    
	if (name == "NUMPAD4") 
		return VK_NUMPAD4;
    
	if (name == "NUMPAD5") 
		return VK_NUMPAD5;
    
	if (name == "NUMPAD6") 
		return VK_NUMPAD6;
    
	if (name == "NUMPAD7") 
		return VK_NUMPAD7;
    
	if (name == "NUMPAD8") 
		return VK_NUMPAD8;
    
	if (name == "NUMPAD9") 
		return VK_NUMPAD9;
    
    // Mouse buttons
    if (name == "MOUSE1") 
		return VK_LBUTTON;
    
	if (name == "MOUSE2") 
		return VK_RBUTTON;
    
	if (name == "MOUSE3") 
		return VK_MBUTTON;
    
	if (name == "MOUSE4") 
		return VK_XBUTTON1;
    
	if (name == "MOUSE5") 
		return VK_XBUTTON2;
    
    return 'V'; // Default
}

// Configuration Initialisation
void LoadConfig() 
{
    // Dodges & Evades Defaults
    g_Config.frontKey = 'V';
    g_Config.leftKey = 'A';
    g_Config.rightKey = 'D';
	g_Config.backKey = 'C';
	g_Config.leftPeekShootKey = 'Q';
	g_Config.rightPeekShootKey = 'E';
    
    g_Config.frontButton = XINPUT_GAMEPAD_A;
    g_Config.leftButton = XINPUT_GAMEPAD_B; 
    g_Config.rightButton = XINPUT_GAMEPAD_X;
	g_Config.backButton = XINPUT_GAMEPAD_Y;
	g_Config.leftPeekShootButton = XINPUT_GAMEPAD_LEFT_SHOULDER;
	g_Config.rightPeekShootButton = XINPUT_GAMEPAD_RIGHT_SHOULDER;
    
    g_Config.diFrontButton = 0;
    g_Config.diLeftButton = 1;
    g_Config.diRightButton = 2;
    g_Config.diBackButton = 3;
    g_Config.diLeftPeekShootButton = 4;
    g_Config.diRightPeekShootButton = 5;
    
    g_Config.frontAnimation = "GEN_Action_Evade_Dive_N";
    g_Config.leftAnimation = "Left_Dive_Dodge_Roll_West";
    g_Config.rightAnimation = "Right_Dive_Dodge_Roll_East";
	g_Config.backAnimation = "Proximity_Weapon_Attack";
	g_Config.leftPeekShootAnimation = "Left_Peek_Shoot";
	g_Config.rightPeekShootAnimation = "Right_Peek_Shoot";
	g_Config.HealthRecovery = "Health_Recovery";
	g_Config.SwitchingCharacters = "Switching_Characters";
	
	// Vehicles
	g_Config.waterVehicleDriver = "Boat_Steer_Generic";
	g_Config.waterVehicleIdles = "Water_Vehicles_Idles";            
	g_Config.waterVehicleDamageDriver = "Driver_Water_Vehicle_Damage";
	g_Config.waterVehicleDamagePassenger = "Passenger_Water_Vehicle_Damage";
	g_Config.landVehicleDriver = "Steer_Forward";
	g_Config.landVehicleDriverReverse = "Steer_Reverse";
	g_Config.landVehicleIdles = "Land_Vehicles_Idles";   
	g_Config.landVehicleDamageDriver = "Driver_Land_Vehicle_Damage";
	g_Config.landVehicleDamageDriverReverse = "Driver_Land_Vehicle_Damage_Reverse";
	g_Config.landVehicleDamagePassenger = "Passenger_Land_Vehicle_Damage";
	g_Config.Damage_50_Calibers = "Damage_50_Calibers";
    
    g_Config.pressMode = 2;
    g_Config.pressWindow = 210;
    g_Config.cooldown = 753;
    
    // 1st Person Camera Defaults
    g_FPSConfig.enabled = false;
	g_FPSConfig.footOnly = false;
	g_FPSConfig.bonePosition = 13;
    g_FPSConfig.heightAdjust = 0.0f;
    g_FPSConfig.sideAdjust = 0.0f;
    g_FPSConfig.distanceAdjust = 0.0f;
	
	// Read-Me File Creation
	std::ifstream file_(README_FILE);
	
	if (!file_.is_open())
	{
        std::ofstream outFile(README_FILE);
        outFile << "Scarface : The World Is Yours :\n\n\tScarfaceEX -\n\n\t\tKeyboard & Mouse Controls :\n\n\t\t\tAvailable Inputs -\n\n\t\t\t\tA - Z\n\t\t\t\t0 - 9\n\t\t\t\tF1 - F12\n\t\t\t\tSHIFT\n\t\t\t\tCTRL\n\t\t\t\tALT\n\t\t\t\tSPACE\n\t\t\t\tENTER\n\t\t\t\tTAB\n\t\t\t\tUP\n\t\t\t\tDOWN\n\t\t\t\tLEFT\n\t\t\t\tRIGHT\n\t\t\t\tPAGEUP\n\t\t\t\tPAGEDOWN\n\t\t\t\tHOME\n\t\t\t\tEND\n\t\t\t\tNUMPAD0 - NUMPAD9\n\t\t\t\tMOUSE1 - MOUSE5\n\t\t\t\tESC\n\t\t\t\tINSERT\n\t\t\t\tDELETE\n\t\t\t\t[\n\t\t\t\t]\n\t\t\t\t;\n\t\t\t\t'\n\t\t\t\t,\n\t\t\t\t.\n\t\t\t\t/\n\t\t\t\t`\n\t\t\t\t-\n\t\t\t\t=\n\t\t\t\t\\\n\t\t\t\t{\n\t\t\t\t}\n\t\t\t\t:\n\t\t\t\t<\n\t\t\t\t>\n\t\t\t\t?\n\t\t\t\t~\n\t\t\t\t_\n\t\t\t\t+\n\t\t\t\t|\n\t\t\t\t\"\n\t\t\t\t\!\n\t\t\t\t\@\n\t\t\t\t\#\n\t\t\t\t\₹ OR $\n\t\t\t\t\%\n\t\t\t\t\^\n\t\t\t\t\&\n\t\t\t\t\*\n\t\t\t\t\(\n\t\t\t\t\)\n\n";
		outFile << "\t\tXbox Controller :\n\n\t\t\tAvailable Inputs -\n\n\t\t\t\tA\n\t\t\t\tB\n\t\t\t\tX\n\t\t\t\tY\n\t\t\t\tLB\n\t\t\t\tRB\n\t\t\t\tL3\n\t\t\t\tR3\n\t\t\t\tUP\n\t\t\t\tDOWN\n\t\t\t\tLEFT\n\t\t\t\tRIGHT\n\t\t\t\tSTART\n\t\t\t\tBACK\n\n";
		outFile << "\t\tDirect Input Controller :\n\n\t\t\tAvailable Inputs -\n\n\t\t\t\t0 - 31\n\t\t\t\tNote :\n\n\t\t\t\t\tButton mapping varies by controller model\n\t\t\t\t\tOnly Buttons will work\n\n";
		outFile << "\t\tAnimations :\n\n\t\t\tAvailable Animations -\n\n\t\t\t\tCheck 'packages/zo4/Animation.p3d'\n\n";
		outFile << "\t\tBehavior :\n\n\t\t\tPress_Mode - Number of Input Presses required\n\t\t\tPress_Window - Specified Time, in milliseconds, within which the player has to complete their inputs\n\t\t\tCooldown - Specified time, in milliseconds, for which all the inputs will stay locked until they clear up after each successful attempt\n\n";
		outFile << "\t\tFirst Person Camera :\n\n\t\t\tFoot_Only - Specifies whether this mode should either stay on everywhere, or only on Foot\n\t\t\tBone_Position - Character's bone, to which this camera mode should attach itself to\n\t\t\tAvailable Bones -\n\n\t\t\t\t0 --- Motion_Root\n\t\t\t\t1 --- Balance_Root\n\t\t\t\t2 --- Character_Root\n\t\t\t\t3 --- Pelvis\n\t\t\t\t4 --- Hip_L\n\t\t\t\t5 --- Knee_L\n\t\t\t\t6 --- Ankle_L\n\t\t\t\t7 --- Hip_R\n\t\t\t\t8 --- Knee_R\n\t\t\t\t9 --- Ankle_R\n\t\t\t\t10 --- Spine_1\n\t\t\t\t11 --- Spine_2\n\t\t\t\t12 --- Neck\n\t\t\t\t13 --- Head\n\t\t\t\t14 --- Clavicle_L\n\t\t\t\t15 --- Shoulder_L\n\t\t\t\t16 --- Elbow_L\n\t\t\t\t17 --- Forarm_L\n\t\t\t\t18 --- Wrist_L\n\t\t\t\t19 --- Clavicle_R\n\t\t\t\t20 --- Shoulder_R\n\t\t\t\t21 --- Elbow_R\n\t\t\t\t22 --- Forarm_R\n\t\t\t\t23 --- Wrist_R\n\t\t\t\t24 --- right_hand_attach\n\t\t\t\t25 --- Rage_Joint\n\n\t\t\tHeight_Adjust - Vertical Offset\n\t\t\tSide_Adjust - Horizontal Offset\n\t\t\tDistance_Adjust - Forward / Back Offset\n";
		outFile.close();
	}
    
    // Configuration File Creation
	std::ifstream file(CONFIG_FILE);
    
	if (!file.is_open()) 
	{
        std::ofstream outFile(CONFIG_FILE);
        outFile << "[Keyboard_Mouse_Controls]\n\nFront_Key=V\nLeft_Key=A\nRight_Key=D\nBack_Key=C\nLeft_Peek_Shoot_Key=Q\nRight_Peek_Shoot_Key=E\n\n";
		outFile << "[Xbox_Controller]\n\nFront_Button=A\nLeft_Button=B\nRight_Button=X\nBack_Button=Y\nLeft_Peek_Shoot_Button=LB\nRight_Peek_Shoot_Button=RB\n\n";
		outFile << "[Direct_Input_Controller]\n\nFront_Button=0\nLeft_Button=1\nRight_Button=2\nBack_Button=3\nLeft_Peek_Shoot_Button=4\nRight_Peek_Shoot_Button=5\n\n";
		outFile << "[Animations]\n\nSwitching_Characters=Switching_Characters\nFront_Animation=GEN_Action_Evade_Dive_N\nLeft_Animation=Left_Dive_Dodge_Roll_West\nRight_Animation=Right_Dive_Dodge_Roll_East\nBack_Animation=Proximity_Weapon_Attack\nLeft_Peek_Shoot_Animation=Left_Peek_Shoot\nRight_Peek_Shoot_Animation=Right_Peek_Shoot\nHealth_Recovery=Health_Recovery\n\n";
		outFile << "[Behavior]\n\nPress_Mode=2\nPress_Window=210\nCooldown=753\n\n";
		outFile << "[Vehicles]\n\nWater_Vehicle_Driver=Boat_Steer_Generic\nLand_Vehicle_Driver=Steer_Forward\nLand_Vehicle_Driver_Reverse=Steer_Reverse\nWater_Vehicle_Idles=Water_Vehicles_Idles\nLand_Vehicle_Idles=Land_Vehicles_Idles\nDriver_Land_Vehicle_Damage=Driver_Land_Vehicle_Damage\nDriver_Land_Vehicle_Damage_Reverse=Driver_Land_Vehicle_Damage_Reverse\nDriver_Water_Vehicle_Damage=Driver_Water_Vehicle_Damage\nPassenger_Land_Vehicle_Damage=Passenger_Land_Vehicle_Damage\nPassenger_Water_Vehicle_Damage=Passenger_Water_Vehicle_Damage\nDamage_50_Calibers=Damage_50_Calibers\n\n";
		outFile << "[First_Person_Camera]\n\nEnabled=0\nFoot_Only=0\nBone_Position=13\nHeight_Adjust=0.0\nSide_Adjust=0.0\nDistance_Adjust=0.0\n";
        outFile.close();
        return;
    }
    
    std::string line, section;
    
	while (std::getline(file, line)) 
	{
        line = Trim(line);
        
		if (line.empty() || line[0] == ';') 
			continue;        
        
		if (line[0] == '[') 
		{
            section = line.substr(1, line.find(']') - 1);
            continue;
        }
        
        size_t pos = line.find('=');
        
		if (pos == std::string::npos) 
			continue;
        
        std::string key = Trim(line.substr(0, pos)), value = Trim(line.substr(pos + 1));
		
		// Inputs Parsing
        if (section == "Keyboard_Mouse_Controls") 
		{
            if (key == "Front_Key") 
				g_Config.frontKey = ParseKey(value);
            
			if (key == "Left_Key") 
				g_Config.leftKey = ParseKey(value);
            
			if (key == "Right_Key") 
				g_Config.rightKey = ParseKey(value);
            
			if (key == "Back_Key") 
				g_Config.backKey = ParseKey(value);
            
			if (key == "Left_Peek_Shoot_Key") 
				g_Config.leftPeekShootKey = ParseKey(value);
            
			if (key == "Right_Peek_Shoot_Key") 
				g_Config.rightPeekShootKey = ParseKey(value);
        }
        
		if (section == "Xbox_Controller") 
		{
            if (key == "Front_Button") 
				g_Config.frontButton = ParseButton(value);
            
			if (key == "Left_Button") 
				g_Config.leftButton = ParseButton(value);
            
			if (key == "Right_Button") 
				g_Config.rightButton = ParseButton(value);
			
			if (key == "Back_Button") 
				g_Config.backButton = ParseButton(value);
            
			if (key == "Left_Peek_Shoot_Button") 
				g_Config.leftPeekShootButton = ParseButton(value);
			
			if (key == "Right_Peek_Shoot_Button") 
				g_Config.rightPeekShootButton = ParseButton(value);
        }
        
		if (section == "Direct_Input_Controller") 
		{
            if (key == "Front_Button") 
				g_Config.diFrontButton = ParseInt(value, 0);
            
			if (key == "Left_Button") 
				g_Config.diLeftButton = ParseInt(value, 1);
            
			if (key == "Right_Button") 
				g_Config.diRightButton = ParseInt(value, 2);
            
			if (key == "Back_Button") 
				g_Config.diBackButton = ParseInt(value, 3);
            
			if (key == "Left_Peek_Shoot_Button") 
				g_Config.diLeftPeekShootButton = ParseInt(value, 4);
            
			if (key == "Right_Peek_Shoot_Button") 
				g_Config.diRightPeekShootButton = ParseInt(value, 5);
        }
        
		if (section == "Animations") 
		{
            if (key == "Front_Animation" && !value.empty()) 
				g_Config.frontAnimation = value;
            
			if (key == "Left_Animation" && !value.empty()) 
				g_Config.leftAnimation = value;
            
			if (key == "Right_Animation" && !value.empty()) 
				g_Config.rightAnimation = value;
            
			if (key == "Back_Animation" && !value.empty()) 
				g_Config.backAnimation = value;
            
			if (key == "Left_Peek_Shoot_Animation" && !value.empty()) 
				g_Config.leftPeekShootAnimation = value;
            
			if (key == "Right_Peek_Shoot_Animation" && !value.empty()) 
				g_Config.rightPeekShootAnimation = value;
            
			if (key == "Health_Recovery" && !value.empty()) 
				g_Config.HealthRecovery = value; 
            
			if (key == "Switching_Characters" && !value.empty()) 
				g_Config.SwitchingCharacters = value; 
        }
		
		if (section == "Behavior") 
		{
            if (key == "Press_Mode") 
				g_Config.pressMode = ParseInt(value, 1);
            
			if (key == "Press_Window") 
				g_Config.pressWindow = ParseInt(value, 300);
            
			if (key == "Cooldown") 
				g_Config.cooldown = ParseInt(value, 500);
        }
        
		if (section == "First_Person_Camera") 
		{
            if (key == "Enabled") 
				g_FPSConfig.enabled = (ParseInt(value, 0) != 0);
            
			if (key == "Foot_Only") 
				g_FPSConfig.footOnly = (ParseInt(value, 0) != 0);
            
			if (key == "Bone_Position") 
				g_FPSConfig.bonePosition = ParseInt(value, 13);
            
			if (key == "Height_Adjust") 
				g_FPSConfig.heightAdjust = ParseFloat(value, 0.0f);
            
			if (key == "Side_Adjust") 
				g_FPSConfig.sideAdjust = ParseFloat(value, 0.0f);
            
			if (key == "Distance_Adjust") 
				g_FPSConfig.distanceAdjust = ParseFloat(value, 0.0f);
        }
		
		if (section == "Vehicles")
		{
			if (key == "Water_Vehicle_Driver" && !value.empty()) 
				g_Config.waterVehicleDriver = value;
			
			if (key == "Land_Vehicle_Driver" && !value.empty()) 
				g_Config.landVehicleDriver = value;
			
			if (key == "Land_Vehicle_Driver_Reverse" && !value.empty()) 
				g_Config.landVehicleDriverReverse = value;
			
			if (key == "Water_Vehicle_Idles" && !value.empty()) 
				g_Config.waterVehicleIdles = value;
			
			if (key == "Land_Vehicle_Idles" && !value.empty()) 
				g_Config.landVehicleIdles = value;

			if (key == "Driver_Land_Vehicle_Damage" && !value.empty())
				g_Config.landVehicleDamageDriver = value;

			if (key == "Driver_Land_Vehicle_Damage_Reverse" && !value.empty())
				g_Config.landVehicleDamageDriverReverse = value;

			if (key == "Driver_Water_Vehicle_Damage" && !value.empty())
				g_Config.waterVehicleDamageDriver = value;

			if (key == "Passenger_Land_Vehicle_Damage" && !value.empty())
				g_Config.landVehicleDamagePassenger = value;

			if (key == "Passenger_Water_Vehicle_Damage" && !value.empty())
				g_Config.waterVehicleDamagePassenger = value;

			if (key == "Damage_50_Calibers" && !value.empty())
				g_Config.Damage_50_Calibers = value;
		}
    }
    file.close();
    
    // Validation
    if (g_Config.pressMode < 1) 
		g_Config.pressMode = 1;
    
	if (g_Config.pressWindow < 100) 
		g_Config.pressWindow = 100;
    
	if (g_Config.cooldown < 100) 
		g_Config.cooldown = 100;
} 

// 'CharacterObject :: RequestAnimation()' Execution Denied
bool IsInVehicle() 
{
    __try 
	{
        int vehicleState = *(int*)ADDR_VehicleState;
        return (vehicleState != 0);
    } 
	
	__except(EXCEPTION_EXECUTE_HANDLER) 
	{
        return false;
    }
} 

// Main Character Alive \ Dead Tracker
bool Main_Character_Death_Status()
{
    bool result = false;

    __try
    {
        __asm
        {
            mov eax, 0x004F4E70
            call eax
            mov result, al
        }
    }

    __except(EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }

    return result;
} 

// Non-Interactive Sequence Tracker
bool Is_NIS_Active()
{
    bool result = false;

    __try
    {
        __asm
        {
            mov eax, 0x00441620
            call eax
            mov result, al
        }
    }

    __except(EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }

    return result;
}  

// Game State Tracker
bool Is_Game_In_Normal_Game_State()
{
    bool result = false;

    __try
    {
        __asm
        {
            mov eax, 0x004F5820
            call eax
            mov result, al
        }
    }

    __except(EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }

    return result;
} 

// Pause Menu Tracker
bool Is_Game_Paused_HUD()
{
    bool result = false;

    __try
    {
        __asm
        {
            mov eax, 0x005C02F0
            call eax
            mov result, al
        }
    }

    __except(EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }

    return result;
} 

// Screen Transition Tracker
bool Get_Screen_Effects_Transition_Blacked_Out()
{
    bool result = false;

    __try
    {
        __asm
        {
            mov eax, 0x005D27E0
            call eax
            mov result, al
        }
    }

    __except(EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }

    return result;
}

// Mission Tracker
bool Get_Mission_Active()
{
    bool result = false;

    __try
    {
        __asm
        {
            mov eax, 0x006420C0
            call eax
            mov result, al
        }
    }

    __except(EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }

    return result;
}

// Player Teleportation Tracker
bool Get_Is_Teleporting()
{
    bool result = false;

    __try
    {
        __asm
        {
            mov eax, 0x004F5A90
            call eax
            mov result, al
        }
    }

    __except(EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }

    return result;
}

// Antonio Ramuindo Montana \ Assassin \ Driver \ Enforcer Pointers
bool InitPlayerPointer() 
{
    const char* pattern = "\x8B\x0D\x00\x00\x00\x00\x85\xC9\x74\x09\xE8\x00\x00\x00\x00\x84\xC0\x75\x5F\x83\x7C\x24\x00\x00\x7E\x43"; 
	const char* mask = "xx????xxxxx????xxxxxxx??xx";
    uintptr_t addr = FindPattern(pattern, mask);
    
	if (!addr) 
		return false;
    
    g_PlayerPointerAddress = *(uintptr_t*)(addr + 2);
    
	return true;
} 

// Non-Playable Characters' Pointers
bool InitCVManager()
{
    const char* pattern = "\x8B\x15\x00\x00\x00\x00\x8B\x42\x40\x0F\xBF\x48\x38\x33\xFF\x85\xC9\x89\x44\x24\x14";
    const char* mask = "xx????xxxxxxxxxxxxxxx";
    uintptr_t addr = FindPattern(pattern, mask);

    if (!addr)
        return false;

    g_CVManagerAddress = *(uintptr_t*)(addr + 2);

    return true;
}

// 'RequestAnimation()' Pointer
bool InitPlayAnimation() 
{
    const char* pattern = "\x8B\xCE\xE8\x00\x00\x00\x00\x8D\x4C\x24\x54";
	const char* mask = "xxx????xxxx";
    uintptr_t addr = FindPattern(pattern, mask);
    
	if (!addr) 
		return false;
    
    uintptr_t funcAddr = 0;
    ReadCall(addr + 2, &funcAddr);
    g_PlayAnimationFunc = (PlayAnimationFunc)funcAddr;
    
	return true;
}

// Character Bones' Pointer
bool InitGetBonePosition() 
{
    // Pattern For 'GetBonePosition' call [push 0Dh = g_FPSConfig.bonePosition; Call GetBonePosition]
    const char* pattern = "\x6A\x0D\xE8\x00\x00\x00\x00\x8B\x08\x8B\x50\x04\x8B\x40\x08\x89\x4C\x24\x08\x89\x4C\x24\x44\x8D\x4C\x24\x14\x51";
	const char* mask = "xxx????xxxxxxxxxxxxxxxxxxxx";
    uintptr_t addr = FindPattern(pattern, mask);
    
	if (!addr) 
	{
        // Alternative Pattern Without Result Handling
        const char* pattern2 = "\x6A\x0D\xE8\x00\x00\x00\x00";
		const char* mask2 = "xxx????";
        addr = FindPattern(pattern2, mask2);
        
		if (!addr) 
			return false;
    }
    
    uintptr_t funcAddr = 0;
    ReadCall(addr + 2, &funcAddr);
    g_GetBonePosition = (GetBonePositionFunc)funcAddr;
    
	return true;
}

// 'Camera :: SetPosition()' Pointer
bool InitCameraSetPosition() 
{
    const char* pattern = "\x8B\xC1\x8B\x4C\x24\x04\x8B\x11\x89\x50\x64\x8B\x51\x04\x89\x50\x68\x8B\x49\x08\x89\x48\x6C\x8B\x88\x00\x00\x00\x00\x8B\x11\x83\xC0\x34\x89\x44\x24\x04\xFF\x62\x64"; 
	const char* mask = "xxxxxxxxxxxxxxxxxxxxxxxxx????xxxxxxxxxxxx";
    uintptr_t addr = FindPattern(pattern, mask);
    
	if (!addr) 
		return false;
    
    g_OriginalSetPosition = (SetPositionFunc)addr;
    
	return true;
}

// Player Pointer Initialisation
CharacterObject* GetPlayer() 
{
    if (!g_PlayerPointerAddress) 
		return NULL;
    
	__try 
	{
        CharacterObject** pp = (CharacterObject**)g_PlayerPointerAddress;
        return pp ? *pp : NULL;
    }
	
	__except(EXCEPTION_EXECUTE_HANDLER) 
	{ 
		return NULL; 
	}
} 

// Damage Weapon Detector
int Damage_Weapon(CharacterObject* character)
{
	__try
    {
        int damageWeapon = *(int*)((uintptr_t)character + 0x17C);
        return damageWeapon;
    }
    
	__except(EXCEPTION_EXECUTE_HANDLER) 
	{ 
		return 0; 
	}
}

// Animation Request ID
int Get_Animation_Request_ID(CharacterObject* character)
{
    if (!character)
        return -1;

    int result = -1;

    __try
    {
        __asm
        {
            push character
            mov eax, 0x005874C0
            call eax
            add esp, 4
            mov result, eax
        }
    }

    __except(EXCEPTION_EXECUTE_HANDLER)
    {
        return -1;
    }

    return result;
} 

// CVManager Singleton Resolver
void* GetCVManager()
{
    if (!g_CVManagerAddress)
        return nullptr;

    __try
    {
        return *(void**)(g_CVManagerAddress);
    }
    
	__except(EXCEPTION_EXECUTE_HANDLER) 
	{ 
		return nullptr; 
	}
}

// CVManager :: Characters' List
void** GetNPCList(int* outCount)
{
    __try
    {
        void* mgr = GetCVManager();

        if (!mgr)
        {
            *outCount = 0;
            return nullptr;
        }

        // CVManager::GetCharacters() = *(GameSet<CharacterObject>**)(this + 0x40)
        void* gameSet = *(void**)((uintptr_t)mgr + 64);

        if (!gameSet)
        {
            *outCount = 0;
            return nullptr;
        }

        *outCount = *(short*)((uintptr_t)gameSet + 56); // GameSet::count
        return *(void***)((uintptr_t)gameSet + 48);     // GameSet::pData
    }
    __except(EXCEPTION_EXECUTE_HANDLER)
    {
        *outCount = 0;
        return nullptr;
    }
}

// Characters' Vehicle Intention Detector
int Actual_Vehicle_Intention(CharacterObject* character)
{
    __try
    {
        int pilotState = *(int*)((uintptr_t)character + 0x2E8);
        return (pilotState > 2 && pilotState < 6) ? 1 : 0;
    }
    
	__except(EXCEPTION_EXECUTE_HANDLER) 
	{ 
		return 0; 
	}
} 

// Characters' Vehicle State Detector
int Get_Vehicle_State(CharacterObject* npc)
{
    __try
    {
        int pilotState = *(int*)((uintptr_t)npc + 0x2E8);
        return (pilotState == 3) ? 1 : 0;
    }
    
	__except(EXCEPTION_EXECUTE_HANDLER) 
	{ 
		return 0; 
	}
}

// Characters' Vehicle Types' Detector
bool NPC_IsInCar(CharacterObject* npc)
{
    if (!npc)
        return false;

    bool result = false;

    __try
    {
        __asm
        {
            push npc
            mov eax, 0x005873E0
            call eax
            add esp, 4
            mov result, al
        }
    }
    
	__except(EXCEPTION_EXECUTE_HANDLER) 
	{ 
		return false; 
	}

    return result;
}

bool NPC_IsInBoat(CharacterObject* npc)
{
    if (!npc)
        return false;

    bool result = false;

    __try
    {
        __asm
        {
            push npc
            mov eax, 0x00587400
            call eax
            add esp, 4
            mov result, al
        }
    }
    
	__except(EXCEPTION_EXECUTE_HANDLER) 
	{ 
		return false; 
	}

    return result;
}

// CharacterObject :: GetHealth()
int GetCharacterHealth(CharacterObject* character)
{
    if (!character)
        return -1;

    int result = -1;

    __try
    {
        __asm
        {
            mov ecx, character
            mov eax, 0x006E51D0
            call eax
            mov result, eax
        }
    }
    
	__except(EXCEPTION_EXECUTE_HANDLER) 
	{ 
		return -1; 
	}

    return result;
}

// Player Camera Matrix Initialisation
Matrix* GetCharacterMatrix(CharacterObject* character) 
{
    if (!character) 
		return NULL;
    
	__try 
	{
        return (Matrix*)((uintptr_t)character + 0x34);
    } 
	
	__except(EXCEPTION_EXECUTE_HANDLER) 
	{ 
		return NULL; 
	}
}

// 'CharacterObject :: RequestAnimation()' Initialisation
int PlayAnimation(CharacterObject* player, const std::string& animName, int priority) 
{
    if (!player || !g_PlayAnimationFunc) 
		return 0;
    
    // Converting std::string To char* For Hash Function
    char animBuffer[256];
    strncpy_s(animBuffer, animName.c_str(), sizeof(animBuffer) - 1);
    animBuffer[sizeof(animBuffer) - 1] = '\0';
    unsigned int hash = Hash(animBuffer);
    
    __try 
	{
        return g_PlayAnimationFunc(player, &hash, priority, 0);
    } 
	
	__except(EXCEPTION_EXECUTE_HANDLER) 
	{
        return 0;
    }
}

// 'CharacterObject :: RequestAnimation()' Triggers Under Appropriate Conditions
void Dodge(CharacterObject* player, const std::string& animName) 
{
    if (IsInVehicle()) 
		return;
    
	PlayAnimation(player, animName, 0);
}

// 1st Person Camera Triggers for Player Set Conditions
void __fastcall Camera_SetPositionHooked(void* camera, void* edx, Vector* pos) 
{
    if (g_FPSConfig.enabled) 
	{
        CharacterObject* player = GetPlayer();
        
        if (player && g_GetBonePosition) 
		{
            if (g_FPSConfig.footOnly && IsInVehicle()) 
			{
                __try 
				{
                    *(Vector*)((int)camera + 100) = *pos;
                    (*(void(__thiscall**)(int, int))(**(int**)((int)camera + 132) + 100))
                        (*(int*)((int)camera + 132), (int)camera + 52);
                } 
				
				__except(EXCEPTION_EXECUTE_HANDLER) 
				{
				}
                
				return;
            }
            
			__try 
			{
                Matrix* charMatrix = GetCharacterMatrix(player);
                
                if (charMatrix) 
				{
                    Vector* headPos = NULL;
                    
					__asm
					{
                        mov ecx, player
                        push g_FPSConfig.bonePosition
                        call g_GetBonePosition
                        mov headPos, eax
                    }
                    
                    if (headPos) 
					{
                        Vector fpView;
                        fpView.X = headPos->X;
                        fpView.Y = headPos->Y;
                        fpView.Z = headPos->Z;
                        fpView.Y += g_FPSConfig.heightAdjust;
                        Vector forward = charMatrix->GetForward();
                        fpView = fpView + (forward * g_FPSConfig.distanceAdjust);
                        Vector right = charMatrix->GetRight();
                        fpView = fpView + (right * g_FPSConfig.sideAdjust);
                        *pos = fpView;
                    }
                }
            } 
			
			__except(EXCEPTION_EXECUTE_HANDLER) 
			{
			}
        }
    }
    
	__try 
	{
        *(Vector*)((int)camera + 100) = *pos;
        (*(void(__thiscall**)(int, int))(**(int**)((int)camera + 132) + 100))
            (*(int*)((int)camera + 132), (int)camera + 52);
    } 
	
	__except(EXCEPTION_EXECUTE_HANDLER) 
	{
	}
}

// 1st Person Camera Hook Initialisation
bool InstallCameraHook() 
{
    if (!g_OriginalSetPosition) 
		return false;
    
	uintptr_t source = (uintptr_t)g_OriginalSetPosition, target = (uintptr_t)&Camera_SetPositionHooked;
    int relativeOffset = (int)(target - source - 5);
    DWORD oldProtect;
    
	if (!VirtualProtect((LPVOID)source, 5, PAGE_EXECUTE_READWRITE, &oldProtect))
        return false;
    
    *(BYTE*)(source) = 0xE9;
    *(int*)(source + 1) = relativeOffset;
    VirtualProtect((LPVOID)source, 5, oldProtect, &oldProtect);
    
    return true;
}

// Dodges & Evades Input Parsing
void CheckKeyboardKey(CharacterObject* player, int key, int index, PressTracker& tracker, const std::string& animation, unsigned long now) 
{
    bool pressed = (GetAsyncKeyState(key) & 0x8000) != 0;
    
    if (pressed && !g_KeyboardState[index]) 
	{
        g_KeyboardState[index] = true;
        tracker.CheckPress(now, g_Config.pressWindow);
        
        if (tracker.ShouldTrigger(g_Config.pressMode)) 
		{
            Dodge(player, animation);
            g_LastDodgeTime = now;
            tracker.Reset();
        }
    }
    
	if (!pressed) 
	{
        g_KeyboardState[index] = false;
        
		if (now - tracker.lastPressTime > g_Config.pressWindow)
            tracker.Reset();
    }
}

void CheckKeyboard(CharacterObject* player, unsigned long now) 
{
    CheckKeyboardKey(player, g_Config.frontKey, 0, g_Trackers.front, g_Config.frontAnimation, now);
    CheckKeyboardKey(player, g_Config.leftKey, 1, g_Trackers.left, g_Config.leftAnimation, now);
    CheckKeyboardKey(player, g_Config.rightKey, 2, g_Trackers.right, g_Config.rightAnimation, now);
    CheckKeyboardKey(player, g_Config.backKey, 3, g_Trackers.back, g_Config.backAnimation, now);
    CheckKeyboardKey(player, g_Config.leftPeekShootKey, 4, g_Trackers.leftPeekShoot, g_Config.leftPeekShootAnimation, now);
    CheckKeyboardKey(player, g_Config.rightPeekShootKey, 5, g_Trackers.rightPeekShoot, g_Config.rightPeekShootAnimation, now);
}

void CheckControllerButton(CharacterObject* player, WORD buttons, WORD button, int index, PressTracker& tracker, const std::string& animation, unsigned long now) 
{
    bool pressed = (buttons & button) != 0;
    
    if (pressed && !g_ControllerState[index]) 
	{
        g_ControllerState[index] = true;
        tracker.CheckPress(now, g_Config.pressWindow);
        
        if (tracker.ShouldTrigger(g_Config.pressMode)) 
		{
            Dodge(player, animation);
            g_LastDodgeTime = now;
            tracker.Reset();
        }
    }
	
    if (!pressed) 
	{
        g_ControllerState[index] = false;
        
		if (now - tracker.lastPressTime > g_Config.pressWindow)
            tracker.Reset();
    }
}

void CheckController(CharacterObject* player, unsigned long now) 
{
    XINPUT_STATE state;
    ZeroMemory(&state, sizeof(XINPUT_STATE));
    
	if (XInputGetState(0, &state) != ERROR_SUCCESS)
        return;
    
    WORD buttons = state.Gamepad.wButtons;
    CheckControllerButton(player, buttons, g_Config.frontButton, 0, g_Trackers.front, g_Config.frontAnimation, now);
    CheckControllerButton(player, buttons, g_Config.leftButton, 1, g_Trackers.left, g_Config.leftAnimation, now);
    CheckControllerButton(player, buttons, g_Config.rightButton, 2, g_Trackers.right, g_Config.rightAnimation, now);
    CheckControllerButton(player, buttons, g_Config.backButton, 3, g_Trackers.back, g_Config.backAnimation, now);
    CheckControllerButton(player, buttons, g_Config.leftPeekShootButton, 4, g_Trackers.leftPeekShoot, g_Config.leftPeekShootAnimation, now);
    CheckControllerButton(player, buttons, g_Config.rightPeekShootButton, 5, g_Trackers.rightPeekShoot, g_Config.rightPeekShootAnimation, now);
}

//  Direct-Input Devices Enumeration
BOOL CALLBACK EnumJoysticksCallback(const DIDEVICEINSTANCE* pdidInstance, VOID* pContext) 
{
    HRESULT hr = g_pDI->CreateDevice(pdidInstance->guidInstance, &g_pJoystick, NULL);
    
	if (SUCCEEDED(hr))
        return DIENUM_STOP;
    
	return DIENUM_CONTINUE;
}

// Direct-Input Initialisation
bool InitDirectInput() 
{
    HINSTANCE hInstance = GetModuleHandle(NULL);
	
	// Direct-Input Device Interface Creation
    HRESULT hr = DirectInput8Create(hInstance, DIRECTINPUT_VERSION, IID_IDirectInput8, (VOID**)&g_pDI, NULL);
    
	if (FAILED(hr))
        return false;
	
	// Direct-Input Device Enumeration
    hr = g_pDI->EnumDevices(DI8DEVCLASS_GAMECTRL, EnumJoysticksCallback, NULL, DIEDFL_ATTACHEDONLY);          
    
	if (FAILED(hr) || !g_pJoystick) 
	{
        if (g_pDI) 
		{
            g_pDI->Release();
            g_pDI = NULL;
        }
        
		return false;
    }
	
	// Simple Joystick
    hr = g_pJoystick->SetDataFormat(&c_dfDIJoystick2);                                                        
    
	if (FAILED(hr)) 
	{
        g_pJoystick->Release();
        g_pDI->Release();
        g_pJoystick = NULL;
        g_pDI = NULL;
        
		return false;
    }
	
	// Cooperative Level Initialisation
    HWND hwnd = GetForegroundWindow();                                                                        
    hr = g_pJoystick->SetCooperativeLevel(hwnd, DISCL_BACKGROUND | DISCL_NONEXCLUSIVE);
    
	if (FAILED(hr)) 
	{
        g_pJoystick->Release();
        g_pDI->Release();
        g_pJoystick = NULL;
        g_pDI = NULL;
        
		return false;
    }
	
	// Device Acquisition
    g_pJoystick->Acquire();                                                                                   
    g_DirectInputEnabled = true;
    
	return true;
}

// Direct-Input Cleanup
void CleanupDirectInput() 
{
    if (g_pJoystick) 
	{
        g_pJoystick->Unacquire();
        g_pJoystick->Release();
        g_pJoystick = NULL;
    }
    
	if (g_pDI) 
	{
        g_pDI->Release();
        g_pDI = NULL;
    }
    
	g_DirectInputEnabled = false;
}

void CheckDirectInputButton(CharacterObject* player, const DIJOYSTATE2& js, int buttonIndex, int stateIndex, PressTracker& tracker, const std::string& animation, unsigned long now)
{
    bool pressed = (buttonIndex >= 0 && buttonIndex < 128) && (js.rgbButtons[buttonIndex] & 0x80);
    
    if (pressed && !g_DirectInputState[stateIndex]) 
	{
        g_DirectInputState[stateIndex] = true;
        tracker.CheckPress(now, g_Config.pressWindow);
        
        if (tracker.ShouldTrigger(g_Config.pressMode)) 
		{
            Dodge(player, animation);
            g_LastDodgeTime = now;
            tracker.Reset();
        }
    }
    
	if (!pressed) 
	{
        g_DirectInputState[stateIndex] = false;
        
		if (now - tracker.lastPressTime > g_Config.pressWindow)
            tracker.Reset();
    }
}

void CheckDirectInput(CharacterObject* player, unsigned long now) 
{
    if (!g_DirectInputEnabled || !g_pJoystick)
        return;
    
	// Device Polling For Reading The Current State
    DIJOYSTATE2 js;
    ZeroMemory(&js, sizeof(DIJOYSTATE2));
    HRESULT hr = g_pJoystick->Poll();     						
    
	if (FAILED(hr)) 
	{
		// Device Lost, Try Reacquiring
        hr = g_pJoystick->Acquire();      						
        while (hr == DIERR_INPUTLOST)
            hr = g_pJoystick->Acquire();
        
		// Failed Rcquiring It Back
        if (FAILED(hr))                   						
            return; 
    }
    
	// Device State Acquisition
    hr = g_pJoystick->GetDeviceState(sizeof(DIJOYSTATE2), &js); 
    
	if (FAILED(hr))
        return;
	
    CheckDirectInputButton(player, js, g_Config.diFrontButton, 0, g_Trackers.front, g_Config.frontAnimation, now);
    CheckDirectInputButton(player, js, g_Config.diLeftButton, 1, g_Trackers.left, g_Config.leftAnimation, now);
    CheckDirectInputButton(player, js, g_Config.diRightButton, 2, g_Trackers.right, g_Config.rightAnimation, now);
    CheckDirectInputButton(player, js, g_Config.diBackButton, 3, g_Trackers.back, g_Config.backAnimation, now);
    CheckDirectInputButton(player, js, g_Config.diLeftPeekShootButton, 4, g_Trackers.leftPeekShoot, g_Config.leftPeekShootAnimation, now);
    CheckDirectInputButton(player, js, g_Config.diRightPeekShootButton, 5, g_Trackers.rightPeekShoot, g_Config.rightPeekShootAnimation, now);
} 

// Vehicles' Idles' Cancellation Input Detection
bool IsAnyRawInputActive()
{
    // Keyboard Detection
    for (int vk = 0x08; vk <= 0xFE; vk++)
        if (GetAsyncKeyState(vk) & 0x8000)
            return true;
	
	// Mouse Detection
	if (GetAsyncKeyState(VK_LBUTTON) & 0x8000) 
		return true;
	
	if (GetAsyncKeyState(VK_RBUTTON) & 0x8000) 
		return true;
	
	if (GetAsyncKeyState(VK_MBUTTON) & 0x8000) 
		return true;
	
	if (GetAsyncKeyState(VK_XBUTTON1) & 0x8000) 
		return true;
	
	if (GetAsyncKeyState(VK_XBUTTON2) & 0x8000) 
		return true;

    // Xbox Controller Detection 
    XINPUT_STATE xstate;
    ZeroMemory(&xstate, sizeof(XINPUT_STATE));

    if (XInputGetState(0, &xstate) == ERROR_SUCCESS)
    {
        XINPUT_GAMEPAD& gp = xstate.Gamepad;

        // Buttons
        if (gp.wButtons != 0)
            return true;

        // Triggers
        if (gp.bLeftTrigger > 30) 
			return true;
        
		if (gp.bRightTrigger > 30) 
			return true;

        // Sticks
        if (abs(gp.sThumbLX) > XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE)  
			return true;
        
		if (abs(gp.sThumbLY) > XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE)  
			return true;
        
		if (abs(gp.sThumbRX) > XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE) 
			return true;
        
		if (abs(gp.sThumbRY) > XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE) 
			return true;
    }

    // Direct-Input Detection
    if (g_DirectInputEnabled && g_pJoystick)
    {
        DIJOYSTATE2 js;
        ZeroMemory(&js, sizeof(DIJOYSTATE2));

        if (SUCCEEDED(g_pJoystick->Poll()) && SUCCEEDED(g_pJoystick->GetDeviceState(sizeof(DIJOYSTATE2), &js)))
        {
            for (int i = 0; i < 128; i++)
                if (js.rgbButtons[i] & 0x80) 
					return true;

            // Axes
            if (abs((int)js.lX - 32767) > 3000) 
				return true;
            
			if (abs((int)js.lY - 32767) > 3000) 
				return true;
            
			if (abs((int)js.lZ - 32767) > 3000) 
				return true;
            
			if (abs((int)js.lRx - 32767) > 3000) 
				return true;
            
			if (abs((int)js.lRy - 32767) > 3000) 
				return true;
            
			if (abs((int)js.lRz - 32767) > 3000) 
				return true;
        }
    }

    return false;
}

void CheckVehicleAnimation(CharacterObject* player)
{
    int vehicleState = *(int*)ADDR_VehicleState, Weapon_State = Get_Weapon_State(player), Reversing = Land_Vehicle_Reverse_Driving(player);
	unsigned long now = GetTickCount();
	
	if (vehicleState <= 0 || vehicleState > 2 || Weapon_State!= 0)
	{
		g_VehicleAnim.Reset();
		return;
	}
	
	if (!g_VehicleAnim.wasInVehicle)
    {
        g_VehicleAnim.idle = false;
        g_VehicleAnim.active = false;
        g_VehicleAnim.entryTime = now;      
        g_VehicleAnim.inputReleasedAt = now;      
        g_VehicleAnim.wasInVehicle = true;
		g_VehicleAnim.wasInputActive = false;
    }

    bool inputActive = IsAnyRawInputActive();
	
	if (g_VehicleAnim.wasInputActive && !inputActive)
        g_VehicleAnim.inputReleasedAt = now;

    g_VehicleAnim.wasInputActive = inputActive;
	
	if (inputActive)
    {
        if (!g_VehicleAnim.active)
        {			
			if (vehicleState == 1 && Reversing)
			{
				const std::string& anim = g_Config.landVehicleDriverReverse;
				PlayAnimation(player, anim, 0);
				g_VehicleAnim.active = true;
				g_VehicleAnim.idle = false;
			}
			
			else
			{
				const std::string& anim = (vehicleState == 1) ? g_Config.landVehicleDriver : g_Config.waterVehicleDriver;
				PlayAnimation(player, anim, 0);
				g_VehicleAnim.active = true;
				g_VehicleAnim.idle = false;
			}
        }
    }
    
	else
    {
        if (!g_VehicleAnim.idle)
		{
			bool entryDelayMet = (now - g_VehicleAnim.entryTime) >= 7532UL, releaseDelayMet = (now - g_VehicleAnim.inputReleasedAt) >= 7532UL;
			
			if (entryDelayMet && releaseDelayMet)
			{
				const std::string& anim = (vehicleState == 1) ? g_Config.landVehicleIdles : g_Config.waterVehicleIdles;
				PlayAnimation(player, anim, 90);
				g_VehicleAnim.idle = true;
				g_VehicleAnim.active = false;
			}			
		}
    }
}

void CheckNPCVehicleAnimations(void** pData, int count)
{
    unsigned long now = GetTickCount();

    if (now - g_LastNPCAnimCheck < NPC_ANIM_INTERVAL)
        return;

    g_LastNPCAnimCheck = now;

    if (!pData || count <= 0)
        return;

    // Main Character Detector
    CharacterObject* mainChar = GetPlayer();

    for (int i = 0; i < count && i < MAX_NPCS; i++)
    {
        CharacterObject* npc = (CharacterObject*)pData[i];

        if (!npc || npc == mainChar)
            continue;

        bool isBoat = NPC_IsInBoat(npc), isCar = NPC_IsInCar(npc);

        if (!isBoat && !isCar)
            continue;

        int seatingPosition = Get_Vehicle_State(npc), Weapon_State = Get_Weapon_State(npc), Reversing = Land_Vehicle_Reverse_Driving(npc);

        if (seatingPosition != 1 || Weapon_State != 0)
        {
            g_NPCStates[i].Reset();
            continue;
        }

        __try
        {
            float curX = *(float*)((uintptr_t)npc + 0x34 + 48), curZ = *(float*)((uintptr_t)npc + 0x34 + 56);
            NPCAnimState& state = g_NPCStates[i];
            float dx = curX - state.lastX, dz = curZ - state.lastZ;
            bool isMoving = (dx * dx + dz * dz) > 0.01f;
            state.lastX = curX;
            state.lastZ = curZ;

            if (isMoving)
            {
                if (!state.activePlayed)
                {
                    if (!isBoat && Reversing && isCar)
					{
						const std::string& anim = g_Config.landVehicleDriverReverse;
						PlayAnimation(npc, anim, 0);
						state.activePlayed = true;
						state.idlePlayed = false;
					}
					
					else
					{
						const std::string& anim = isBoat ? g_Config.waterVehicleDriver : g_Config.landVehicleDriver;
						PlayAnimation(npc, anim, 0);
						state.activePlayed = true;
						state.idlePlayed = false;	
					}
                }
            }
            
            else
            {
                if (!state.idlePlayed)
                {
                    const std::string& anim = isBoat ? g_Config.waterVehicleIdles : g_Config.landVehicleIdles;
                    PlayAnimation(npc, anim, 90);
                    state.idlePlayed = true;
                    state.activePlayed = false;
                }
            }
        }
        
        __except(EXCEPTION_EXECUTE_HANDLER) 
        { 
            continue; 
        }
    }
} 

// Vehicle Damage Animation Player
void ProcessDamageAnim(CharacterObject* character, DamageAnimState& state, bool isBoat)
{
    int currentHealth = GetCharacterHealth(character), Seat = Get_Vehicle_State(character), Reversing = Land_Vehicle_Reverse_Driving(character);

    if (currentHealth <= 0)
        return;

    if (state.lastHealth == -1)
    {
        state.lastHealth = currentHealth;
        return;
    }

    bool healthDropped = (currentHealth < state.lastHealth), stillAlive = (currentHealth > 1);

    if (healthDropped && stillAlive)
        state.animPlayed = false;

    if (!state.animPlayed && healthDropped && stillAlive)
    {		
		if (Seat)
		{
			if (!isBoat && Reversing)
			{
				const std::string& anim = g_Config.landVehicleDamageDriverReverse;
				PlayAnimation(character, anim, 0);
				state.animPlayed = true;
			}
			
			else
			{
				const std::string& anim = isBoat ? g_Config.waterVehicleDamageDriver : g_Config.landVehicleDamageDriver;
				PlayAnimation(character, anim, 0);
				state.animPlayed = true;
			}
		}
	
		else
		{
			const std::string& anim = isBoat ? g_Config.waterVehicleDamagePassenger : g_Config.landVehicleDamagePassenger;
			PlayAnimation(character, anim, 0);
			state.animPlayed = true;
		}
    }

    state.lastHealth = currentHealth;
}

void CheckNPCVehicleDamage(void** pData, int count)
{
    if (!pData || count <= 0)
        return;

    for (int i = 0; i < count && i < MAX_NPC_DAMAGE; i++)
    {
        CharacterObject* npc = (CharacterObject*)pData[i];

        if (!npc)
            continue;

        bool isBoat = NPC_IsInBoat(npc), Vehicle_Detected = isBoat || NPC_IsInCar(npc);
        int Injuries_50_Caliber = Get_50_Calibers(npc), Driver_Passenger = Actual_Vehicle_Intention(npc), Weapon_Damage = Damage_Weapon(npc);

        if (!Vehicle_Detected || Injuries_50_Caliber || !Driver_Passenger || Weapon_Damage == -1)
        {
            g_NPCDamageStates[i].Reset();
            continue;
        }

        ProcessDamageAnim(npc, g_NPCDamageStates[i], isBoat);
    }
} 

void Damages_50_Calibers(void** pData, int count)
{
    if (!pData || count <= 0)
        return;

    for (int i = 0; i < count && i < MAX_NPC_50CAL; i++)
    {
        CharacterObject* npc = (CharacterObject*)pData[i];

        if (!npc)
            continue;

        int get_50_calibers = Get_50_Calibers(npc);

        if (!get_50_calibers)
        {
            g_NPC50CalStates[i].Reset();
            continue;
        }

        int currentHealth = GetCharacterHealth(npc);

        if (currentHealth <= 0)
            continue;

        DamageAnimState& state = g_NPC50CalStates[i];

        // First Tick Initialisation
        if (state.lastHealth == -1)
        {
            state.lastHealth = currentHealth;
            continue;
        }

        bool healthDropped = (currentHealth < state.lastHealth), stillAlive = (currentHealth > 1);

        if (healthDropped && stillAlive)
            state.animPlayed = false;

        if (!state.animPlayed && healthDropped && stillAlive)
        {
            PlayAnimation(npc, g_Config.Damage_50_Calibers, 0);
            state.animPlayed = true;
        }

        state.lastHealth = currentHealth;
    }
}

// Health Recovery Animation
void Health_Recovery_Function(CharacterObject* player)
{
	int currentHealth = GetCharacterHealth(player), actionMap = *(int*)ADDR_VehicleState, weapon_State = Get_Weapon_State(player); 
	
	if (actionMap != 0 || currentHealth <= 0 || weapon_State != 0)
		return;
	
	if (HRS_Struct.lastHealth == -1)
	{
		HRS_Struct.lastHealth = currentHealth;
		HRS_Struct.lastX = *(float*)((uintptr_t)player + 0x34 + 48);
		HRS_Struct.lastY = *(float*)((uintptr_t)player + 0x34 + 52);
		HRS_Struct.lastZ = *(float*)((uintptr_t)player + 0x34 + 56);
		
		return;
	}
	
	bool healthGained = (currentHealth > HRS_Struct.lastHealth);
	int resultantHealth = currentHealth - HRS_Struct.lastHealth;
	
	if (healthGained && (resultantHealth > 2300))
		HRS_Struct.animationPlayed = false;
	
	float currentX = *(float*)((uintptr_t)player + 0x34 + 48), currentY = *(float*)((uintptr_t)player + 0x34 + 52), currentZ = *(float*)((uintptr_t)player + 0x34 + 56), dx = currentX - HRS_Struct.lastX, dy = currentY - HRS_Struct.lastY, dz = currentZ - HRS_Struct.lastZ;
	bool moving = (dx * dx + dy * dy + dz * dz) > 0.01f;
	HRS_Struct.lastX = currentX;                        
	HRS_Struct.lastY = currentY;
	HRS_Struct.lastZ = currentZ;
	
	if (!HRS_Struct.animationPlayed && healthGained && !moving)
	{
		PlayAnimation(player, g_Config.HealthRecovery, 0);
		HRS_Struct.animationPlayed = true;
	}
	
	HRS_Struct.lastHealth = currentHealth;
} 

// Main Character Switching Detection
void Main_Character_Switching_Function(CharacterObject* player)
{
	int actionMap = *(int*)ADDR_VehicleState;
	
	if (actionMap != 0)
		return;
	
	if (MCS_Struct.lastPackage == "")
	{
		MCS_Struct.lastPackage = CVM_Get_Main_Character_Package_Wrapper();
		
		return;
	}
	
	std::string currentPackage = CVM_Get_Main_Character_Package_Wrapper();
    bool Main_Character_Switched = (currentPackage != MCS_Struct.lastPackage), Valid_Package = (currentPackage == "MCP_Assassin" || currentPackage == "MCP_Driver" || currentPackage == "MCP_Enforcer"), Last_Valid_Package = (MCS_Struct.lastPackage == "MCP_Assassin" || MCS_Struct.lastPackage == "MCP_Driver" || MCS_Struct.lastPackage == "MCP_Enforcer");

    if (Main_Character_Switched)
    {
        MCS_Struct.animationPlayed = false;
		
        if (Last_Valid_Package && !Valid_Package)
            MCS_Struct.phoneCallTriggered = false;
    }

    unsigned long now = GetTickCount();

    if (!MCS_Struct.animationPlayed)
    {
        PlayAnimation(player, g_Config.SwitchingCharacters, 0);
        MCS_Struct.animationPlayed = true;
    }
	
    if (!MCS_Struct.phoneCallTriggered && Valid_Package)
    {
        if (MCS_Struct.lastIteration > 753)
            RunScript("'MainCharacter'.RequestCellPhoneAnswer(0);");

        if (MCS_Struct.lastIteration > MCS_Struct.currentIteration && MCS_Struct.currentIteration == -1)
        {
            RunScript("'MainCharacter'.RequestCellPhoneEnd();");
            MCS_Struct.phoneCallTriggered = true;
        }
    }

    MCS_Struct.lastPackage = currentPackage;
    MCS_Struct.lastIteration = MCS_Struct.currentIteration;
}

// Main Input Monitoring Thread
DWORD WINAPI InputThread(LPVOID lpParam) 
{
    Sleep(7532);
	InitDirectInput(); // Initialize DirectInput
    
    while (g_Running) 
	{
        Sleep(10);
        CharacterObject* p = GetPlayer();
        
        if (!p) 
            continue;
        
        unsigned long now = GetTickCount();
		float currentX = *(float*)((uintptr_t)p + 0x34 + 48), currentY = *(float*)((uintptr_t)p + 0x34 + 52), currentZ = *(float*)((uintptr_t)p + 0x34 + 56);
		bool Main_Menu = ((currentX > -3500.0f && currentX < -3400.0f) && (currentY > 19.0f && currentY < 20.0f) && (currentZ > -1800.0f && currentZ < -1700.0f));
		
		if (Main_Menu)
			continue;
		
		std::string currentPackage = CVM_Get_Main_Character_Package_Wrapper();
		bool currentNIS = Is_NIS_Active(), currentNormalGameState = Is_Game_In_Normal_Game_State(), currentGamePaused = Is_Game_Paused_HUD(), currentScreenFXTransition = Get_Screen_Effects_Transition_Blacked_Out(), currentMission = Get_Mission_Active(), currentTeleportation = Get_Is_Teleporting(), currentDeathStatus = Main_Character_Death_Status(), Combat_Tutorial = (currentPackage == "MCP_ArmyTony");
		int currentAnimation = Get_Animation_Request_ID(p), Seating_Position = Get_Vehicle_State(p), npcCount = 0;
		
		MCS_Struct.currentIteration = GlobalSoundGetCurrentMs();
		
		if (!currentNIS && !currentGamePaused && !currentScreenFXTransition && !currentTeleportation && !currentDeathStatus)
		{
			if (currentNormalGameState && !currentAnimation)
			{
				Health_Recovery_Function(p);

				if (!Combat_Tutorial)
					Main_Character_Switching_Function(p);
			}
			
			// Main Character Vehicle Idles
			if (Seating_Position)
				CheckVehicleAnimation(p);
			
			// CharacterObject::GetInstance() Tracker : Vehicle / Weapon Prop Damages \ Non-Playable Characters' Vehicles' Idles
			void** npcList = GetNPCList(&npcCount);
			Damages_50_Calibers(npcList, npcCount);
			CheckNPCVehicleDamage(npcList, npcCount);
			CheckNPCVehicleAnimations(npcList, npcCount);
		}

        if (now - g_LastDodgeTime >= g_Config.cooldown) 
        {
            CheckKeyboard(p, now);
            CheckController(p, now);
            CheckDirectInput(p, now);
        }
    } 
	
	CleanupDirectInput();

    return 0;
}

// DLL Entry Point
BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID lpReserved) 
{
    if (reason == DLL_PROCESS_ATTACH) 
	{
        DisableThreadLibraryCalls(hModule);
		LoadConfig(); // Load Configuration
		HRS_Struct.Reset();
		MCS_Struct.Reset();

		// Characters' States' Initialisation
		for (int i = 0; i < MAX_NPCS; i++)       
			g_NPCStates[i].Reset();
		
		for (int i = 0; i < MAX_NPC_DAMAGE; i++)  
			g_NPCDamageStates[i].Reset();
		
		for (int i = 0; i < MAX_NPC_50CAL; i++)   
			g_NPC50CalStates[i].Reset();
		
		// Initialize Dodges & Evades system; First Person Camera system; Characters' Tracking System
		bool dodgesOK = InitPlayerPointer() && InitPlayAnimation(), fpsOK = InitGetBonePosition() && InitCameraSetPosition(), npcOK = InitCVManager(), scriptOK  = Initialise_Run_Script();

		// Install Camera Hook If 1st Person System Initialized Successfully
		if (fpsOK)
			InstallCameraHook();

		// Start Input Thread If At Least 1 System Initialized
		if (dodgesOK || fpsOK || npcOK || scriptOK)
			g_InputThreadHandle = CreateThread(NULL, 0, InputThread, NULL, 0, NULL);
    }
    
	if (reason == DLL_PROCESS_DETACH) 
	{
		// Signal Thread To Stop
        g_Running = false;                                             
        
        // Wait For Thread To Finish
        if (g_InputThreadHandle) 
		{
			// Wait Maximum 1 Second
            WaitForSingleObject(g_InputThreadHandle, 1000);            
            CloseHandle(g_InputThreadHandle);
            g_InputThreadHandle = NULL;
        }
	}
    
	return TRUE;
}