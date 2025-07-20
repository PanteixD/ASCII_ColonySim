#include <Windows.h>
#include <vector>
#include <string>
#include <sstream>
#include <cstdlib>
#include <ctime>
#include <map>
#include <cmath>
#include <algorithm>
#include <set>
#include <queue>
#include <fstream>
#include <locale>
#include <codecvt>
#include <functional>
#include "SDKs/discord/include/discord_game_sdk.h"
#include "SDKs/discord/cpp/core.h"
#include <chrono>
//#pragma comment(lib, "discord_game_sdk.dll.lib")

// --- Discord Rich Presence State ---
namespace DiscordRichPresence {
    // Prototypes for functions defined later
    void init();
    void update();
    void shutdown();
}


// --- Pathfinding & Navigation ---
// (This Point3D struct should be above initGameData, as it's used in global contexts)
struct Point3D {
    int x, y, z;
    // NEW: Define operator< for Point3D to allow its use in std::set and other sorted containers.
    bool operator<(const Point3D& other) const {
        if (z != other.z) return z < other.z;
        if (y != other.y) return y < other.y;
        return x < other.x;
    }
};

// NEW: Define a Point2D struct for 2D coordinate handling where comparison is needed
// This will be used in the 'spread_ore' function.
struct Point2D {
    int x, y; // Changed from long to int for consistency with game coordinates

    // Define operator< for Point2D for use in std::set (still needed if you use Point2D in std::map keys elsewhere, but not for this specific visited optimization)
    bool operator<(const Point2D& other) const {
        if (y != other.y) return y < other.y; // Compare by y first
        return x < other.x; // Then by x
    }
};
// END of new Point2D struct

// NEW: A* pathfinding (BFS if costs are uniform)
std::vector<Point3D> findPath(Point3D start, Point3D end) {
    if (start.x == end.x && start.y == end.y && start.z == end.z) {
        return { start }; // Already at destination
    }

    std::queue<Point3D> q;
    std::map<Point3D, Point3D> came_from; // To reconstruct the path
    std::set<Point3D> visited;           // To avoid cycles and redundant checks

    q.push(start);
    visited.insert(start);

    bool path_found = false;

    while (!q.empty()) {
        Point3D current = q.front();
        q.pop();

        if (current.x == end.x && current.y == end.y && current.z == end.z) {
            path_found = true;
            break;
        }

        // Neighbors on the same Z-level (8 directions)
        for (int dy = -1; dy <= 1; ++dy) {
            for (int dx = -1; dx <= 1; ++dx) {
                if (dx == 0 && dy == 0) continue;

                Point3D neighbor = { current.x + dx, current.y + dy, current.z };

                if (isWalkable(neighbor.x, neighbor.y, neighbor.z) && visited.find(neighbor) == visited.end()) {
                    visited.insert(neighbor);
                    came_from[neighbor] = current;
                    q.push(neighbor);
                }
            }
        }

        // Check for stairs to move between Z-levels
        TileType currentTile = Z_LEVELS[current.z][current.y][current.x].type;

        // Try to go DOWN
        if (currentTile == TileType::STAIR_DOWN && current.z > 0) {
            Point3D neighbor = { current.x, current.y, current.z - 1 };
            // Check if the tile below is a STAIR_UP AND is walkable (the stair itself is walkable)
            if (Z_LEVELS[neighbor.z][neighbor.y][neighbor.x].type == TileType::STAIR_UP &&
                isWalkable(neighbor.x, neighbor.y, neighbor.z) && visited.find(neighbor) == visited.end()) {
                visited.insert(neighbor);
                came_from[neighbor] = current;
                q.push(neighbor);
            }
        }

        // Try to go UP
        if (currentTile == TileType::STAIR_UP && current.z < TILE_WORLD_DEPTH - 1) {
            Point3D neighbor = { current.x, current.y, current.z + 1 };
            // Check if the tile above is a STAIR_DOWN AND is walkable (the stair itself is walkable)
            if (Z_LEVELS[neighbor.z][neighbor.y][neighbor.x].type == TileType::STAIR_DOWN &&
                isWalkable(neighbor.x, neighbor.y, neighbor.z) && visited.find(neighbor) == visited.end()) {
                visited.insert(neighbor);
                came_from[neighbor] = current;
                q.push(neighbor);
            }
        }
    }

    std::vector<Point3D> path;
    if (path_found) {
        Point3D current = end;
        while (!(current.x == start.x && current.y == start.y && current.z == start.z)) {
            path.push_back(current);
            current = came_from[current];
        }
        path.push_back(start);
        std::reverse(path.begin(), path.end());
    }
    return path;
}

int windowWidth = 800;  // Example width
int windowHeight = 600; // Example height

// --- Global Inspector Tool ---
bool isInspectorModeActive = false;
struct InspectorInfo { RECT rect; std::wstring info; };
std::vector<InspectorInfo> g_inspectorElements;



// --- Inspector-aware Rendering Macros & Prototypes ---
void renderTextInspectable_internal(HDC hdc, const std::wstring& text, int x, int y, COLORREF color, const wchar_t* s_text, const wchar_t* s_x, const wchar_t* s_y, const wchar_t* s_color, const wchar_t* s_caller, const std::wstring& extra_info = L"");
void renderCenteredTextInspectable_internal(HDC hdc, const std::wstring& text, int y, int windowWidth, COLORREF color, const wchar_t* s_text, const wchar_t* s_y, const wchar_t* s_windowWidth, const wchar_t* s_color, const wchar_t* s_caller, const std::wstring& extra_info = L"");
void renderBoxInspectable_internal(HDC hdc, RECT rect, COLORREF color, const wchar_t* s_rect, const wchar_t* s_color, const wchar_t* s_caller, const std::wstring& extra_info = L"");

#define RENDER_TEXT_INSPECTABLE(hdc, text, x, y, color, ...) \
    renderTextInspectable_internal(hdc, text, x, y, color, L#text, L#x, L#y, L#color, __FUNCTIONW__, ##__VA_ARGS__)

#define RENDER_CENTERED_TEXT_INSPECTABLE(hdc, text, y, windowWidth, color, ...) \
    renderCenteredTextInspectable_internal(hdc, text, y, windowWidth, color, L#text, L#y, L#windowWidth, L#color, __FUNCTIONW__, ##__VA_ARGS__)

#define RENDER_BOX_INSPECTABLE(hdc, rect, color, ...) \
    renderBoxInspectable_internal(hdc, rect, color, L#rect, L#color, __FUNCTIONW__, ##__VA_ARGS__)


// --- Tile, Tag, & Biome System ---
enum class TileTag {
    NONE, STONE, TREE_PART, DIRT, WATER, MINERAL, SOIL,
    SEDIMENTARY, IGNEOUS_INTRUSIVE, IGNEOUS_EXTRUSIVE, METAMORPHIC, INNER_STONE,
    WOOD, ITEM, CHUNK, FLUID,
    TREE_TRUNK, TREE_BRANCH, TREE_LEAF, CACTUS_PART,
    STRUCTURE, FURNITURE, LIGHTS, PRODUCTION,
    BLUEPRINT_TAG,
    STOCKPILE_ZONE,
    METAL, ORE,
    SECURITY // NEW
};
enum class TileType {
    EMPTY, DIRT_FLOOR, GRASS, SAND, SNOW, ICE, STONE_FLOOR, WOOD_FLOOR,
    JUNGLE_GRASS,
    // Stones
    CHALK, CHERT, CLAYSTONE, CONGLOMERATE, DOLOMITE, LIMESTONE, MUDSTONE, ROCK_SALT, SANDSTONE, SHALE, SILTSTONE,
    DIORITE, GABBRO, GRANITE, ANDESITE, BASALT, DACITE, OBSIDIAN, RHYOLITE,
    GNEISS, MARBLE, PHYLLITE, QUARTZITE, SCHIST, SLATE,
    CORESTONE, WATER, MOLTEN_CORE, RICH_MINERALS,
    // Tree/Cactus Base Types (for generation logic)
    OAK, ACACIA, SPRUCE, BIRCH, PINE, POPLAR, CECROPIA,
    COCOA, CYPRESS, MAPLE, PALM, TEAK, SAGUARO, PRICKLYPEAR, CHOLLA,
    // --- Tree & Cactus Parts ---
    // Generic
    TRUNK, BRANCH, LEAF,
    // Oak
    OAK_TRUNK, OAK_BRANCH, OAK_LEAF,
    // Acacia
    ACACIA_TRUNK, ACACIA_BRANCH, ACACIA_LEAF,
    // Spruce
    SPRUCE_TRUNK, SPRUCE_BRANCH, SPRUCE_NEEDLE,
    // Birch
    BIRCH_TRUNK, BIRCH_BRANCH, BIRCH_LEAF,
    // Pine
    PINE_TRUNK, PINE_BRANCH, PINE_NEEDLE,
    // Poplar
    POPLAR_TRUNK, POPLAR_BRANCH, POPLAR_LEAF,
    // Cecropia
    CECROPIA_TRUNK, CECROPIA_BRANCH, CECROPIA_LEAF,
    // Cocoa
    COCOA_TRUNK, COCOA_BRANCH, COCOA_LEAF,
    // Cypress
    CYPRESS_TRUNK, CYPRESS_BRANCH, CYPRESS_FOLIAGE,
    // Maple
    MAPLE_TRUNK, MAPLE_BRANCH, MAPLE_LEAF,
    // Palm
    PALM_TRUNK, PALM_FROND,
    // Teak
    TEAK_TRUNK, TEAK_BRANCH, TEAK_LEAF,
    // Saguaro
    SAGUARO_TRUNK, SAGUARO_ARM,
    // Prickly Pear
    PRICKLYPEAR_PAD, PRICKLYPEAR_TUNA,
    // Cholla
    CHOLLA_TRUNK, CHOLLA_JOINT,
    // --- Resources on ground ---
    /// Stone Chunks
    STONE_CHUNK,
    CHALK_CHUNK, CHERT_CHUNK, CLAYSTONE_CHUNK, CONGLOMERATE_CHUNK, DOLOMITE_CHUNK, LIMESTONE_CHUNK, MUDSTONE_CHUNK,
    ROCK_SALT_CHUNK, SANDSTONE_CHUNK, SHALE_CHUNK, SILTSTONE_CHUNK, DIORITE_CHUNK, GABBRO_CHUNK, GRANITE_CHUNK,
    ANDESITE_CHUNK, BASALT_CHUNK, DACITE_CHUNK, OBSIDIAN_CHUNK, RHYOLITE_CHUNK, GNEISS_CHUNK, MARBLE_CHUNK,
    PHYLLITE_CHUNK, QUARTZITE_CHUNK, SCHIST_CHUNK, SLATE_CHUNK, CORESTONE_CHUNK,
    /// Woods
    OAK_WOOD, ACACIA_WOOD, SPRUCE_WOOD, BIRCH_WOOD, PINE_WOOD, POPLAR_WOOD, CECROPIA_WOOD,
    COCOA_WOOD, CYPRESS_WOOD, MAPLE_WOOD, PALM_WOOD, TEAK_WOOD, SAGUARO_WOOD, PRICKLYPEAR_WOOD, CHOLLA_WOOD,
    /// Metals
    COPPER_METAL, TIN_METAL, NICKEL_METAL, ZINC_METAL, IRON_METAL, LEAD_METAL,
    SILVER_METAL, TUNGSTEN_METAL, GOLD_METAL, PLATINUM_METAL, ALUMINUM_METAL,
    CHROMIUM_METAL, BISMUTH_METAL, RHODIUM_METAL, OSMIUM_METAL, IRIDIUM_METAL,
    COBALT_METAL, PALLADIUM_METAL, MITHRIL_METAL, ORICHALCUM_METAL, ADAMANTIUM_METAL, TITANIUM_METAL,
    /// Ores
    BISMUTHINITE_ORE, CASSITERITE_ORE, NATIVE_COPPER_ORE, GALENA_ORE, GARNIERITE_ORE,
    NATIVE_GOLD_ORE, HEMATITE_ORE, HORN_SILVER_ORE, LIMONITE_ORE, MAGNETITE_ORE,
    MALACHITE_ORE, NATIVE_PLATINUM_ORE, NATIVE_SILVER_ORE, SPHALERITE_ORE, TETRAHEDRITE_ORE,
    NATIVE_ALUMINUM_ORE, ADAMANTITE_ORE, WOLFRAMITE_ORE, SPERRYLITE_ORE, IRIDOSMINE_ORE,
    COBALTITE_ORE, PALLADINITE_ORE, MITHRITE_ORE, ORICALCITE_ORE, RUTILE_ORE,
    // --- Architect Placeables ---
    BLUEPRINT,
    GROWING_ZONE, HYDROPONICS_BASIN,
    WALL, STONE_WALL, ACOUSTIC_WALL_ITEM,
    COLUMN,
    STAIR_UP, STAIR_DOWN,
    BLACKBOARD_FURNITURE, SCHOOL_CHAIR_FURNITURE, SCHOOL_DESK_FURNITURE,
    HOSPITAL_BED,
    TORCH,
    ELECTRIC_LAMP,
    CHAIR, TABLE,
    RESEARCH_BENCH, ADVANCED_RESEARCH_BENCH,
    MINE_SHAFT,
    CARPENTRY_WORKBENCH, STONECUTTING_TABLE, SMITHY, BLAST_FURNACE, COINING_MILL,
    DRUG_LAB, PRINTING_PRESS_FURNITURE, TYPEWRITER,
    WINDMILL, WIND_TURBINE,
    ASSEMBLY_LINE,
    THEATRE_STAGE,
    TELESCOPE_FURNITURE, RADIO_FURNITURE, COMPUTER_FURNITURE,
    LIGHTHOUSE_BUILDING,
    SERVER, SWITCH, ROUTER,


    // Melee Weapon
    KNIGHT_WEAPON,


    // Ranged Weapon
    BOW, ARROW,



    // Tools/Items/Apparel
    PICKAXE, COMPASS_ITEM,


    // Security 
    SPIKE_TRAP,

    // Vehicles
    RAFT, BOAT_ITEM,





    // Resources
    WHEEL, COIN, LENS, GUNPOWDER_ITEM, WIRE, LIGHTBULB,


    // Apparel    
    GLASSES, KNIGHT_ARMOR,


    // Devices
    SMARTPHONE, TABLET, SMARTWATCH,
    MOUSE, SCREEN, KEYBOARD, // NEW


    // Chemicals
    CHEMICALS_ITEM,




    // Drugs    
    DRUGS_ITEM,

};

enum class CritterTag {
    // Main Groups
    NONE, MAMMAL, MARINE_MAMMAL, BIRD, INSECT, RODENT, REPTILE, ARACHNID,
    CRUSTACEAN, CNIDARIAN, AMPHIBIAN, FISH, CRYPTID, YOUKAI, MYTHICAL,
    BEAST, MEGAFAUNA, CELESTIAL, DEMON, ABERRATION, ENTITY,

    // Sub-Tags
    LIVESTOCK, DOMESTIC, UNDEAD, PRIMATE, HUMANOID, MARSUPIAL, MOLLUSK,
    ANNELID, DINOSAUR, DRAGONOID, FAE, CONSTRUCT, CHIMERA, GIANT, PLANTFOLK,
    CHIROPTERA, CETACEAN, MUSTELID, PACHYDERM,

    // Sub-Sub-Tags (Behavior/Attribute)
    PREDATOR, VERMIN, SCAVENGER, FLYING, AQUATIC, VENOMOUS, NOCTURNAL,
    MIGRATORY, PARASITIC, PACK_ANIMAL, BURROWING, DIURNAL, SWARMING, PARTHENOGENIC,
    SYNTHETIC, MUTATED, MOUNT,
};


enum class CritterType {
    // Mammals
    DEER, WOLF, PIG, COW, SHEEP, GOAT, CAT, DOG, MONKEY, KANGAROO, BEAR,
    // Marine Mammals
    DOLPHIN, WHALE,
    // Birds
    PIGEON, HAWK,
    // Insects
    BUTTERFLY, BEETLE,
    // Rodents
    RAT, SQUIRREL,
    // Reptiles
    SNAKE, LIZARD,
    // Arachnids
    SPIDER,
    // Crustaceans
    CRAB,
    // Fish
    TROUT, TUNA,
    // Undead
    ZOMBIE, SKELETON,
    // Mythical
    GRIFFIN,
    // Dragonoid
    WYVERN
};













struct TileData {
    std::wstring name;
    wchar_t character;
    COLORREF color;
    std::vector<TileTag> tags;
    TileType drops;
    float hardness;
    float value;
    TileType display_trunk_type = TileType::EMPTY;
    std::wstring symbol;
};
std::map<TileType, TileData> TILE_DATA;

enum class Biome { OCEAN, TUNDRA, BOREAL_FOREST, TEMPERATE_FOREST, JUNGLE, DESERT };
struct BiomeData { std::wstring name; COLORREF mapColor; };
std::map<Biome, BiomeData> BIOME_DATA;
Biome landingBiome = Biome::TEMPERATE_FOREST;

// --- Global Game State & Data ---
bool running = true;
enum class GameState { MAIN_MENU, WORLD_GENERATION_MENU, PLANET_CUSTOMIZATION_MENU, LANDING_SITE_SELECTION, REGION_SELECTION, PAWN_SELECTION, IN_GAME };
GameState currentState = GameState::MAIN_MENU;
std::wstring worldName = L"New World";
std::wstring solarSystemName = L"Sol System";
float g_startingTimezoneOffset = 0.0f;



// --- Font Management Globals ---
HFONT g_hDisplayFont = NULL;
const std::wstring FONT_CONFIG_FILE = L"font_config.txt";
std::vector<std::wstring> g_availableFonts;
std::wstring g_currentFontName = L"Consolas"; // The built-in default
std::wstring g_currentFontFile = L""; // Path to the currently loaded .ttf file
bool isInFontMenu = false;
bool isInResearchGraphView = false;
int fontMenu_selectedOption = 0;

// --- Time, Season, Weather & Lighting ---
long long gameTicks = 0;
int gameHour = 12, gameMinute = 0, gameSecond = 0, gameDay = 1, gameMonth = 0, gameYear = 1;
bool isFullMoon = false;
enum class Season { SPRING, SUMMER, AUTUMN, WINTER };
const std::vector<std::wstring> SeasonNames = { L"Spring", L"Summer", L"Autumn", L"Winter" };
const std::vector<std::wstring> MonthNames = { L"March", L"April", L"May", L"June", L"July", L"August", L"September", L"October", L"November", L"December", L"January", L"February" };
Season currentSeason = Season::SPRING;
enum class Weather { CLEAR, RAINING, SNOWING }; Weather currentWeather = Weather::CLEAR;
int temperature = 15; int fps = 0; ULONGLONG lastFPSTime = 0; int frameCount = 0;
int weatherChangeCooldown = 0;
enum class TimeOfDay { DAWN, MORNING, MIDDAY, AFTERNOON, EVENING, DUSK, NIGHT, MIDNIGHT };
TimeOfDay currentTimeOfDay = TimeOfDay::MIDDAY;
float currentLightLevel = 1.0f;
struct LightSource { int x, y, z, radius; };
std::vector<LightSource> g_lightSources;
const int HOUR_SLOWNESS_FACTOR = 2;
const long long BASE_TICKS_PER_DAY = 24LL * 60 * 60;
const long long TICKS_PER_DAY = BASE_TICKS_PER_DAY * HOUR_SLOWNESS_FACTOR;

int TROPOSPHERE_TOP_Z_LEVEL = 0;

// -- Undead Invasion State --
long long lastUndeadSpawnCheck = 0;
const long long UNDEAD_SPAWN_INTERVAL = TICKS_PER_DAY / 2; // Check twice per day
const int UNDEAD_SPAWN_CHANCE_PER_1000 = 5; // 0.5% chance per check

// Critter Data
struct CritterData {
    std::wstring name;
    wchar_t character;
    COLORREF color;
    std::vector<CritterTag> tags;
    int wander_speed; // Ticks between moves
};

struct Critter {
    CritterType type;
    int x, y, z;
    int wanderCooldown;
    int targetPawnIndex = -1;
};

std::map<CritterType, CritterData> g_CritterData;
std::map<CritterTag, std::wstring> g_CritterTagNames;
std::vector<Critter> g_critters;
std::map<Biome, std::vector<CritterType>> g_BiomeCritters;



// --- Z-Layer System ---
enum class Stratum {
    INNER_CORE, OUTER_CORE, LOWER_MANTLE, UPPER_MANTLE, ASTHENOSPHERE, LITHOSPHERE, CRUST,
    HYDROSPHERE, BIOSPHERE, ATMOSPHERE,
    TROPOSPHERE, STRATOSPHERE, MESOSPHERE, THERMOSPHERE, EXOSPHERE,
    OUTER_SPACE_PLANET_VIEW, OUTER_SPACE_SYSTEM_VIEW, OUTER_SPACE_BEYOND
};
struct StratumInfo { std::wstring name; Stratum type; int depth; };
const std::vector<StratumInfo> strataDefinition = {
    { L"Inner Core",     Stratum::INNER_CORE,             5 },
    { L"Outer Core",     Stratum::OUTER_CORE,             10},
    { L"Lower Mantle",   Stratum::LOWER_MANTLE,           15},
    { L"Upper Mantle",   Stratum::UPPER_MANTLE,           10},
    { L"Asthenosphere",  Stratum::ASTHENOSPHERE,          3 },
    { L"Lithosphere",    Stratum::LITHOSPHERE,            5 },
    { L"Crust",          Stratum::CRUST,                  2 },
    { L"Hydrosphere",    Stratum::HYDROSPHERE,            1 },
    { L"Biosphere",      Stratum::BIOSPHERE,              1 },
    { L"Atmosphere",     Stratum::ATMOSPHERE,             10 },
    { L"Troposphere",    Stratum::TROPOSPHERE,            3 },
    { L"Stratosphere",   Stratum::STRATOSPHERE,           2 },
    { L"Mesosphere",     Stratum::MESOSPHERE,             2 },
    { L"Thermosphere",   Stratum::THERMOSPHERE,           2 },
    { L"Exosphere",      Stratum::EXOSPHERE,              3 },
};
int TILE_WORLD_DEPTH = 0; int BIOSPHERE_Z_LEVEL = 0; int HYDROSPHERE_Z_LEVEL = 0; int ATMOSPHERE_TOP_Z = 0; int currentZ = 0;





// --- Tabs, Menus, and UI State ---
enum class Tab { NONE, ARCHITECT, WORK, RESEARCH, STUFFS, MENU }; Tab currentTab = Tab::NONE;
// Architect GUI
enum class ArchitectCategory { ORDERS, ZONES, STRUCTURE, STORAGE, LIGHTS, PRODUCTION, FURNITURE, DECORATION }; ArchitectCategory currentArchitectCategory = ArchitectCategory::ORDERS;
int architectGizmoSelection = 0; bool isSelectingArchitectGizmo = false;
enum class ArchitectMode { NONE, DESIGNATING_MINE, DESIGNATING_CHOP, DESIGNATING_BUILD, DESIGNATING_STOCKPILE, DESIGNATING_DECONSTRUCT };
ArchitectMode currentArchitectMode = ArchitectMode::NONE;
TileType buildableToPlace;
bool isDrawingDesignationRect = false; int designationStartX = -1, designationStartY = -1;
// Pawn Info Panel
enum class PawnInfoTab { OVERVIEW, ITEMS, HEALTH, SKILLS, RELATIONS, GROUPS, THOUGHTS, PERSONALITY }; PawnInfoTab currentPawnInfoTab = PawnInfoTab::OVERVIEW;
int pawnInfo_scrollOffset = 0; 
int pawnInfo_selectedLine = 0;
int inspectedPawnIndex = -1; int workUI_selectedPawn = 0; int workUI_selectedJob = 0; int menuUI_selectedOption = 0;
int pawnItems_scrollOffset = 0;
bool isInSettingsMenu = false; int settingsUI_selectedOption = 0;
int targetFPS = 60;
// --- New State for Stuffs Tab ---
enum class StuffsCategory { STONES, CHUNKS, WOODS, METALS, ORES, TREES, CRITTERS };
const std::vector<std::wstring> StuffsCategoryNames = { L"Stones", L"Chunks", L"Woods", L"Metals", L"Ores", L"Trees", L"Critters" };
StuffsCategory currentStuffsCategory = StuffsCategory::STONES;
int stuffsUI_selectedItem = 0;
int stuffsUI_scrollOffset = 0; // New: Scroll offset for the stuffs item list
bool g_stuffsAlphabeticalSort = true; // Sorting table content of the stuffs gui alphabeticaly

// --- Research System ---
enum class ResearchEra { NEOLITHIC, CLASSICAL, MEDIEVAL, RENAISSANCE, INDUSTRIAL, MODERN, ATOMIC, INFORMATION, CYBERNETIC, INTERSTELLAR };
const std::vector<std::wstring> ResearchEraNames = { L"Neolithic", L"Classical", L"Medieval", L"Renaissance", L"Industrial", L"Modern", L"Atomic", L"Information", L"Cybernetic", L"Interstellar" };
enum class ResearchCategory {
    ALL, CONSTRUCTION, CRAFTING, APPAREL, COOKING, FARMING, ANIMALS, STORAGE, COMBAT, SECURITY,
    MEDICINE, HOSPITALITY, TECH, POWER, TRANSPORT, RECREATION, SCIENCE, SPACE,
    LIGHTS
};
std::vector<std::wstring> ResearchCategoryNames = {
    L"All", L"Construction", L"Crafting", L"Apparel", L"Cooking", L"Farming", L"Animals", L"Storage", L"Combat", L"Security",
    L"Medicine", L"Hospitality", L"Technology", L"Power", L"Transport", L"Recreation", L"Science", L"Space", L"Lights" // NEW
};

// in Global Game State & Data -> Research System section
struct ResearchProject { std::wstring id, name, description; int cost; ResearchEra era; ResearchCategory category; std::vector<std::wstring> prerequisites; std::vector<std::wstring> unlocks; };
std::map<std::wstring, ResearchProject> g_allResearch;
std::set<std::wstring> g_completedResearch;
std::wstring g_currentResearchProject = L"";
int g_researchProgress = 0;

// UI State for Research Panel
ResearchEra researchUI_selectedEra = ResearchEra::NEOLITHIC;
ResearchCategory researchUI_selectedCategory = ResearchCategory::ALL;
int researchUI_selectedProjectIndex = 0;
std::vector<std::wstring> researchUI_projectList;
std::set<TileType> g_unlockedBuildings;
int researchUI_scrollOffset = 0;

// SPAWNABLE STRUCT DEFINITION
enum class SpawnableType { TILE, CRITTER };
struct Spawnable {
    SpawnableType type;
    std::wstring name;
    union {
        TileType tile_type;
        CritterType critter_type;
    };
};

// --- Debug Mode ---
bool isDebugMode = false;
bool isBrightModeActive = false;
bool isDebugCritterListVisible = false;
enum class DebugMenuState { NONE, SPAWN, HOUR, WEATHER, PLACING_TILE };
DebugMenuState currentDebugState = DebugMenuState::NONE;
int spawnMenuSelection = 0;
std::vector<Spawnable> g_spawnMenuList;
std::wstring spawnMenuSearch;
bool spawnMenuIsSearching = false;
Spawnable g_spawnableToPlace;
bool isPlacingWithBrush = false;
int spawnUI_scrollOffset = 0; // NEW: Scroll offset for the debug spawn list

// --- Solar System & Planet Generation Data ---
enum class WorldType { EARTH_LIKE, SINGLE_CONTINENT, ARCHIPELAGO }; const std::vector<std::wstring> WorldTypeNames = { L"Earth-like", L"Single Continent", L"Archipelago" };
WorldType selectedWorldType = WorldType::EARTH_LIKE;
const int PLANET_MAP_WIDTH = 220, PLANET_MAP_HEIGHT = 100;
int numberOfPlanets = 5; int worldGen_selectedOption = 0; bool worldGen_isNaming = false;
int planetCustomization_selected = 0; bool planetCustomization_isEditing = false;
struct Planet { std::wstring name; WorldType type; std::vector<std::vector<Biome>> biomeMap; double orbitalRadius, currentAngle, orbitalSpeed; COLORREF color; int size; };
std::vector<Planet> solarSystem; struct Moon { double orbitalRadius, currentAngle, orbitalSpeed; COLORREF color; int size; }; Moon homeMoon;
struct Star { float x, y, dx; int size; COLORREF color; };  std::vector<Star> distantStars; int g_homeSystemStarIndex = -1;
int landingSiteX = -1, landingSiteY = -1; int finalStartX = -1, finalStartY = -1;

// --- In-Game World & Map Data ---
const int BUILD_WORK_REQUIRED = 200;
struct Tree; // Forward declaration
struct MapCell {
    TileType type = TileType::EMPTY;
    TileType underlying_type = TileType::EMPTY;
    TileType target_type = TileType::EMPTY; // What this cell will become (for blueprints)
    int construction_progress = 0;          // Ticks of work applied
    std::vector<TileType> itemsOnGround;
    Tree* tree = nullptr; // Pointer to the tree this cell is part of
    int stockpileId = -1; // NEW: ID of the stockpile this cell belongs to, -1 if none
};
const int WORLD_WIDTH = 120, WORLD_HEIGHT = 60;
std::vector<std::vector<std::vector<MapCell>>> Z_LEVELS;
std::vector<std::wstring> designations(WORLD_HEIGHT, std::wstring(WORLD_WIDTH, L' '));
const int MAX_STACK_SIZE = 64;

struct ContinentInfo {
    bool found = false;
    std::wstring name;
    std::vector<POINT> tiles;
    std::set<Biome> biomes;
    float avgTemp = 0.0f;
    float timezoneOffset = 0.0f;
};


// --- Camera & Viewport ---
// Global character dimensions (adjusted for smaller font)
int charWidth = 9;
int charHeight = 16;

const int VIEWPORT_WIDTH_TILES = 80; const int VIEWPORT_HEIGHT_TILES = 26;
int cameraX = (WORLD_WIDTH - VIEWPORT_WIDTH_TILES) / 2;
int cameraY = (WORLD_HEIGHT - VIEWPORT_HEIGHT_TILES) / 2;
int followedPawnIndex = -1;

// --- Tree System ---
struct TreePart { int x, y, z; TileType type; };
struct Tree { int id; TileType type; int rootX, rootY, rootZ; std::vector<TreePart> parts; };
std::map<int, Tree> a_trees; // Use map for stable IDs
int nextTreeId = 0;
struct FallenTree {
    int treeId; TileType baseType;
    std::vector<TreePart> initialParts;
    int fallStep;
    int fallDirectionX, fallDirectionY;
};
std::vector<FallenTree> a_fallingTrees;

// --- Job & Pawn Data ---
struct Backstory { std::wstring name; std::wstring description; };
std::map<std::wstring, Backstory> g_Backstories;
std::map<std::wstring, std::wstring> g_SkillDescriptions;

enum class JobType { Mine, Chop, Farm, Build, Haul, Cook, Hunt, Research, Deconstruct }; // <-- ADD Deconstruct
const std::vector<std::wstring> JobTypeNames = { L"Mining", L"Chopping", L"Farming", L"Construction", L"Hauling", L"Cooking", L"Hunting", L"Research", L"Deconstruction" };
struct Job {
    JobType type;
    int x, y, z;          // Target/Primary location (e.g., where to mine, where to build)
    int treeId = -1;      // For chop jobs
    TileType itemType = TileType::EMPTY; // For haul jobs: type of item to haul
    int itemSourceX = -1; // For haul jobs: source X of the item
    int itemSourceY = -1; // For haul jobs: source Y of the item
    int itemSourceZ = -1; // For haul jobs: source Z of the item
};
std::vector<Job> jobQueue;
struct Pawn {
    std::wstring name, gender, backstory; int age; std::vector<std::wstring> traits;
    bool isDrafted = false;
    int x = -1, y = -1, z = 0;
    std::wstring currentTask = L"Idle";
    int targetX = -1, targetY = -1, targetZ = -1; // Current target location for movement
    int wanderCooldown = 10;
    int jobTreeId = -1; // ID of the tree this pawn is assigned to chop
    int jobSearchCooldown = 0;
    std::map<std::wstring, int> skills; std::map<JobType, int> priorities;
    std::map<TileType, int> inventory;
    int haulCooldown = 0;

    int haulSourceX = -1, haulSourceY = -1, haulSourceZ = -1;
    int haulDestX = -1, haulDestY = -1, haulDestZ = -1;

    int ticksStuck = 0; // NEW: Counter for how long the pawn has been unable to move towards target

    // NEW: Pathfinding data
    std::vector<Point3D> currentPath; // Stores the sequence of (x,y,z) points to follow
    size_t currentPathIndex;          // Index of the next point in currentPath to move to
};
const int PAWN_INVENTORY_CAPACITY = 15; // NEW: Maximum items a pawn can carry.
std::vector<Pawn> rerollablePawns; std::vector<Pawn> colonists;
std::map<std::wstring, int> resources;
std::map<TileType, int> g_stockpiledResources;

// Stockpile Struct
struct Stockpile {
    int id;
    RECT rect; // {left, top, right, bottom} in world coordinates (x,y)
    int z;
    std::set<TileType> acceptedResources; // Items this stockpile accepts
    // For UI, to maintain order and easily iterate
    /* std::vector<TileType> allHaulableItems; */
};

// Global Stockpile Management
std::vector<Stockpile> g_stockpiles;
int nextStockpileId = 0;
std::map<int, int> g_unreachableStockpileCache;
int inspectedStockpileIndex = -1; // Index of the stockpile currently being configured (UI state)
int stockpilePanel_selectedLineIndex = -1; // -1 for Accept All, -2 for Decline All, 0-N for categories/items
int stockpilePanel_scrollOffset = 0; // For scrolling through item list in the stockpile panel
int stockpilePanel_selectedItemIndex = 0; // Currently selected item in the stockpile panel
// NEW: Global definitions for grouping items in stockpile UI
std::vector<TileTag> stockpilePanel_displayCategoriesOrder; // Order in which to display categories in UI
std::map<TileTag, std::wstring> g_tagNames; // Maps TileTag to a display name (e.g., TileTag::WOOD -> "Woods")
std::map<TileTag, std::vector<TileType>> g_haulableItemsGrouped; // Stores TileTypes grouped by their primary TileTag
std::map<TileTag, bool> stockpilePanel_categoryExpanded; // Stores expansion state for each category in the UI


// --- UI & Controls ---
int cursorX = WORLD_WIDTH / 2, cursorY = WORLD_HEIGHT / 2; int gameSpeed = 1, lastGameSpeed = 1;
int g_cursorSpeed = 1; // NEW: Cursor movement speed (tiles per keypress)
std::vector<std::vector<std::vector<bool>>> g_isTileReachable;

// --- Function Prototypes ---
void initGameData(); void initResearchData();
void computeGlobalReachability();
void handleInput(HWND hwnd); void updateGame(); void updateTime(); void updateSolarSystem(); void updateFallingTrees(); void resetGame();
Pawn generatePawn(); void generateFullWorld(Biome biome); void generatePlanetMap(Planet& planet); void generateSolarSystem(int numPlanets, bool preserveNames); void generateDistantStars(); void preparePawnSelection();
void spawnInitialCritters();
StratumInfo getStratumInfoForZ(int z); std::wstring getDaySuffix(int day);
void spawnTree(int x, int y, TileType type);
void fellTree(int treeId, const Pawn& chopper);
COLORREF applyLightLevel(COLORREF originalColor, float lightLevel);
void renderWrappedText(HDC hdc, const std::wstring& text, RECT& rect, COLORREF color);
void renderMainMenu(HDC hdc, int width, int height);
void renderWorldGenerationMenu(HDC hdc, int width, int height);
void renderPlanetCustomizationMenu(HDC hdc, int width, int height);
void renderLandingSiteSelection(HDC hdc, int width, int height);
void renderRegionSelection(HDC hdc, int width, int height);
void renderColonistSelection(HDC hdc, int width, int height);
void renderPawnInfoPanel(HDC hdc, int width, int height);
void renderWorkPanel(HDC hdc, int width, int height);
void renderResearchPanel(HDC hdc, int width, int height);
void renderMenuPanel(HDC hdc, int width, int height);
void renderSettingsPanel(HDC hdc, int width, int height);
void renderDebugUI(HDC hdc, int width, int height);
void renderInspectorOverlay(HDC hdc, HWND hwnd);
void renderStockpileReadout(HDC hdc, int width, int height);
void renderGame(HDC hdc, int width, int height);
void renderMinimap(HDC hdc, int startX, int startY);
void renderPlanetView(HDC hdc, int width, int height);
void renderSystemView(HDC hdc, int width, int height);
void renderBeyondView(HDC hdc, int width, int height);
void scanForFonts();
void renderFontMenu(HDC hdc, int width, int height);
void initPawnData();
void initCritterData();
void initBiomeCritterSpawns();
bool isCritterWalkable(int x, int y, int z);
void renderStockpilePanel(HDC hdc, int width, int height);
void updateUnlockedContent(const ResearchProject& project);



// NEW: For pawn pathfinding and logic
bool isWalkable(int x, int y, int z); // This should already be there, but double check
bool isDeconstructable(TileType type); // Add this prototype
struct Point3D; // Forward declaration for Point3D if not already done, needed for findPath
std::vector<Point3D> findPath(Point3D start, Point3D end); // Add this prototype



// --- ALL FUNCTION DEFINITIONS ---

// Helper function to convert wstring to string for Discord SDK
std::string WStringToString(const std::wstring& wstr) {
    if (wstr.empty()) {
        return std::string();
    }
    int size_needed = WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), NULL, 0, NULL, NULL);
    std::string strTo(size_needed, 0);
    WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), &strTo[0], size_needed, NULL, NULL);
    return strTo;
}

std::string getCurrentUIContext() {
    if (inspectedPawnIndex != -1 && inspectedPawnIndex < colonists.size()) {
        return "Inspecting " + WStringToString(colonists[inspectedPawnIndex].name);
    }
    if (inspectedStockpileIndex != -1 && inspectedStockpileIndex < g_stockpiles.size()) {
        return "Configuring Stockpile";
    }
    if (currentArchitectMode != ArchitectMode::NONE) {
        std::string mode_str = "Designating ";
        if (currentArchitectMode == ArchitectMode::DESIGNATING_MINE) mode_str += "Mine";
        else if (currentArchitectMode == ArchitectMode::DESIGNATING_CHOP) mode_str += "Chop";
        else if (currentArchitectMode == ArchitectMode::DESIGNATING_BUILD) mode_str += "Build";
        else if (currentArchitectMode == ArchitectMode::DESIGNATING_STOCKPILE) mode_str += "Stockpile";
        else if (currentArchitectMode == ArchitectMode::DESIGNATING_DECONSTRUCT) mode_str += "Deconstruct";
        return mode_str;
    }

    switch (currentTab) {
    case Tab::ARCHITECT: return "In Architect GUI";
    case Tab::WORK: return "Adjusting Work Priorities";
    case Tab::RESEARCH:
        if (isInResearchGraphView) return "Viewing Research Graph";
        if (!g_currentResearchProject.empty()) {
            // Get project name if current research is active
            if (g_allResearch.count(g_currentResearchProject)) {
                return "Researching: " + WStringToString(g_allResearch.at(g_currentResearchProject).name);
            }
        }
        return "In Research GUI";
    case Tab::STUFFS: return "Viewing Stuffs Database";
    case Tab::MENU:
        if (isInSettingsMenu) return "In Settings Menu";
        return "In Pause Menu";
    case Tab::NONE: // If no tab is open, check general activity
        if (colonists.size() > 0 && std::any_of(colonists.begin(), colonists.end(), [](const Pawn& p) { return p.currentTask != L"Idle"; })) {
            return "Colony busy with tasks";
        }
        return "Exploring the World";
    }
    return "Idle in game"; // Fallback for in-game if no specific UI is open
}

// --- Discord Rich Presence Implementation ---
namespace DiscordRichPresence {
    discord::Core* core{};
    std::chrono::steady_clock::time_point last_update_time;
    time_t game_start_timestamp = 0;

    void init() {
        // IMPORTANT: Replace with your own Client ID from the Discord Developer Portal!
        const char* clientID = ""; // <--- PUT YOUR CLIENT ID HERE

        auto result = discord::Core::Create(std::stoll(clientID), DiscordCreateFlags_NoRequireDiscord, &core);
        if (!core) {
            return;
        }
        last_update_time = std::chrono::steady_clock::now();
        game_start_timestamp = std::time(0);
        update(); // Set initial presence
    }

    void update() {
        if (!core) return;

        auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::seconds>(now - last_update_time).count() < 5) {
            return;
        }
        last_update_time = now;

        discord::Activity activity{};
        std::string details_str;
        std::string state_str;

        switch (currentState) {
        case GameState::MAIN_MENU:
            details_str = "In Main Menu";
            state_str = "Preparing for a new colony.";
            activity.GetAssets().SetLargeImage("game_logo");
            activity.GetAssets().SetLargeText("ASCII Colony Management");
            break;
        case GameState::WORLD_GENERATION_MENU:
            details_str = "Generating World";
            state_str = "Naming: " + WStringToString(worldName);
            activity.GetAssets().SetLargeImage("world_gen_icon");
            activity.GetAssets().SetLargeText("Generating a New World");
            break;
        case GameState::PLANET_CUSTOMIZATION_MENU:
            details_str = "Customizing Planet";
            state_str = "Current Planet: " + (solarSystem.empty() ? "N/A" : WStringToString(solarSystem[0].name));
            activity.GetAssets().SetLargeImage("planet_icon");
            activity.GetAssets().SetLargeText("Customizing Planetary Details");
            break;
        case GameState::LANDING_SITE_SELECTION:
            details_str = "Selecting Landing Site";
            state_str = "On " + (solarSystem.empty() ? "Homeworld" : WStringToString(solarSystem[0].name));
            activity.GetAssets().SetLargeImage("map_icon");
            activity.GetAssets().SetLargeText("Choosing a Drop Zone");
            break;
        case GameState::REGION_SELECTION:
            details_str = "Selecting Region";
            state_str = "Scouting the local terrain.";
            activity.GetAssets().SetLargeImage("map_icon");
            activity.GetAssets().SetLargeText("Pinpointing Landing Coordinates");
            break;
        case GameState::PAWN_SELECTION:
            details_str = "Selecting Colonists";
            state_str = "Assembling the pioneer crew.";
            activity.GetAssets().SetLargeImage("pawn_icon");
            activity.GetAssets().SetLargeText("Choosing the Future of the Colony");
            break;
        case GameState::IN_GAME: {
            std::wstring season_wstr = SeasonNames[static_cast<int>(currentSeason)];
            details_str = "Year " + std::to_string(gameYear) + std::string(", ") + WStringToString(season_wstr) + std::string(", ") + std::to_string(colonists.size()) + " colonists";
            state_str = getCurrentUIContext(); // Dynamically set based on current UI
            activity.GetTimestamps().SetStart(game_start_timestamp);
            activity.GetAssets().SetLargeImage("game_logo");
            break;
        }
        }

        activity.SetDetails(details_str.c_str());
        activity.SetState(state_str.c_str());

        core->ActivityManager().UpdateActivity(activity, [](discord::Result result) {});
    }

    void shutdown() {
        if (core) {
            core->ActivityManager().ClearActivity([](discord::Result result) {});
        }
    }
} // --- End of namespace DiscordPresence ---

void UpdateDisplayFont(HDC hdc) {
    // Delete the old font object if it exists to prevent GDI resource leaks.
    if (g_hDisplayFont) {
        DeleteObject(g_hDisplayFont);
        g_hDisplayFont = NULL;
    }

    // Attempt to create the font based on g_currentFontName and g_currentFontFile
    // Note: The font size (16, 9) should ideally be configurable or derived dynamically,
    // but for now, keep it consistent with your existing code.
    if (!g_currentFontFile.empty()) {
        g_hDisplayFont = CreateFont(16, 9, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, VARIABLE_PITCH | FF_DONTCARE, g_currentFontName.c_str());
    }

    // If custom font creation failed or if using the default, create Consolas.
    if (g_hDisplayFont == NULL) {
        g_hDisplayFont = CreateFont(16, 9, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, FIXED_PITCH | FF_MODERN, L"Consolas");
        // Ensure g_currentFontName reflects the fallback for consistency.
        g_currentFontName = L"Consolas";
        g_currentFontFile = L""; // Clear font file path for default
    }

    // Temporarily select the new font into the provided HDC to get its metrics.
    // This is important because GetTextMetrics depends on the font currently selected in the DC.
    HFONT hOldFontTemp = (HFONT)SelectObject(hdc, g_hDisplayFont);
    TEXTMETRIC tm;
    GetTextMetrics(hdc, &tm);
    charWidth = tm.tmAveCharWidth;
    charHeight = tm.tmHeight + tm.tmExternalLeading;
    SelectObject(hdc, hOldFontTemp); // Restore the original font to the DC.

    // Debug output (can keep these, they are useful)
    OutputDebugStringW((L"Font selected: " + g_currentFontName + L"\n").c_str());
    OutputDebugStringW((L"Calculated charWidth: " + std::to_wstring(charWidth) + L"\n").c_str());
    OutputDebugStringW((L"Calculated charHeight: " + std::to_wstring(charHeight) + L"\n").c_str());
}

void spawnInitialCritters() {
    g_critters.clear(); // Ensure we start with a clean slate

    const int INITIAL_CRITTER_COUNT = 15 + (rand() % 10); // Spawn 15-24 critters initially

    for (int i = 0; i < INITIAL_CRITTER_COUNT; ++i) {
        // Decide if we're spawning a land or aquatic critter
        bool spawn_aquatic = (rand() % 100 < 20); // 20% chance to try spawning an aquatic one

        CritterType type_to_spawn;
        bool found_type = false;

        if (spawn_aquatic) {
            const auto& aquatic_critters = g_BiomeCritters.at(Biome::OCEAN);
            if (!aquatic_critters.empty()) {
                type_to_spawn = aquatic_critters[rand() % aquatic_critters.size()];
                found_type = true;
            }
        }

        // If not spawning aquatic, or if aquatic list was empty, spawn a land critter
        if (!found_type) {
            if (g_BiomeCritters.count(landingBiome) && !g_BiomeCritters.at(landingBiome).empty()) {
                const auto& possible_critters = g_BiomeCritters.at(landingBiome);
                type_to_spawn = possible_critters[rand() % possible_critters.size()];
                found_type = true;
            }
        }

        if (!found_type) continue; // Skip if no valid critter types for this biome

        const auto& data = g_CritterData.at(type_to_spawn);
        bool is_aquatic = std::find(data.tags.begin(), data.tags.end(), CritterTag::AQUATIC) != data.tags.end();

        // Find a valid spawn location
        int spawn_x = -1, spawn_y = -1;
        int attempts = 50;
        while (attempts > 0) {
            int try_x = rand() % WORLD_WIDTH;
            int try_y = rand() % WORLD_HEIGHT;

            if (is_aquatic) {
                if (Z_LEVELS[BIOSPHERE_Z_LEVEL][try_y][try_x].type == TileType::WATER) {
                    spawn_x = try_x;
                    spawn_y = try_y;
                    break;
                }
            }
            else {
                if (isCritterWalkable(try_x, try_y, BIOSPHERE_Z_LEVEL)) {
                    spawn_x = try_x;
                    spawn_y = try_y;
                    break;
                }
            }
            attempts--;
        }

        if (spawn_x != -1) {
            Critter new_critter;
            new_critter.type = type_to_spawn;
            new_critter.x = spawn_x;
            new_critter.y = spawn_y;
            new_critter.z = BIOSPHERE_Z_LEVEL;
            new_critter.wanderCooldown = data.wander_speed + (rand() % 50); // Random initial cooldown
            g_critters.push_back(new_critter);
        }
    }
}

void renderTextInspectable_internal(HDC hdc, const std::wstring& text, int x, int y, COLORREF color, const wchar_t* s_text, const wchar_t* s_x, const wchar_t* s_y, const wchar_t* s_color, const wchar_t* s_caller, const std::wstring& extra_info) {
    SetTextColor(hdc, color);
    TextOut(hdc, x, y, text.c_str(), static_cast<int>(text.length()));
    SIZE size;
    GetTextExtentPoint32(hdc, text.c_str(), static_cast<int>(text.length()), &size);
    RECT r = { x, y, x + size.cx, y + size.cy };

    std::wstringstream ss;
    ss << L"Source Function: " << s_caller << L"\n"
        << L"------------------------------------\n"
        << L"renderTextInspectable(\n"
        << L"  text:  " << s_text << L",\n"
        << L"  x:     " << s_x << L"  => " << x << L",\n"
        << L"  y:     " << s_y << L"  => " << y << L",\n"
        << L"  color: " << s_color << L"  => RGB(" << (int)GetRValue(color) << L"," << (int)GetGValue(color) << L"," << (int)GetBValue(color) << L")\n"
        << L");";
    if (!extra_info.empty()) {
        ss << L"\n\n---\n" << extra_info;
    }
    g_inspectorElements.push_back({ r, ss.str() });
}

void renderCenteredTextInspectable_internal(HDC hdc, const std::wstring& text, int y, int windowWidth, COLORREF color, const wchar_t* s_text, const wchar_t* s_y, const wchar_t* s_windowWidth, const wchar_t* s_color, const wchar_t* s_caller, const std::wstring& extra_info) {
    SIZE size;
    GetTextExtentPoint32(hdc, text.c_str(), static_cast<int>(text.length()), &size);
    int x = (windowWidth - size.cx) / 2;

    SetTextColor(hdc, color);
    TextOut(hdc, x, y, text.c_str(), static_cast<int>(text.length()));
    RECT r = { x, y, x + size.cx, y + size.cy };

    std::wstringstream ss;
    ss << L"Source Function: " << s_caller << L"\n"
        << L"------------------------------------\n"
        << L"renderCenteredTextInspectable(\n"
        << L"  text:        " << s_text << L",\n"
        << L"  y:           " << s_y << L"  => " << y << L",\n"
        << L"  windowWidth: " << s_windowWidth << L"  => " << windowWidth << L",\n"
        << L"  color:       " << s_color << L"  => RGB(" << (int)GetRValue(color) << L"," << (int)GetGValue(color) << L"," << (int)GetBValue(color) << L")\n"
        << L");\n\n"
        << L"Calculated x: (" << s_windowWidth << L" - " << size.cx << L") / 2 = " << x;
    if (!extra_info.empty()) {
        ss << L"\n\n---\n" << extra_info;
    }
    g_inspectorElements.push_back({ r, ss.str() });
}

void renderBoxInspectable_internal(HDC hdc, RECT rect, COLORREF color, const wchar_t* s_rect, const wchar_t* s_color, const wchar_t* s_caller, const std::wstring& extra_info) {
    HPEN hPen = CreatePen(PS_SOLID, 1, color);
    HGDIOBJ hOldPen = SelectObject(hdc, hPen);
    SelectObject(hdc, GetStockObject(NULL_BRUSH));
    Rectangle(hdc, rect.left, rect.top, rect.right, rect.bottom);
    SelectObject(hdc, hOldPen);
    DeleteObject(hPen);

    std::wstringstream ss;
    ss << L"Source Function: " << s_caller << L"\n"
        << L"------------------------------------\n"
        << L"renderBoxInspectable(\n"
        << L"  rect:  " << s_rect << L"\n"
        << L"    left: " << rect.left << L", top: " << rect.top << L", right: " << rect.right << L", bottom: " << rect.bottom << L"\n"
        << L"  color: " << s_color << L"  => RGB(" << (int)GetRValue(color) << L"," << (int)GetGValue(color) << L"," << (int)GetBValue(color) << L")\n"
        << L");";
    if (!extra_info.empty()) {
        ss << L"\n\n---\n" << extra_info;
    }
    g_inspectorElements.push_back({ rect, ss.str() });
}

void renderDebugCritterList(HDC hdc, int width, int height) {
    if (!isDebugMode || !isDebugCritterListVisible) return;

    // 1. Count and group critters on the current Z-Level
    std::map<CritterType, int> critterCounts;
    for (const auto& critter : g_critters) {
        if (critter.z == currentZ) {
            critterCounts[critter.type]++;
        }
    }

    // 2. Create a sorted list for consistent display
    std::vector<std::pair<CritterType, int>> sorted_critters;
    for (const auto& pair : critterCounts) {
        sorted_critters.push_back(pair);
    }
    std::sort(sorted_critters.begin(), sorted_critters.end(), [](const auto& a, const auto& b) {
        return g_CritterData.at(a.first).name < g_CritterData.at(b.first).name;
        });

    // 3. Define panel position and start drawing
    int panelX = 20;
    // Position it below the resource readout, assuming it takes up about 250px height
    int panelY = 80 + 250;
    int lineHeight = 16;

    // Draw the header
    RENDER_TEXT_INSPECTABLE(hdc, L"Critters on this Level [F5]", panelX, panelY, RGB(255, 100, 100));
    panelY += lineHeight + 5;

    if (sorted_critters.empty()) {
        RENDER_TEXT_INSPECTABLE(hdc, L"(None)", panelX + 5, panelY, RGB(128, 128, 128));
        return;
    }

    // 4. Draw each critter entry
    for (const auto& critter_pair : sorted_critters) {
        const CritterData& data = g_CritterData.at(critter_pair.first);
        int count = critter_pair.second;

        // Format the string for alignment: [Name] (Count)
        wchar_t buffer[100];
        swprintf_s(buffer, 100, L"%-25.25s (%d)", data.name.c_str(), count);

        RENDER_TEXT_INSPECTABLE(hdc, buffer, panelX + 5, panelY, RGB(220, 220, 220));

        panelY += lineHeight;
        // Stop if we would draw off the bottom of the screen
        if (panelY > height - 40) break;
    }
}

void initResearchData() {
    g_allResearch.clear();
    g_completedResearch.clear();

    // --- Neolithic Era ---
    g_allResearch[L"AGR"] = { L"AGR", L"Agriculture", L"Unlock the ability to create farm plots to grow crops.", 1500, ResearchEra::NEOLITHIC, ResearchCategory::FARMING, {}, {L"Building: Farm Plot", L"Work: Sowing & Harvesting", L"Building: Growing Zone"} };
    g_allResearch[L"ANH"] = { L"ANH", L"Animal Husbandry", L"Learn to domesticate and breed animals for food, leather, and labor.", 1800, ResearchEra::NEOLITHIC, ResearchCategory::ANIMALS, {L"AGR"}, {L"Building: Animal Pen", L"Work: Taming"} };
    g_allResearch[L"BSC"] = { L"BSC", L"Construction", L"Build walls and structures from wood.", 1000, ResearchEra::NEOLITHIC, ResearchCategory::CONSTRUCTION, {}, {L"Building: Wall"} };
    g_allResearch[L"BWW"] = { L"BWW", L"Woodworking", L"Develop techniques to craft simple furniture and items from wood.", 1000, ResearchEra::NEOLITHIC, ResearchCategory::CRAFTING, {L"BSC"}, {L"Building: Carpentry Workbench", L"Building: Chair", L"Building: Table", L"Building: Torch"} };
    g_allResearch[L"STC"] = { L"STC", L"Stonecutting", L"Process rough stone chunks into uniform blocks, which are required for advanced stone buildings.", 1200, ResearchEra::NEOLITHIC, ResearchCategory::CRAFTING, {L"BSC"}, {L"Building: Stonecutting Table", L"Recipe: Stone Blocks"} };
    g_allResearch[L"TXS"] = { L"TXS", L"Textile Spinning", L"Learn to spin raw fibers from plants or animals into thread, the basic component for weaving cloth.", 1800, ResearchEra::NEOLITHIC, ResearchCategory::APPAREL, {L"AGR"}, {L"Building: Spinning Wheel", L"Recipe: Thread", L"Recipe: Basic Apparel"} };
    g_allResearch[L"BRW"] = { L"BRW", L"Beer Brewing", L"Discover the process of fermentation to turn grains and fruits into a pleasant alcoholic beverage.", 1500, ResearchEra::NEOLITHIC, ResearchCategory::COOKING, {L"AGR"}, {L"Building: Brewery", L"Recipe: Beer"} };
    g_allResearch[L"MAS"] = { L"MAS", L"Masonry", L"Learn to shape stone into blocks and construct sturdy stone walls and structures.", 1200, ResearchEra::NEOLITHIC, ResearchCategory::CONSTRUCTION, {L"STC"}, {L"Building: Stone Wall"} };
    g_allResearch[L"ARC"] = { L"ARC", L"Archery", L"Train skilled hunters and fletchers to craft bows and arrows for ranged combat and hunting.", 1200, ResearchEra::NEOLITHIC, ResearchCategory::COMBAT, {L"BWW", L"HUNT"}, {L"Recipe: Bow", L"Recipe: Arrow"}};
    g_allResearch[L"BZWRK"] = { L"BZWRK", L"Bronze Working", L"Master the art of combining copper and tin to forge bronze, a stronger metal for tools and weapons.", 2000, ResearchEra::NEOLITHIC, ResearchCategory::CRAFTING, {L"MIN"}, {L"Recipe: Bronze Ingot", L"Building: Bronze Anvil (future)"} };
    g_allResearch[L"CAL"] = { L"CAL", L"Calendar", L"Organize time with a calendar to track seasons, predict harvests, and plan events.", 1000, ResearchEra::NEOLITHIC, ResearchCategory::SCIENCE, {}, {} };
    g_allResearch[L"MIN"] = { L"MIN", L"Mining", L"Unlock basic techniques for systematically excavating ore veins and extracting raw minerals from rock.", 1000, ResearchEra::NEOLITHIC, ResearchCategory::CRAFTING, {L"STC"}, {L"Work: Mine Ores", L"Building: Mine Shaft", L"Recipe: Pickaxe"} };
    g_allResearch[L"POT"] = { L"POT", L"Pottery", L"Craft basic clay vessels for storage, cooking, and decorative purposes.", 800, ResearchEra::NEOLITHIC, ResearchCategory::CRAFTING, {L"AGR"}, {L"Recipe: Clay Pot", L"Building: Potter's Wheel (future)"} };
    g_allResearch[L"SAI"] = { L"SAI", L"Sailing", L"Develop rudimentary sailing techniques, allowing for exploration and trade across water bodies.", 1500, ResearchEra::NEOLITHIC, ResearchCategory::TRANSPORT, {L"BWW"}, {L"Recipe: Raft"}};
    g_allResearch[L"TRA"] = { L"TRA", L"Trapping", L"Learn to set traps for small game, providing a reliable source of food and fur.", 900, ResearchEra::NEOLITHIC, ResearchCategory::COMBAT, {L"HUNT"}, {L"Recipe: Simple Trap", L"Recipe: Spike Trap"} };
    g_allResearch[L"IRR"] = { L"IRR", L"Irrigation", L"Develop systems to transport water to crops, improving agricultural yields and resilience to drought.", 1700, ResearchEra::NEOLITHIC, ResearchCategory::FARMING, {L"AGR"}, {L"Building: Irrigation Ditch (future)"} };
    g_allResearch[L"ASTRO"] = { L"ASTRO", L"Astrology", L"Observe celestial bodies to predict natural phenomena and understand their influence on life.", 1100, ResearchEra::NEOLITHIC, ResearchCategory::SCIENCE, {L"CAL"}, {} };
    g_allResearch[L"FIS"] = { L"FIS", L"Fishing", L"Develop primitive tools and techniques to catch fish, providing a new food source.", 800, ResearchEra::NEOLITHIC, ResearchCategory::COOKING, {}, {L"Building: Fishing Zone (future)", L"Recipe: Fish (future)"} }; // NEW
    g_allResearch[L"HUNT"] = { L"HUNT", L"Hunting", L"Master the techniques of tracking, ambushing, and eliminating wild animals for resources and security.", 9500, ResearchEra::NEOLITHIC, ResearchCategory::COMBAT, {}, {L"Work: Hunting"} };
    g_allResearch[L"FIRE_TAM"] = { L"FIRE_TAM", L"Fire Taming", L"Learn to safely maintain and utilize fire for warmth, cooking, and crafting, essential for survival.", 1500, ResearchEra::NEOLITHIC, ResearchCategory::COOKING, {}, {L"Building: Campfire"} };

    // --- Classical Era ---
    g_allResearch[L"CLS_PHL"] = { L"CLS_PHL", L"Philosophy", L"Ponder the big questions, establishing formal methods of thought and debate.", 2500, ResearchEra::CLASSICAL, ResearchCategory::SCIENCE, {}, {L"Recreation: Meditation Spot"} };
    g_allResearch[L"WRT"] = { L"WRT", L"Writing", L"Develop a system of written symbols to record knowledge, improving research and social organization.", 2000, ResearchEra::CLASSICAL, ResearchCategory::SCIENCE, {L"CLS_PHL"}, {L"Passive: +10% Research Speed", L"Building: Bookshelf (future)"} };
    g_allResearch[L"CLS_SMT"] = { L"CLS_SMT", L"Smithing", L"Learn to work with metals to craft durable tools, weapons, and armor.", 3000, ResearchEra::CLASSICAL, ResearchCategory::CRAFTING, {L"MAS", L"BZWRK"}, {L"Building: Smithy", L"Recipe: Metal Tools & Weapons"} };
    g_allResearch[L"CLS_WHL"] = { L"CLS_WHL", L"The Wheel", L"Develop the wheel and axle for transportation and construction, improving hauling efficiency.", 2200, ResearchEra::CLASSICAL, ResearchCategory::TRANSPORT, {L"BWW"}, {L"Passive: +10% Pawn move speed", L"Recipe: Wheel", L"Unlock: Basic Land Vehicles (future)", L"Unlock: Windmills"} };
    g_allResearch[L"CUR"] = { L"CUR", L"Currency", L"Establish a standardized system of trade, allowing for more efficient exchanges and economic growth.", 2500, ResearchEra::CLASSICAL, ResearchCategory::TECH, {L"WRT"}, {L"Unlocks advanced trade options (future)", L"Building: Coining Mill", L"Recipe: Coin"} };
    g_allResearch[L"DRA"] = { L"DRA", L"Drama and Poetry", L"Cultivate artistic expression through dramatic performances and poetic verse, improving colony morale.", 1800, ResearchEra::CLASSICAL, ResearchCategory::RECREATION, {L"WRT"}, {L"Building: Theatre Stage"} };
    g_allResearch[L"ENG"] = { L"ENG", L"Engineering", L"Apply scientific principles to design and build complex structures, including bridges and defensive works.", 3000, ResearchEra::CLASSICAL, ResearchCategory::CONSTRUCTION, {L"CLS_SMT", L"MAT"}, {L"Building: Stone Bridge (future)", L"Building: Siege Wall (future)"} };
    g_allResearch[L"HRB"] = { L"HRB", L"Horseback Riding", L"Domesticate and train horses for transportation, warfare, and labor.", 2000, ResearchEra::CLASSICAL, ResearchCategory::ANIMALS, {L"ANH"}, {L"Work: Horse Riding (future)", L"Building: Stables (future)"} };
    g_allResearch[L"IRW"] = { L"IRW", L"Iron Working", L"Discover how to smelt iron from ore and forge it into strong tools and weapons.", 3500, ResearchEra::CLASSICAL, ResearchCategory::CRAFTING, {L"MIN", L"CLS_SMT"}, {L"Recipe: Iron Ingot", L"Building: Bloomery (future)"} };
    g_allResearch[L"MAT"] = { L"MAT", L"Mathematics", L"Develop advanced mathematical concepts for improved construction, engineering, and astronomy.", 2800, ResearchEra::CLASSICAL, ResearchCategory::SCIENCE, {L"CLS_PHL"}, {L"Passive: +10% Construction Speed", L"Passive: +10% Research Speed"} };
    g_allResearch[L"OPT"] = { L"OPT", L"Optics", L"Craft basic lenses and mirrors, leading to advancements in scientific observation and ranged weaponry.", 2200, ResearchEra::CLASSICAL, ResearchCategory::TECH, {L"CLS_SMT"}, {L"Building: Simple Telescope (future)", L"Recipe: Spyglass (future)", L"Recipe: Lens", L"Recipe: Glasses"} };
    g_allResearch[L"WNDM"] = { L"WNDM", L"Windmills", L"Harness wind power for grinding grain and pumping water, improving efficiency in production.", 2500, ResearchEra::CLASSICAL, ResearchCategory::POWER, {L"CLS_WHL", L"ENG"}, {L"Building: Windmill"} };
    g_allResearch[L"PAP"] = { L"PAP", L"Paper Making", L"Process plant fibers into paper, a superior medium for writing and record-keeping.", 2000, ResearchEra::CLASSICAL, ResearchCategory::SCIENCE, {L"AGR", L"BWW"}, {L"Recipe: Paper"} };
    g_allResearch[L"GLS"] = { L"GLS", L"Glassblowing", L"Learn to shape molten glass into functional and decorative items, from windows to lenses.", 5000, ResearchEra::CLASSICAL, ResearchCategory::CRAFTING, {L"STC"}, {L"Recipe: Glass", L"Recipe: Glass Pane"} };

    // --- Medieval Era ---
    g_allResearch[L"MED_MSC"] = { L"MED_MSC", L"Metal Casting", L"Master the techniques for casting molten metal, allowing for the creation of complex and sturdy metal items.", 4500, ResearchEra::MEDIEVAL, ResearchCategory::CRAFTING, {L"CLS_SMT", L"IRW"}, {L"Building: Blast Furnace", L"Unlocks advanced metal recipes"} };
    g_allResearch[L"MED_CRB"] = { L"MED_CRB", L"Crossbows", L"Engineer powerful crossbows that are easier to use than conventional bows and have excellent armor penetration.", 3500, ResearchEra::MEDIEVAL, ResearchCategory::COMBAT, {L"BWW", L"CLS_SMT"}, {L"Recipe: Crossbow"} };
    g_allResearch[L"MED_GLD"] = { L"MED_GLD", L"Guilds", L"Formalize craftsmenship into guilds, improving the quality and speed of production for specialized goods.", 4000, ResearchEra::MEDIEVAL, ResearchCategory::CRAFTING, {L"WRT", L"CUR"}, {L"Passive: +10% Crafting Speed"} };
    g_allResearch[L"EDU"] = { L"EDU", L"Education", L"Establish formal schools and universities to greatly enhance learning and scientific discovery.", 7000, ResearchEra::MEDIEVAL, ResearchCategory::SCIENCE, {L"WRT", L"MAT"}, {L"Building: Advanced Research Bench", L"Building: Blackboard", L"Building: School Chair", L"Building: School Desk"} };
    g_allResearch[L"CHV"] = { L"CHV", L"Chivalry", L"Develop a code of conduct and advanced combat techniques, improving military discipline and morale.", 3800, ResearchEra::MEDIEVAL, ResearchCategory::COMBAT, {L"CLS_SMT", L"HRB"}, {L"Recipe: Knight Armor", L"Recipe: Knight Weapons"} };
    g_allResearch[L"CIV"] = { L"CIV", L"Civil Service", L"Establish a bureaucracy and organized governance for greater efficiency in managing your colony.", 4200, ResearchEra::MEDIEVAL, ResearchCategory::SCIENCE, {L"EDU"}, {L"Passive: +10% Work Speed"} };
    g_allResearch[L"CMP"] = { L"CMP", L"Compass", L"Invent the magnetic compass, greatly aiding navigation and long-distance travel.", 3200, ResearchEra::MEDIEVAL, ResearchCategory::TRANSPORT, {L"OPT"}, {L"Recipe: Compass"} };
    g_allResearch[L"MCH"] = { L"MCH", L"Machinery", L"Develop complex mechanical devices for improved resource extraction and production.", 5000, ResearchEra::MEDIEVAL, ResearchCategory::CRAFTING, {L"MED_MSC", L"ENG"}, {L"Building: Gear Press (future)", L"Recipe: Gears (future)"} };
    g_allResearch[L"PHY"] = { L"PHY", L"Physics", L"Explore the fundamental laws of nature, laying the groundwork for future technological breakthroughs.", 4800, ResearchEra::MEDIEVAL, ResearchCategory::SCIENCE, {L"MAT"}, {L"Passive: +15% Research Speed"} };
    g_allResearch[L"STL"] = { L"STL", L"Steel", L"Discover the process of making steel, a much stronger and more durable metal than iron or bronze.", 5500, ResearchEra::MEDIEVAL, ResearchCategory::CRAFTING, {L"MED_MSC"}, {L"Recipe: Steel Ingot"} };
    g_allResearch[L"THL"] = { L"THL", L"Theology", L"Formalize spiritual beliefs, providing comfort and social cohesion for your colonists.", 3000, ResearchEra::MEDIEVAL, ResearchCategory::RECREATION, {L"CLS_PHL"}, {L"Building: Shrine (future)"} };
    g_allResearch[L"FOR"] = { L"FOR", L"Fortification", L"Design and construct formidable defensive structures to withstand sieges.", 5200, ResearchEra::MEDIEVAL, ResearchCategory::SECURITY, {L"MAS", L"ENG"}, {L"Building: Watchtower (future)", L"Building: Drawbridge (future)"} };
    g_allResearch[L"MIL_ENG"] = { L"MIL_ENG", L"Military Engineering", L"Combine engineering prowess with military tactics to build siege engines and defensive works in the field.", 5800, ResearchEra::MEDIEVAL, ResearchCategory::SECURITY, {L"ENG", L"CHV"}, {L"Recipe: Catapult (future)", L"Recipe: Ballista (future)"} };
    g_allResearch[L"MIL_TAC"] = { L"MIL_TAC", L"Military Tactics", L"Study strategic and tactical principles to improve the effectiveness of your combat units.", 4500, ResearchEra::MEDIEVAL, ResearchCategory::COMBAT, {L"CHV", L"WRT"}, {L"Passive: +5% Combat Effectiveness"} };
    g_allResearch[L"TOR"] = { L"TOR", L"Torture", L"Develop methods of extracting information or influencing behavior through coercion.", 3000, ResearchEra::MEDIEVAL, ResearchCategory::SECURITY, {L"CIV"}, {} };
    g_allResearch[L"STI"] = { L"STI", L"Stirrups", L"Equip cavalry with stirrups for better stability and effectiveness in mounted combat.", 3500, ResearchEra::MEDIEVAL, ResearchCategory::ANIMALS, {L"HRB"}, {} };
    g_allResearch[L"NOB_APP"] = { L"NOB_APP", L"Noble Apparel", L"Craft fine clothing and adornments befitting royalty and the aristocracy, signifying status and wealth.", 6000, ResearchEra::MEDIEVAL, ResearchCategory::APPAREL, {L"TXS", L"MED_GLD"}, {L"Recipe: Noble Dress", L"Recipe: Noble Tunic"} };
    g_allResearch[L"ALCH"] = { L"ALCH", L"Alchemy", L"Experiment with chemical compounds and processes, seeking to transmute materials and unlock potent elixirs.", 7000, ResearchEra::RENAISSANCE, ResearchCategory::SCIENCE, {L"PHY"}, {L"Building: Alchemy Lab (future)"} };

    // --- Renaissance Era ---
    g_allResearch[L"REN_PRP"] = { L"REN_PRP", L"Printing Press", L"Create a machine that allows for the mass production of written works, dramatically accelerating research.", 6000, ResearchEra::RENAISSANCE, ResearchCategory::SCIENCE, {L"WRT", L"MCH"}, {L"Passive: +25% Research Speed", L"Building: Printing Press", L"Building: Typewriter"} };
    g_allResearch[L"REN_GUN"] = { L"REN_GUN", L"Gunpowder", L"Experiment with black powder to create the first primitive firearms.", 7500, ResearchEra::RENAISSANCE, ResearchCategory::COMBAT, {L"MED_MSC", L"CHEM"}, {L"Recipe: Musket", L"Recipe: Gunpowder"} };
    g_allResearch[L"REN_BNK"] = { L"REN_BNK", L"Banking", L"Develop complex economic systems and currency, enabling more efficient trade with off-world factions.", 6500, ResearchEra::RENAISSANCE, ResearchCategory::TECH, {L"MED_GLD", L"ECO"}, {L"Building: Trade Beacon (future)"} };
    g_allResearch[L"ACS"] = { L"ACS", L"Acoustics", L"Study the science of sound, leading to improved communication and musical instruments.", 6000, ResearchEra::RENAISSANCE, ResearchCategory::SCIENCE, {L"PHY"}, {L"Building: Acoustic Wall"} };
    g_allResearch[L"ARC_REN"] = { L"ARC_REN", L"Architecture", L"Master advanced building design and structural integrity for grander and more efficient constructions.", 7000, ResearchEra::RENAISSANCE, ResearchCategory::CONSTRUCTION, {L"ENG"}, {L"Building: Column"} };
    g_allResearch[L"AST"] = { L"AST", L"Astronomy", L"Chart the stars and planets with improved observation tools, enhancing navigation and understanding of the cosmos.", 6800, ResearchEra::RENAISSANCE, ResearchCategory::SCIENCE, {L"OPT", L"MAT"}, {L"Building: Telescope"} };
    g_allResearch[L"CHEM"] = { L"CHEM", L"Chemistry", L"Explore the fundamental composition of matter and chemical reactions, enabling new industrial processes.", 7200, ResearchEra::RENAISSANCE, ResearchCategory::SCIENCE, {L"PHY"}, {L"Building: Drug Lab", L"Recipe: Various Drugs", L"Recipe: Chemical Equipment"} };
    g_allResearch[L"ECO"] = { L"ECO", L"Economics", L"Formulate theories of production, distribution, and consumption of goods, improving trade and resource management.", 6500, ResearchEra::RENAISSANCE, ResearchCategory::SCIENCE, {L"MED_GLD"}, {} };
    g_allResearch[L"REN_MET"] = { L"REN_MET", L"Metallurgy", L"Advance the science of metals, leading to purer alloys and more sophisticated forging techniques.", 8000, ResearchEra::RENAISSANCE, ResearchCategory::CRAFTING, {L"MED_MSC", L"CHEM"}, {L"Recipe: Alloys"} };
    g_allResearch[L"NAV"] = { L"NAV", L"Navigation", L"Develop advanced navigational instruments and techniques for accurate long-distance travel across oceans and plains.", 7500, ResearchEra::RENAISSANCE, ResearchCategory::TRANSPORT, {L"CMP", L"AST"}, {L"Recipe: Boat", L"Building: Lighthouse"} };
    g_allResearch[L"SQR_RIG"] = { L"SQR_RIG", L"Square Rigging", L"Improve ship design with square rigs for faster and more efficient long-distance ocean travel.", 7800, ResearchEra::RENAISSANCE, ResearchCategory::TRANSPORT, {L"SAI", L"NAV"}, {} };
    g_allResearch[L"SIE_TAC"] = { L"SIE_TAC", L"Siege Tactics", L"Master the art of assaulting and defending fortified positions with specialized military doctrines.", 8200, ResearchEra::RENAISSANCE, ResearchCategory::COMBAT, {L"FOR", L"MIL_ENG"}, {} };
    g_allResearch[L"CAR"] = { L"CAR", L"Cartography", L"Create accurate maps of explored territories, enhancing navigation and strategic planning.", 7000, ResearchEra::RENAISSANCE, ResearchCategory::SCIENCE, {L"AST", L"WRT"}, {} };
    g_allResearch[L"MAS_PRO"] = { L"MAS_PRO", L"Mass Production", L"Implement early forms of standardized production to create items more quickly and efficiently.", 7500, ResearchEra::RENAISSANCE, ResearchCategory::CRAFTING, {L"REN_MET", L"MCH"}, {L"Passive: +10% Production Speed"} };
    g_allResearch[L"LAC"] = { L"LAC", L"Lacework", L"Develop intricate techniques for creating delicate, patterned fabrics used in high-fashion apparel.", 5500, ResearchEra::RENAISSANCE, ResearchCategory::APPAREL, {L"TXS"}, {L"Recipe: Lace Trim"} };
    g_allResearch[L"OILP"] = { L"OILP", L"Oil Painting", L"Master the use of oil paints to create rich, vibrant, and lasting works of art.", 5800, ResearchEra::RENAISSANCE, ResearchCategory::RECREATION, {L"PAP", L"CHEM"}, {L"Building: Easel (future)"} };
    g_allResearch[L"PERSP"] = { L"PERSP", L"Perspectives", L"Understand geometric perspective to create more realistic and immersive artistic representations.", 5200, ResearchEra::RENAISSANCE, ResearchCategory::SCIENCE, {L"MAT", L"WRT"}, {} };
    

    
    

    // --- Industrial Era ---
    g_allResearch[L"IND_STM"] = { L"IND_STM", L"Steam Power", L"Harness the power of steam to drive machinery and generators, providing a powerful new energy source.", 9000, ResearchEra::INDUSTRIAL, ResearchCategory::POWER, {L"REN_MET", L"MCH"}, {L"Building: Steam Generator"} };
    g_allResearch[L"IND_MAN"] = { L"IND_MAN", L"Manufacturing", L"Establish principles of industrial manufacturing to produce complex items in large quantities.", 10000, ResearchEra::INDUSTRIAL, ResearchCategory::CRAFTING, {L"EDU", L"MCH", L"ECO"}, {L"Building: Machining Table"} };
    g_allResearch[L"IND_ELC"] = { L"IND_ELC", L"Electricity", L"Harness electricity to create light, heat, and power for advanced machinery.", 12000, ResearchEra::INDUSTRIAL, ResearchCategory::POWER, {L"IND_MAN", L"SCI_THE"}, {L"Building: Power Generator"} };
    g_allResearch[L"IND_RFL"] = { L"IND_RFL", L"Rifling", L"Develop spiral grooves in gun barrels to dramatically improve the accuracy and range of firearms.", 9500, ResearchEra::INDUSTRIAL, ResearchCategory::COMBAT, {L"REN_GUN", L"IND_MAN"}, {L"Recipe: Rifles"} };
    g_allResearch[L"IND_BIO"] = { L"IND_BIO", L"Biology", L"Gain a deep understanding of microbiology and genetics, paving the way for modern medicine and hydroponics.", 11000, ResearchEra::INDUSTRIAL, ResearchCategory::MEDICINE, {L"EDU", L"CHEM"}, {} };
    g_allResearch[L"IND_ARC"] = { L"IND_ARC", L"Archaeology", L"Unearth ancient artifacts and ruins to gain historical knowledge and recover lost technologies.", 8500, ResearchEra::INDUSTRIAL, ResearchCategory::SCIENCE, {L"AST"}, {} };
    g_allResearch[L"DYN"] = { L"DYN", L"Dynamite", L"Invent powerful explosives for efficient mining, demolition, and combat engineering.", 10500, ResearchEra::INDUSTRIAL, ResearchCategory::TECH, {L"CHEM", L"MIN"}, {L"Recipe: Dynamite"} };
    g_allResearch[L"FRT"] = { L"FRT", L"Fertilizer", L"Develop chemical compounds to enrich soil, significantly boosting crop yields and farm efficiency.", 9800, ResearchEra::INDUSTRIAL, ResearchCategory::FARMING, {L"CHEM", L"AGR"}, {L"Recipe: Synthetic Fertilizer"} };
    g_allResearch[L"IND_IND"] = { L"IND_IND", L"Industrialization", L"Mass production techniques and assembly lines revolutionize manufacturing, vastly increasing output.", 11500, ResearchEra::INDUSTRIAL, ResearchCategory::CRAFTING, {L"IND_MAN", L"IND_STM"}, {L"Passive: +20% Production Speed", L"Building: Assembly Line"} };
    g_allResearch[L"MLS"] = { L"MLS", L"Military Science", L"Apply scientific principles to military tactics and logistics, leading to more effective armies.", 10800, ResearchEra::INDUSTRIAL, ResearchCategory::COMBAT, {L"CHV", L"IND_RFL"}, {L"Recipe: Advanced Armor"} };
    g_allResearch[L"SCI_THE"] = { L"SCI_THE", L"Scientific Theory", L"Formalize the scientific method, enabling rapid advancements across all fields of study.", 10000, ResearchEra::INDUSTRIAL, ResearchCategory::SCIENCE, {L"PHY", L"EDU"}, {L"Passive: +30% Research Speed"} };
    g_allResearch[L"TGR"] = { L"TGR", L"Telegraph", L"Invent the telegraph, allowing for instant, long-distance communication and improved coordination.", 12500, ResearchEra::INDUSTRIAL, ResearchCategory::TECH, {L"IND_ELC"}, {L"Building: Telegraph Station (future)"} };
    g_allResearch[L"HLC"] = { L"HLC", L"Healthcare", L"Establish modern medical practices and facilities to improve colonist health and recovery.", 13000, ResearchEra::INDUSTRIAL, ResearchCategory::MEDICINE, {L"IND_BIO"}, {L"Building: Hospital Bed"} };
    g_allResearch[L"HYD"] = { L"HYD", L"Hydroponics", L"Grow crops efficiently in nutrient-rich water solutions, allowing farming in harsh environments.", 14000, ResearchEra::INDUSTRIAL, ResearchCategory::FARMING, {L"IND_BIO", L"FRT"}, {L"Building: Hydroponics Basin"} };
    g_allResearch[L"WIR"] = { L"WIR", L"Wiring", L"Develop insulated wires and switches to safely distribute electricity for lighting and simple machines.", 12800, ResearchEra::INDUSTRIAL, ResearchCategory::POWER, {L"IND_ELC"}, {L"Recipe: Wire", L"Recipe: Lightbulb", L"Building: Electric Lamp"} };
    g_allResearch[L"ADV_LGT"] = { L"ADV_LGT", L"Advanced Lighting", L"Design and implement more efficient and powerful lighting solutions for larger areas.", 13500, ResearchEra::INDUSTRIAL, ResearchCategory::LIGHTS, {L"WIR"}, {L"Building: Floodlight (future)"} };
    g_allResearch[L"SAN"] = { L"SAN", L"Sanitation", L"Implement advanced waste disposal and water treatment systems to prevent disease and improve hygiene.", 9000, ResearchEra::INDUSTRIAL, ResearchCategory::MEDICINE, {L"CHEM"}, {L"Building: Latrine (future)", L"Building: Water Filter (future)"} };
    g_allResearch[L"BALL"] = { L"BALL", L"Ballistics", L"Study projectile motion and weapon design for more accurate and powerful firearms.", 10500, ResearchEra::INDUSTRIAL, ResearchCategory::COMBAT, {L"IND_RFL", L"PHY"}, {L"Recipe: Improved Firearms"} };
    g_allResearch[L"PROSTH"] = { L"PROSTH", L"Prosthetics", L"Develop artificial limbs to replace lost body parts, restoring function to injured colonists.", 11800, ResearchEra::INDUSTRIAL, ResearchCategory::MEDICINE, {L"IND_BIO", L"IND_MAN"}, {L"Recipe: Simple Prosthetics"} };
    g_allResearch[L"PNM"] = { L"PNM", L"Pneumatics", L"Harness compressed air to power simple mechanisms, improving efficiency in tools and manufacturing.", 11000, ResearchEra::INDUSTRIAL, ResearchCategory::TECH, {L"MCH"}, {L"Building: Pneumatic Press (future)"} };


    // --- Modern Era ---
    g_allResearch[L"MOD_ELC"] = { L"MOD_ELC", L"Electronics", L"Create complex circuits and vacuum tubes, the foundation of all modern technology.", 15000, ResearchEra::MODERN, ResearchCategory::TECH, {L"IND_ELC", L"SCI_THE"}, {L"Building: Comms Console", L"Recipe: Electronic Components"} };
    g_allResearch[L"MOD_MEDP"] = { L"MOD_MEDP", L"Medicine Production", L"Mass produce medical supplies and pharmaceuticals, improving colony health and recovery rates.", 13000, ResearchEra::MODERN, ResearchCategory::MEDICINE, {L"IND_BIO", L"CHEM"}, {L"Recipe: Medicine"} };
    g_allResearch[L"TEL"] = { L"TEL", L"Television", L"Broadcast visual information, enabling mass communication and entertainment.", 17000, ResearchEra::MODERN, ResearchCategory::TECH, {L"MOD_ELC"}, {L"Building: Television (future)"} };
    g_allResearch[L"RAD"] = { L"RAD", L"Radio", L"Develop wireless communication, allowing for instant, long-range messaging.", 16500, ResearchEra::MODERN, ResearchCategory::TECH, {L"MOD_ELC", L"ACS"}, {L"Building: Radio"} };
    g_allResearch[L"RLR"] = { L"RLR", L"Railroad", L"Construct extensive rail networks for efficient, high-volume transportation of goods and people.", 19000, ResearchEra::MODERN, ResearchCategory::TRANSPORT, {L"IND_IND", L"STL"}, {L"Building: Rail Tracks (future)", L"Building: Train Station (future)"} };
    g_allResearch[L"REF"] = { L"REF", L"Refrigeration", L"Preserve food and other perishables for extended periods, reducing spoilage and waste.", 15500, ResearchEra::MODERN, ResearchCategory::COOKING, {L"IND_ELC"}, {L"Building: Freezer (future)"} };
    g_allResearch[L"REP"] = { L"REP", L"Replaceable Parts", L"Standardize manufacturing to produce interchangeable components, speeding up repairs and construction.", 17500, ResearchEra::MODERN, ResearchCategory::CRAFTING, {L"IND_IND"}, {L"Passive: +15% Repair Speed"} };
    g_allResearch[L"REFINE"] = { L"REFINE", L"Refining", L"Develop chemical processes for purifying raw materials like petroleum into useful fuels and byproducts.", 17000, ResearchEra::MODERN, ResearchCategory::CRAFTING, {L"MOD_CMB", L"CHEM"}, {L"Building: Refinery (future)", L"Recipe: Chemfuel"} };
    g_allResearch[L"MOD_PLA"] = { L"MOD_PLA", L"Plastics", L"Learn to refine chemical precursors into versatile polymers for use in countless applications.", 16000, ResearchEra::MODERN, ResearchCategory::CRAFTING, {L"MOD_CMB", L"CHEM"}, {L"Recipe: Plastic"} };
    g_allResearch[L"MOD_CMB"] = { L"MOD_CMB", L"Combustion", L"Invent the internal combustion engine, enabling vehicles and more efficient power generation.", 14000, ResearchEra::MODERN, ResearchCategory::POWER, {L"IND_STM", L"REN_MET"}, {L"Building: Chemfuel Generator"} };
    g_allResearch[L"FLI"] = { L"FLI", L"Flight", L"Achieve powered flight, opening up new possibilities for transportation and aerial reconnaissance.", 18000, ResearchEra::MODERN, ResearchCategory::TRANSPORT, {L"MOD_CMB", L"ENG"}, {L"Building: Airstrip (future)"} };
    g_allResearch[L"AUTO"] = { L"AUTO", L"Automobile", L"Develop wheeled vehicles powered by internal combustion engines for efficient personal and cargo transport.", 20000, ResearchEra::MODERN, ResearchCategory::TRANSPORT, {L"MOD_CMB", L"IND_IND", L"CLS_WHL"}, {L"Recipe: Automobile"}};
    g_allResearch[L"EV"] = { L"EV", L"Electric Vehicles", L"Harness electricity to power vehicles, offering a cleaner and quieter alternative to combustion engines.", 22000, ResearchEra::MODERN, ResearchCategory::TRANSPORT, {L"MOD_CMB", L"IND_ELC"}, {L"Recipe: Electric Car"} };

    // --- Atomic Era ---
    g_allResearch[L"ENC"] = { L"ENC", L"Encryption", L"Implement advanced algorithms to secure data and communications, vital for sensitive operations.", 14500, ResearchEra::ATOMIC, ResearchCategory::SECURITY, {L"MOD_ELC"}, {L"Passive: Enhanced Data Security (future)"} };
    g_allResearch[L"ATO_CMP"] = { L"ATO_CMP", L"Computers", L"Develop primitive computers based on transistor logic, allowing for complex calculations and automation.", 20000, ResearchEra::ATOMIC, ResearchCategory::TECH, {L"MOD_ELC", L"ENC"}, {L"Building: Hi-Tech Research Bench", L"Furniture: Computer", L"Furniture: Mouse", L"Furniture: Screen", L"Furniture: Keyboard"} };
    g_allResearch[L"ATO_RCK"] = { L"ATO_RCK", L"Rocketry", L"Develop powerful rockets capable of escaping the planet's gravitational pull.", 25000, ResearchEra::ATOMIC, ResearchCategory::SPACE, {L"MOD_CMB", L"PHY"}, {L"Building: Ship Engine Parts"} };
    g_allResearch[L"ATO_NUC"] = { L"ATO_NUC", L"Nuclear Fission", L"Split the atom to release immense amounts of energy, providing a powerful but dangerous new power source.", 30000, ResearchEra::ATOMIC, ResearchCategory::POWER, {L"ATO_CMP", L"ATOT"}, {L"Building: Nuclear Reactor (future)"} };
    g_allResearch[L"ATOT"] = { L"ATOT", L"Atomic Theory", L"Delve into the structure of atoms, leading to advancements in energy and materials science.", 22000, ResearchEra::ATOMIC, ResearchCategory::SCIENCE, {L"SCI_THE", L"CHEM"}, {} };
    g_allResearch[L"CBA"] = { L"CBA", L"Combined Arms", L"Integrate different military branches for synergistic combat strategies.", 21000, ResearchEra::ATOMIC, ResearchCategory::COMBAT, {L"MLS", L"FLI"}, {} };
    g_allResearch[L"ATO_ECO"] = { L"ATO_ECO", L"Ecology", L"Understand the interactions between organisms and their environment, enabling sustainable resource management.", 20500, ResearchEra::ATOMIC, ResearchCategory::FARMING, {L"IND_BIO", L"ECO"}, {} };
    g_allResearch[L"ATO_RAD"] = { L"ATO_RAD", L"Radar", L"Utilize radio waves to detect objects and measure their range and speed, crucial for defense and navigation.", 23000, ResearchEra::ATOMIC, ResearchCategory::TECH, {L"RAD"}, {} };
    g_allResearch[L"ADV_FLI"] = { L"ADV_FLI", L"Advanced Flight", L"Push the boundaries of atmospheric and sub-orbital flight, enabling faster and more efficient travel.", 26000, ResearchEra::ATOMIC, ResearchCategory::TRANSPORT, {L"FLI", L"ATO_RCK"}, {L"Recipe: Advanced Aircraft"} };
    g_allResearch[L"AT_BAL"] = { L"AT_BAL", L"Advanced Ballistics", L"Refine projectile physics for unparalleled accuracy and destructive power in weaponry.", 28000, ResearchEra::ATOMIC, ResearchCategory::COMBAT, {L"BALL", L"ATOT"}, {L"Recipe: High-Velocity Rounds"} };
    g_allResearch[L"SYN_MAT"] = { L"SYN_MAT", L"Synthetic Materials", L"Engineer new materials with superior properties by manipulating molecular structures.", 24000, ResearchEra::ATOMIC, ResearchCategory::CRAFTING, {L"MOD_PLA", L"ATOT"}, {L"Recipe: Synthetics"} };
    g_allResearch[L"ARCADE"] = { L"ARCADE", L"Arcades", L"Create interactive entertainment centers that provide amusement and recreation for colonists.", 21000, ResearchEra::ATOMIC, ResearchCategory::RECREATION, {L"ATO_CMP"}, {L"Building: Arcade Cabinet (future)"} }; // NEW
    g_allResearch[L"AER"] = { L"AER", L"Aerogels", L"Synthesize ultra-lightweight, highly insulative materials with diverse applications in construction and space travel.", 23000, ResearchEra::ATOMIC, ResearchCategory::CRAFTING, {L"SYN_MAT", L"CHEM"}, {L"Recipe: Aerogel Panels (future)", L"Passive: Improved Insulation (future)"} }; // NEW
    g_allResearch[L"MECH"] = { L"MECH", L"Mechs", L"Design and construct large, bipedal combat machines, providing powerful mobile firepower.", 27000, ResearchEra::ATOMIC, ResearchCategory::COMBAT, {L"CBA", L"IND_IND"}, {L"Building: Mech Hangar (future)", L"Recipe: Light Combat Mech (future)"} }; // NEW
    g_allResearch[L"MED_IMG"] = { L"MED_IMG", L"Medical Imaging", L"Develop advanced techniques for visualizing internal body structures, greatly improving diagnosis and treatment.", 24000, ResearchEra::ATOMIC, ResearchCategory::MEDICINE, {L"HLC", L"ATO_RAD"}, {L"Building: Medical Scanner (future)", L"Passive: Faster Diagnosis (future)"} }; // NEW
    g_allResearch[L"RDL"] = { L"RDL", L"Radioluminescence", L"Harness the emission of light from radioactive materials for long-lasting, self-powered illumination.", 26000, ResearchEra::ATOMIC, ResearchCategory::LIGHTS, {L"ATO_NUC"}, {L"Recipe: Radiocells (future)", L"Building: Radioluminescent Lights (future)"} }; // NEW
    g_allResearch[L"SPL"] = { L"SPL", L"Specialized Limbs", L"Engineer prosthetics with enhanced functionality beyond basic replacement, for specific tasks or combat.", 25000, ResearchEra::ATOMIC, ResearchCategory::MEDICINE, {L"PROSTH", L"SYN_MAT"}, {L"Recipe: Mining Arm (future)", L"Recipe: Combat Leg (future)"} }; // NEW
    g_allResearch[L"VDG"] = { L"VDG", L"Videogames", L"Create complex interactive digital experiences for recreation and cognitive training.", 22000, ResearchEra::ATOMIC, ResearchCategory::RECREATION, {L"ATO_CMP", L"ARCADE"}, {L"Recipe: Game Cartridge (future)", L"Recreation: Video Games (future)"} }; // NEW

    // --- Information Era ---
    g_allResearch[L"INF_ROB"] = { L"INF_ROB", L"Robotics", L"Create autonomous robots to perform simple tasks like cleaning and hauling.", 35000, ResearchEra::INFORMATION, ResearchCategory::CRAFTING, {L"ATO_CMP", L"MCH"}, {L"Recipe: Hauling & Cleaning Bots"} };
    g_allResearch[L"INF_SAT"] = { L"INF_SAT", L"Satellites", L"Launch satellites into orbit to provide global communications, weather prediction, and resource scanning.", 40000, ResearchEra::INFORMATION, ResearchCategory::SPACE, {L"ATO_RCK", L"TGR"}, {L"Unlocks Orbital Scanners & Trade Ships (future)"} };
    g_allResearch[L"INF_FAB"] = { L"INF_FAB", L"Advanced Fabrication", L"Construct highly advanced fabrication benches capable of creating complex components and spacer-tech items.", 50000, ResearchEra::INFORMATION, ResearchCategory::CRAFTING, {L"INF_ROB", L"REP"}, {L"Building: Fabrication Bench"} };
    g_allResearch[L"GLO"] = { L"GLO", L"Globalization", L"Establish interstellar trade networks and diplomatic relations across vast distances.", 42000, ResearchEra::INFORMATION, ResearchCategory::HOSPITALITY, {L"CUR", L"INF_SAT"}, {L"Unlocks Faction Trading (future)"} };
    g_allResearch[L"LAS"] = { L"LAS", L"Lasers", L"Harness coherent light for cutting, communication, and powerful directed-energy weapons.", 45000, ResearchEra::INFORMATION, ResearchCategory::COMBAT, {L"OPT", L"PHY"}, {L"Recipe: Laser Rifle"} };
    g_allResearch[L"MOB_TAC"] = { L"MOB_TAC", L"Mobile Tactics", L"Develop rapid deployment and maneuver warfare strategies for highly agile combat units.", 37000, ResearchEra::INFORMATION, ResearchCategory::COMBAT, {L"CBA"}, {} };
    g_allResearch[L"NUF"] = { L"NUF", L"Nuclear Fusion", L"Unlock the power of the stars by fusing atomic nuclei, providing limitless clean energy.", 60000, ResearchEra::INFORMATION, ResearchCategory::POWER, {L"ATO_NUC", L"PAR_PHY"}, {L"Building: Fusion Reactor (future)"} };
    g_allResearch[L"PAR_PHY"] = { L"PAR_PHY", L"Particle Physics", L"Investigate subatomic particles and fundamental forces, pushing the boundaries of scientific knowledge.", 48000, ResearchEra::INFORMATION, ResearchCategory::SCIENCE, {L"ATOT"}, {L"Passive: +40% Research Speed"} };
    g_allResearch[L"AIT"] = { L"AIT", L"Artificial Intelligence", L"Create advanced sentient AI, revolutionizing automation and decision-making.", 55000, ResearchEra::INFORMATION, ResearchCategory::TECH, {L"INF_ROB", L"ATO_CMP"}, {L"Building: AI Core (future)"} };
    g_allResearch[L"SDE"] = { L"SDE", L"Smart Devices", L"Integrate miniature computers into everyday objects, enhancing convenience and control.", 36000, ResearchEra::INFORMATION, ResearchCategory::TECH, {L"ATO_CMP", L"MOD_ELC"}, {L"Recipe: Comm Device", L"Recipe: Smartphone", L"Recipe: Tablet", L"Recipe: Smartwatch"} };
    g_allResearch[L"TCM"] = { L"TCM", L"Telecommunication", L"Establish global communication networks via satellite and fiber optics.", 40000, ResearchEra::INFORMATION, ResearchCategory::TECH, {L"TGR", L"INF_SAT"}, {L"Building: Global Comm Hub (future)"} };
    g_allResearch[L"INT"] = { L"INT", L"Internet", L"Create a vast, interconnected global network for rapid data exchange and information sharing.", 43000, ResearchEra::INFORMATION, ResearchCategory::TECH, {L"TCM", L"ATO_CMP"}, {L"Passive: +10% Global Work Speed", L"Unlock: Internetwork Access in Computers and Smart Devices", L"Building: Server", L"Building: Switch", L"Building: Router"} };
    g_allResearch[L"INF_STH"] = { L"INF_STH", L"Stealth", L"Develop technologies to evade detection, crucial for espionage and covert operations.", 40000, ResearchEra::INFORMATION, ResearchCategory::SECURITY, {L"AT_BAL"}, {L"Recipe: Stealth Suit"} };
    g_allResearch[L"REN_EN"] = { L"REN_EN", L"Renewable Energy", L"Develop sustainable power sources that harness natural phenomena, reducing reliance on fossil fuels.", 38000, ResearchEra::INFORMATION, ResearchCategory::POWER, {L"IND_ELC", L"ATO_ECO"}, {L"Building: Wind Turbine", L"Building: Solar Panel (future)"} };
    g_allResearch[L"GEO_P"] = { L"GEO_P", L"Geothermal Power", L"Tap into the Earth's internal heat to generate clean, consistent energy.", 41000, ResearchEra::INFORMATION, ResearchCategory::POWER, {L"REN_EN", L"ENG"}, {L"Building: Geothermal Generator (future)"} };
    g_allResearch[L"GUI_SYS"] = { L"GUI_SYS", L"Guidance Systems", L"Create sophisticated systems for automated navigation and precision targeting in vehicles and missiles.", 46000, ResearchEra::INFORMATION, ResearchCategory::TECH, {L"ATO_RAD", L"ATO_CMP"}, {L"Recipe: GPS Module"} };
    g_allResearch[L"COMPO"] = { L"COMPO", L"Composites", L"Engineer advanced composite materials by combining different substances, yielding high strength-to-weight ratios.", 44000, ResearchEra::INFORMATION, ResearchCategory::CRAFTING, {L"SYN_MAT"}, {L"Recipe: Composite Panels"} };
    g_allResearch[L"BIO"] = { L"BIO", L"Bionics", L"Integrate mechanical and electronic components with biological systems to create advanced prosthetics and implants.", 49000, ResearchEra::INFORMATION, ResearchCategory::MEDICINE, {L"PROSTH", L"INF_ROB"}, {L"Recipe: Bionic Arm"} };
    g_allResearch[L"VRT"] = { L"VRT", L"Virtual Reality", L"Construct immersive virtual environments for training, entertainment, and advanced simulation.", 18000, ResearchEra::INFORMATION, ResearchCategory::RECREATION, {L"MOD_ELC"}, {L"Building: VR Station (future)", L"Recipe: VR Headset (future)"} }; // NEW
    g_allResearch[L"BLC"] = { L"BLC", L"Blockchain", L"Implement a decentralized ledger system for secure and transparent transactions, revolutionizing trade and record-keeping.", 19000, ResearchEra::INFORMATION, ResearchCategory::TECH, {L"TGR"}, {L"Passive: Enhanced Trade Security (future)", L"Unlocks: Digital Currencies (future)"} }; // NEW
    g_allResearch[L"CLN"] = { L"CLN", L"Cloning", L"Uncover the secrets of genetic replication, allowing for the creation of new lifeforms or the rapid healing of tissues.", 20000, ResearchEra::INFORMATION, ResearchCategory::MEDICINE, {L"IND_BIO"}, {L"Recipe: Cloning Vat (future)", L"Passive: Faster Healing (future)"} }; // NEW
    g_allResearch[L"CLC"] = { L"CLC", L"Cloud Computing", L"Develop networked computing resources that can be accessed remotely, enabling scalable data processing and collaboration.", 17500, ResearchEra::INFORMATION, ResearchCategory::TECH, {L"TGR"}, {L"Building: Server Farm (future)", L"Passive: Increased Research Speed (future)"} }; // NEW
    g_allResearch[L"CRG"] = { L"CRG", L"Cryptography", L"Devise secure communication methods to protect sensitive information from interception and tampering.", 16000, ResearchEra::INFORMATION, ResearchCategory::SECURITY, {L"MOD_ELC", L"ENC"}, {L"Passive: Enhanced Communication Security (future)"} }; // NEW
    g_allResearch[L"CYS"] = { L"CYS", L"Cybersecurity", L"Implement defensive measures to protect digital systems and networks from malicious attacks and data breaches.", 18500, ResearchEra::INFORMATION, ResearchCategory::SECURITY, {L"CLC", L"CRG"}, {L"Building: Firewall (future)", L"Passive: Improved Network Resilience (future)"} }; // NEW
    g_allResearch[L"DRN"] = { L"DRN", L"Drone Swarms", L"Design and control synchronized groups of autonomous drones for surveillance, transport, or combat operations.", 19500, ResearchEra::INFORMATION, ResearchCategory::COMBAT, {L"FLI", L"MOD_ELC"}, {L"Recipe: Combat Drone (future)", L"Work: Drone Piloting (future)"} }; // NEW
    g_allResearch[L"HLG"] = { L"HLG", L"Holograms", L"Project three-dimensional images using light interference, allowing for advanced displays and communication.", 17000, ResearchEra::INFORMATION, ResearchCategory::TECH, {L"OPT"}, {L"Building: Holographic Projector (future)"} }; // NEW
    g_allResearch[L"OPF"] = { L"OPF", L"Optic Fibers", L"Develop thin strands of glass or plastic for transmitting light signals over long distances, greatly improving data transfer.", 16500, ResearchEra::INFORMATION, ResearchCategory::TECH, {L"MOD_ELC"}, {L"Recipe: Fiber Optic Cable (future)", L"Passive: Increased Communication Range (future)"} }; // NEW


    // --- Cybernetic Era ---
    g_allResearch[L"ADV_AI"] = { L"ADV_AI", L"Advanced AI", L"Develop true sentient artificial intelligence capable of complex reasoning and independent learning.", 65000, ResearchEra::CYBERNETIC, ResearchCategory::TECH, {L"AIT", L"PAR_PHY"}, {L"Unlock: AI Colonists (future)"} };
    g_allResearch[L"PWR_CEL"] = { L"PWR_CEL", L"Power Cells", L"Create highly efficient, compact energy storage devices for portable power.", 62000, ResearchEra::CYBERNETIC, ResearchCategory::POWER, {L"NUF", L"SYN_MAT"}, {L"Recipe: Power Cell"} };
    g_allResearch[L"CYB"] = { L"CYB", L"Cybernetics", L"Directly interface technology with biological systems to augment human capabilities and treat severe injuries.", 70000, ResearchEra::CYBERNETIC, ResearchCategory::MEDICINE, {L"BIO", L"ADV_AI"}, {L"Recipe: Cybernetic Implants"} };
    g_allResearch[L"PRE_SYS"] = { L"PRE_SYS", L"Predictive Systems", L"Develop sophisticated algorithms to analyze vast datasets and predict future events with high accuracy.", 68000, ResearchEra::CYBERNETIC, ResearchCategory::SCIENCE, {L"INT", L"ADV_AI"}, {L"Passive: Early Warning Systems"} };
    g_allResearch[L"SEA"] = { L"SEA", L"Seasteads", L"Design and construct self-sustaining floating platforms for expanding habitable territory on water worlds.", 75000, ResearchEra::CYBERNETIC, ResearchCategory::CONSTRUCTION, {L"COMPO", L"ARC_REN", L"NAV"}, {L"Building: Modular Seastead (future)"} };
    g_allResearch[L"SMT_MAT"] = { L"SMT_MAT", L"Smart Materials", L"Engineer materials that can react to external stimuli by changing their properties, enabling adaptive structures.", 67000, ResearchEra::CYBERNETIC, ResearchCategory::CRAFTING, {L"SYN_MAT", L"NUF"}, {L"Recipe: Self-Repairing Alloys"} };
    g_allResearch[L"OFF_MIS"] = { L"OFF_MIS", L"Offworld Missions", L"Equip your colony for sustained operations beyond the homeworld, including deep space exploration.", 80000, ResearchEra::CYBERNETIC, ResearchCategory::SPACE, {L"INF_SAT", L"PWR_CEL", L"GUI_SYS"}, {L"Unlock: Interstellar Travel (future)"} };
    g_allResearch[L"AND"] = { L"AND", L"Androids", L"Construct advanced humanoid robots with sophisticated AI, capable of performing complex tasks autonomously.", 70000, ResearchEra::CYBERNETIC, ResearchCategory::TECH, {L"ADV_AI", L"SMT_MAT"}, {L"Recruit: Basic Android (future)", L"Recipe: Android Chassis (future)"} }; // NEW
    g_allResearch[L"EXO"] = { L"EXO", L"Exoskeletons", L"Develop powered external frameworks to augment strength and endurance for combat or heavy labor.", 65000, ResearchEra::CYBERNETIC, ResearchCategory::COMBAT, {L"CYB", L"SMT_MAT"}, {L"Recipe: Power Armor (future)", L"Recipe: Cargo Exoskeleton (future)"} }; // NEW
    g_allResearch[L"GNBL"] = { L"GNBL", L"Gunblades", L"Integrate melee weapons with firearm capabilities, allowing for versatile combat styles.", 68000, ResearchEra::CYBERNETIC, ResearchCategory::COMBAT, {L"LAS", L"EXO"}, {L"Recipe: Prototype Gunblade (future)"} }; // NEW
    g_allResearch[L"NEUCOMP"] = { L"NEUCOMP", L"Neural Computation", L"Develop computing systems that mimic biological neural networks, enabling advanced pattern recognition and learning.", 72000, ResearchEra::CYBERNETIC, ResearchCategory::SCIENCE, {L"ADV_AI", L"PAR_PHY"}, {L"Passive: Faster AI Learning (future)"} }; // NEW
    g_allResearch[L"QUANTC"] = { L"QUANTC", L"Quantum Computing", L"Harness quantum-mechanical phenomena to perform calculations far beyond classical computers, breaking modern encryption.", 75000, ResearchEra::CYBERNETIC, ResearchCategory::TECH, {L"NEUCOMP"}, {L"Building: Quantum Computer (future)", L"Passive: Uncrackable Encryption (future)"} }; // NEW
    g_allResearch[L"SMTWPN"] = { L"SMTWPN", L"Smart Weapons", L"Integrate advanced guidance systems and AI into projectile and energy weapons for unprecedented accuracy and lethality.", 69000, ResearchEra::CYBERNETIC, ResearchCategory::COMBAT, {L"GUI_SYS", L"AT_BAL"}, {L"Recipe: Smart Rifle (future)", L"Recipe: Guided Missile (future)"} }; // NEW
    g_allResearch[L"ECOTHEORY"] = { L"ECOTHEORY", L"Ecotheory", L"Gain a profound understanding of planetary ecosystems, enabling advanced terraforming and environmental control.", 66000, ResearchEra::CYBERNETIC, ResearchCategory::SCIENCE, {L"ATO_ECO", L"PAR_PHY"}, {L"Passive: Terraforming Research (future)"} }; // NEW
    g_allResearch[L"HEALFAC"] = { L"HEALFAC", L"Healing Factors", L"Manipulate biological regeneration processes to drastically accelerate wound healing and recovery from injury.", 71000, ResearchEra::CYBERNETIC, ResearchCategory::MEDICINE, {L"CYB", L"CLN"}, {L"Recipe: Regeneration Serum (future)", L"Passive: Rapid Healing for Colonists (future)"} }; // NEW
    g_allResearch[L"MOLANA"] = { L"MOLANA", L"Molecular Analysis", L"Develop tools to analyze and manipulate matter at the molecular level, crucial for advanced synthesis and materials science.", 73000, ResearchEra::CYBERNETIC, ResearchCategory::SCIENCE, {L"SMT_MAT", L"PAR_PHY"}, {L"Building: Molecular Assembler (future)", L"Passive: Increased Crafting Efficiency (future)"} }; // NEW

    // --- Interstellar Era ---
    g_allResearch[L"ARCO"] = { L"ARCO", L"Arcologies", L"Design and construct massive, self-contained multi-story habitats capable of housing entire populations.", 90000, ResearchEra::INTERSTELLAR, ResearchCategory::CONSTRUCTION, {L"SEA", L"ENG"}, {L"Building: Basic Arcology (future)"} }; // NEW
    g_allResearch[L"IONT"] = { L"IONT", L"Ion Thrusters", L"Develop highly efficient propulsion systems that use ionized particles for long-duration interstellar travel.", 95000, ResearchEra::INTERSTELLAR, ResearchCategory::SPACE, {L"OFF_MIS", L"NUF"}, {L"Building: Ion Engine (future)", L"Passive: Faster Interstellar Travel (future)"} }; // NEW
    g_allResearch[L"THTR"] = { L"THTR", L"Thermal Thrusters", L"Engineer propulsion systems that heat propellant using nuclear fission or fusion for powerful, sustained thrust.", 92000, ResearchEra::INTERSTELLAR, ResearchCategory::SPACE, {L"OFF_MIS", L"ATO_NUC"}, {L"Building: Thermal Engine (future)", L"Passive: Increased Ship Speed (future)"} }; // NEW
    g_allResearch[L"WHR"] = { L"WHR", L"Waste Heat Recovery", L"Implement advanced systems to capture and convert waste heat into usable energy, dramatically improving efficiency.", 88000, ResearchEra::INTERSTELLAR, ResearchCategory::POWER, {L"PWR_CEL", L"SMT_MAT"}, {L"Passive: +20% Power Efficiency (future)"} }; // NEW
}

// --- String Utility Function ---
std::wstring replacePlaceholder(std::wstring original, const std::wstring& placeholder, const std::wstring& value) {
    size_t pos = 0;
    while ((pos = original.find(placeholder, pos)) != std::wstring::npos) {
        original.replace(pos, placeholder.length(), value);
        pos += value.length();
    }
    return original;
}

void saveFontSelection() {
    // We save the font by its name as it appears in the menu (e.g., "(Default)" or "MyCustomFont")
    std::wstring fontToSave = L"(Default)";
    if (g_currentFontName != L"Consolas") {
        fontToSave = g_currentFontName;
    }

    HANDLE hFile = CreateFileW(FONT_CONFIG_FILE.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile != INVALID_HANDLE_VALUE) {
        // UTF-16 files should start with a BOM (Byte Order Mark)
        DWORD bytesWritten;
        WORD bom = 0xFEFF;
        WriteFile(hFile, &bom, sizeof(bom), &bytesWritten, NULL);
        // Write the actual font name
        WriteFile(hFile, fontToSave.c_str(), (DWORD)(fontToSave.length() * sizeof(wchar_t)), &bytesWritten, NULL);
        CloseHandle(hFile);
    }
}

void loadFontSelection() {
    HANDLE hFile = CreateFileW(FONT_CONFIG_FILE.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        // Config file doesn't exist, do nothing.
        return;
    }

    DWORD fileSize = GetFileSize(hFile, NULL);
    if (fileSize > 2) { // Must be larger than the BOM
        // Allocate buffer and read the file
        std::vector<wchar_t> buffer(fileSize / sizeof(wchar_t) + 1, 0);
        DWORD bytesRead;
        ReadFile(hFile, buffer.data(), fileSize, &bytesRead, NULL);
        CloseHandle(hFile);

        // Check for BOM and extract the font name
        if (buffer[0] == 0xFEFF) {
            std::wstring loadedFontName(buffer.data() + 1); // Skip BOM

            // Check if the loaded font is still available
            bool fontExists = false;
            for (const auto& availableFont : g_availableFonts) {
                if (availableFont == loadedFontName) {
                    fontExists = true;
                    break;
                }
            }

            if (fontExists) {
                // If it exists, apply it
                if (loadedFontName == L"(Default)") {
                    g_currentFontName = L"Consolas";
                }
                else {
                    g_currentFontName = loadedFontName;
                    g_currentFontFile = L"Fonts\\" + loadedFontName + L".ttf";
                    AddFontResourceExW(g_currentFontFile.c_str(), FR_PRIVATE, NULL);
                }
            }
        }
    }
    else {
        CloseHandle(hFile);
    }
}

void generateOresInStratum(int z, const StratumInfo& sInfo,
    const std::set<TileType>& sedimentaryStones,
    const std::set<TileType>& igneousExtrusiveStones,
    const std::set<TileType>& igneousIntrusiveStones,
    const std::set<TileType>& metamorphicStones,
    const std::set<TileType>& allIgneousStones,
    const std::set<TileType>& allStones);

// NEW: Helper function to read names from a text file (one name per line)
std::vector<std::wstring> readNamesFromFile(const std::wstring& filePath) {
    std::vector<std::wstring> names;
    std::wifstream file(filePath);

    if (!file.is_open()) {
        return names; // Return empty vector if file can't be opened
    }

    // Set the locale to handle UTF-8 files correctly
    file.imbue(std::locale(file.getloc(), new std::codecvt_utf8<wchar_t>));

    std::wstring line;
    while (std::getline(file, line)) {
        // Remove potential carriage return at the end of the line
        if (!line.empty() && line.back() == L'\r') {
            line.pop_back();
        }
        // Add non-empty lines to our list
        if (!line.empty()) {
            names.push_back(line);
        }
    }

    file.close();
    return names;
}

void initGameData() {

    int TROPOSPHERE_TOP_Z_LEVEL = 0;

    // NEW: Create Data directory for name lists etc.
    if (!CreateDirectory(L"Data", NULL)) {
        if (GetLastError() != ERROR_ALREADY_EXISTS) {
            // This is an optional place to handle an error if the directory couldn't be created
        }
    }

    TILE_DATA.clear();
    // Base Tiles
    TILE_DATA[TileType::EMPTY] = { L"Empty Space", L' ', RGB(0,0,0), {}, TileType::EMPTY, 0.0f, 0.0f };
    TILE_DATA[TileType::DIRT_FLOOR] = { L"Dirt", L'.', RGB(139,69,19), {TileTag::DIRT, TileTag::SOIL}, TileType::EMPTY };
    TILE_DATA[TileType::GRASS] = { L"Grass", L'.', RGB(34,139,34), {TileTag::DIRT, TileTag::SOIL}, TileType::EMPTY };
    TILE_DATA[TileType::JUNGLE_GRASS] = { L"Jungle Grass", L'.', RGB(0, 100, 0), {TileTag::DIRT, TileTag::SOIL}, TileType::EMPTY };
    TILE_DATA[TileType::SAND] = { L"Sand", L'~', RGB(244,164,96), {TileTag::DIRT, TileTag::SOIL}, TileType::EMPTY };
    TILE_DATA[TileType::SNOW] = { L"Snow", L'·', RGB(255,250,250), {TileTag::DIRT, TileTag::SOIL}, TileType::EMPTY };
    TILE_DATA[TileType::ICE] = { L"Ice", L'#', RGB(173,216,230), {TileTag::DIRT}, TileType::EMPTY };
    TILE_DATA[TileType::STONE_FLOOR] = { L"Stone Floor", L'.', RGB(80, 80, 80), {TileTag::SOIL}, TileType::EMPTY };
    TILE_DATA[TileType::WOOD_FLOOR] = { L"Wood Floor", L'=', RGB(160, 82, 45), {TileTag::SOIL, TileTag::STRUCTURE}, TileType::EMPTY };

    // Stones
    TILE_DATA[TileType::CHALK] = { L"Chalk", L'#', RGB(245,245,245), {TileTag::STONE, TileTag::SEDIMENTARY}, TileType::CHALK_CHUNK, 1.0f, 0.5f };
    TILE_DATA[TileType::CHERT] = { L"Chert", L'#', RGB(210,180,140), {TileTag::STONE, TileTag::SEDIMENTARY}, TileType::CHERT_CHUNK, 1.2f, 0.6f };
    TILE_DATA[TileType::CLAYSTONE] = { L"Claystone", L'#', RGB(188,143,143), {TileTag::STONE, TileTag::SEDIMENTARY}, TileType::CLAYSTONE_CHUNK, 0.9f, 0.4f };
    TILE_DATA[TileType::CONGLOMERATE] = { L"Conglomerate", L'#', RGB(160,160,160), {TileTag::STONE, TileTag::SEDIMENTARY}, TileType::CONGLOMERATE_CHUNK, 1.5f, 0.7f };
    TILE_DATA[TileType::DOLOMITE] = { L"Dolomite", L'#', RGB(192,192,192), {TileTag::STONE, TileTag::SEDIMENTARY}, TileType::DOLOMITE_CHUNK, 1.3f, 0.6f };
    TILE_DATA[TileType::LIMESTONE] = { L"Limestone", L'#', RGB(211,211,211), {TileTag::STONE, TileTag::SEDIMENTARY}, TileType::LIMESTONE_CHUNK, 1.1f, 0.5f };
    TILE_DATA[TileType::MUDSTONE] = { L"Mudstone", L'#', RGB(139,115,85), {TileTag::STONE, TileTag::SEDIMENTARY}, TileType::MUDSTONE_CHUNK, 0.8f, 0.3f };
    TILE_DATA[TileType::ROCK_SALT] = { L"Rock Salt", L'#', RGB(255,228,225), {TileTag::STONE, TileTag::SEDIMENTARY}, TileType::ROCK_SALT_CHUNK, 0.7f, 0.9f }; // High value for salt
    TILE_DATA[TileType::SANDSTONE] = { L"Sandstone", L'#', RGB(218,165,32), {TileTag::STONE, TileTag::SEDIMENTARY}, TileType::SANDSTONE_CHUNK, 1.0f, 0.5f };
    TILE_DATA[TileType::SHALE] = { L"Shale", L'#', RGB(128,128,128), {TileTag::STONE, TileTag::SEDIMENTARY}, TileType::SHALE_CHUNK, 1.1f, 0.4f };
    TILE_DATA[TileType::SILTSTONE] = { L"Siltstone", L'#', RGB(198,179,155), {TileTag::STONE, TileTag::SEDIMENTARY}, TileType::SILTSTONE_CHUNK, 1.0f, 0.45f };
    TILE_DATA[TileType::DIORITE] = { L"Diorite", L'#', RGB(119,136,153), {TileTag::STONE, TileTag::IGNEOUS_INTRUSIVE}, TileType::DIORITE_CHUNK, 1.8f, 0.9f };
    TILE_DATA[TileType::GABBRO] = { L"Gabbro", L'#', RGB(80,80,80), {TileTag::STONE, TileTag::IGNEOUS_INTRUSIVE}, TileType::GABBRO_CHUNK, 2.0f, 1.0f };
    TILE_DATA[TileType::GRANITE] = { L"Granite", L'#', RGB(150,150,150), {TileTag::STONE, TileTag::IGNEOUS_INTRUSIVE}, TileType::GRANITE_CHUNK, 7.0f, 1.2f }; // Original value
    TILE_DATA[TileType::ANDESITE] = { L"Andesite", L'#', RGB(130,130,130), {TileTag::STONE, TileTag::IGNEOUS_EXTRUSIVE}, TileType::ANDESITE_CHUNK, 1.6f, 0.8f };
    TILE_DATA[TileType::BASALT] = { L"Basalt", L'#', RGB(40,40,40), {TileTag::STONE, TileTag::IGNEOUS_EXTRUSIVE}, TileType::BASALT_CHUNK, 1.9f, 0.95f };
    TILE_DATA[TileType::DACITE] = { L"Dacite", L'#', RGB(180,180,180), {TileTag::STONE, TileTag::IGNEOUS_EXTRUSIVE}, TileType::DACITE_CHUNK, 1.7f, 0.85f };
    TILE_DATA[TileType::OBSIDIAN] = { L"Obsidian", L'#', RGB(20,20,20), {TileTag::STONE, TileTag::IGNEOUS_EXTRUSIVE}, TileType::OBSIDIAN_CHUNK, 2.5f, 1.5f }; // Higher value for obsidian
    TILE_DATA[TileType::RHYOLITE] = { L"Rhyolite", L'#', RGB(255,192,203), {TileTag::STONE, TileTag::IGNEOUS_EXTRUSIVE}, TileType::RHYOLITE_CHUNK, 1.5f, 0.75f };
    TILE_DATA[TileType::GNEISS] = { L"Gneiss", L'#', RGB(105,105,105), {TileTag::STONE, TileTag::METAMORPHIC}, TileType::GNEISS_CHUNK, 2.2f, 1.1f };
    TILE_DATA[TileType::MARBLE] = { L"Marble", L'#', RGB(255,250,250), {TileTag::STONE, TileTag::METAMORPHIC}, TileType::MARBLE_CHUNK, 2.3f, 1.3f };
    TILE_DATA[TileType::PHYLLITE] = { L"Phyllite", L'#', RGB(176,196,222), {TileTag::STONE, TileTag::METAMORPHIC}, TileType::PHYLLITE_CHUNK, 2.0f, 1.0f };
    TILE_DATA[TileType::QUARTZITE] = { L"Quartzite", L'#', RGB(238,232,170), {TileTag::STONE, TileTag::METAMORPHIC}, TileType::QUARTZITE_CHUNK, 2.4f, 1.2f };
    TILE_DATA[TileType::SCHIST] = { L"Schist", L'#', RGB(0,128,128), {TileTag::STONE, TileTag::METAMORPHIC}, TileType::SCHIST_CHUNK, 2.1f, 1.05f };
    TILE_DATA[TileType::SLATE] = { L"Slate", L'#', RGB(70,70,70), {TileTag::STONE, TileTag::METAMORPHIC}, TileType::SLATE_CHUNK, 1.9f, 0.9f };
    TILE_DATA[TileType::CORESTONE] = { L"Corestone", L'#', RGB(25,25,25), {TileTag::STONE, TileTag::INNER_STONE}, TileType::CORESTONE_CHUNK, 10.0f, 5.0f }; // Very high hardness/value
    TILE_DATA[TileType::WATER] = { L"Water", L'~', RGB(0,0,139), {TileTag::WATER, TileTag::FLUID}, TileType::EMPTY };
    TILE_DATA[TileType::MOLTEN_CORE] = { L"Molten Core", L'~', RGB(255,100,0), {TileTag::WATER, TileTag::FLUID}, TileType::EMPTY };
    TILE_DATA[TileType::RICH_MINERALS] = { L"Rich Minerals", L'%', RGB(255,215,0), {TileTag::MINERAL}, TileType::EMPTY, 3.0f, 10.0f };


    // Base types (for spawning logic) - These are not items, they are world features.
    TILE_DATA[TileType::OAK] = { L"Oak Tree", L' ', RGB(0,0,0), {}, TileType::OAK_WOOD, 0.0f, 0.0f, TileType::OAK_TRUNK };
    TILE_DATA[TileType::ACACIA] = { L"Acacia Tree", L' ', RGB(0,0,0), {}, TileType::ACACIA_WOOD, 0.0f, 0.0f, TileType::ACACIA_TRUNK };
    TILE_DATA[TileType::SPRUCE] = { L"Spruce Tree", L' ', RGB(0,0,0), {}, TileType::SPRUCE_WOOD, 0.0f, 0.0f, TileType::SPRUCE_TRUNK };
    TILE_DATA[TileType::BIRCH] = { L"Birch Tree", L' ', RGB(0,0,0), {}, TileType::BIRCH_WOOD, 0.0f, 0.0f, TileType::BIRCH_TRUNK };
    TILE_DATA[TileType::PINE] = { L"Pine Tree", L' ', RGB(0,0,0), {}, TileType::PINE_WOOD, 0.0f, 0.0f, TileType::PINE_TRUNK };
    TILE_DATA[TileType::POPLAR] = { L"Poplar Tree", L' ', RGB(0,0,0), {}, TileType::POPLAR_WOOD, 0.0f, 0.0f, TileType::POPLAR_TRUNK };
    TILE_DATA[TileType::CECROPIA] = { L"Cecropia Tree", L' ', RGB(0,0,0), {}, TileType::CECROPIA_WOOD, 0.0f, 0.0f, TileType::CECROPIA_TRUNK };
    TILE_DATA[TileType::COCOA] = { L"Cocoa Tree", L' ', RGB(0,0,0), {}, TileType::COCOA_WOOD, 0.0f, 0.0f, TileType::COCOA_TRUNK };
    TILE_DATA[TileType::CYPRESS] = { L"Cypress Tree", L' ', RGB(0,0,0), {}, TileType::CYPRESS_WOOD, 0.0f, 0.0f, TileType::CYPRESS_TRUNK };
    TILE_DATA[TileType::MAPLE] = { L"Maple Tree", L' ', RGB(0,0,0), {}, TileType::MAPLE_WOOD, 0.0f, 0.0f, TileType::MAPLE_TRUNK };
    TILE_DATA[TileType::PALM] = { L"Palm Tree", L' ', RGB(0,0,0), {}, TileType::PALM_WOOD, 0.0f, 0.0f, TileType::PALM_TRUNK };
    TILE_DATA[TileType::TEAK] = { L"Teak Tree", L' ', RGB(0,0,0), {}, TileType::TEAK_WOOD, 0.0f, 0.0f, TileType::TEAK_TRUNK };
    TILE_DATA[TileType::SAGUARO] = { L"Saguaro Cactus", L' ', RGB(0,0,0), {}, TileType::SAGUARO_WOOD, 0.0f, 0.0f, TileType::SAGUARO_TRUNK };
    TILE_DATA[TileType::PRICKLYPEAR] = { L"Prickly Pear Cactus", L' ', RGB(0,0,0), {}, TileType::PRICKLYPEAR_WOOD, 0.0f, 0.0f, TileType::PRICKLYPEAR_PAD };
    TILE_DATA[TileType::CHOLLA] = { L"Cholla Cactus", L' ', RGB(0,0,0), {}, TileType::CHOLLA_WOOD, 0.0f, 0.0f, TileType::CHOLLA_TRUNK };

    // Tree Parts
    // These are temporary parts of a tree and generally shouldn't be picked up as loose items
    // unless they have a 'drops' defined, in which case their 'drops' item would be haulable.
    // For now, they are not marked as TileTag::ITEM directly.
    TILE_DATA[TileType::TRUNK] = { L"Trunk", L'0', RGB(139,69,19), {TileTag::TREE_PART, TileTag::TREE_TRUNK}, TileType::OAK_WOOD };
    TILE_DATA[TileType::BRANCH] = { L"Branch", L'|', RGB(139,69,19), {TileTag::TREE_PART, TileTag::TREE_BRANCH}, TileType::OAK_WOOD };
    TILE_DATA[TileType::LEAF] = { L"Leaves", L'♣', RGB(0,100,0), {TileTag::TREE_PART, TileTag::TREE_LEAF}, TileType::EMPTY };
    TILE_DATA[TileType::OAK_TRUNK] = { L"Oak Trunk", L'0', RGB(139,69,19), {TileTag::TREE_PART, TileTag::TREE_TRUNK}, TileType::OAK_WOOD };
    TILE_DATA[TileType::OAK_BRANCH] = { L"Oak Branch", L'|', RGB(139,69,19), {TileTag::TREE_PART, TileTag::TREE_BRANCH}, TileType::OAK_WOOD };
    TILE_DATA[TileType::OAK_LEAF] = { L"Oak Leaves", L'♣', RGB(0,100,0), {TileTag::TREE_PART, TileTag::TREE_LEAF}, TileType::EMPTY };
    TILE_DATA[TileType::ACACIA_TRUNK] = { L"Acacia Trunk", L'0', RGB(184, 142, 90), {TileTag::TREE_PART, TileTag::TREE_TRUNK}, TileType::ACACIA_WOOD };
    TILE_DATA[TileType::ACACIA_BRANCH] = { L"Acacia Branch", L'-', RGB(184, 142, 90), {TileTag::TREE_PART, TileTag::TREE_BRANCH}, TileType::ACACIA_WOOD };
    TILE_DATA[TileType::ACACIA_LEAF] = { L"Acacia Leaves", L'☘', RGB(124, 252, 0), {TileTag::TREE_PART, TileTag::TREE_LEAF}, TileType::EMPTY };
    TILE_DATA[TileType::SPRUCE_TRUNK] = { L"Spruce Trunk", L'0', RGB(94, 65, 41), {TileTag::TREE_PART, TileTag::TREE_TRUNK}, TileType::SPRUCE_WOOD };
    TILE_DATA[TileType::SPRUCE_BRANCH] = { L"Spruce Branch", L'\\', RGB(94, 65, 41), {TileTag::TREE_PART, TileTag::TREE_BRANCH}, TileType::SPRUCE_WOOD };
    TILE_DATA[TileType::SPRUCE_NEEDLE] = { L"Spruce Needles", L'♠', RGB(26, 61, 26), {TileTag::TREE_PART, TileTag::TREE_LEAF}, TileType::EMPTY };
    TILE_DATA[TileType::BIRCH_TRUNK] = { L"Birch Trunk", L'0', RGB(245, 245, 220), {TileTag::TREE_PART, TileTag::TREE_TRUNK}, TileType::BIRCH_WOOD };
    TILE_DATA[TileType::BIRCH_BRANCH] = { L"Birch Branch", L'/', RGB(225, 225, 200), {TileTag::TREE_PART, TileTag::TREE_BRANCH}, TileType::BIRCH_WOOD };
    TILE_DATA[TileType::BIRCH_LEAF] = { L"Birch Leaves", L'♣', RGB(154, 205, 50), {TileTag::TREE_PART, TileTag::TREE_LEAF}, TileType::EMPTY };
    TILE_DATA[TileType::PINE_TRUNK] = { L"Pine Trunk", L'0', RGB(115, 74, 47), {TileTag::TREE_PART, TileTag::TREE_TRUNK}, TileType::PINE_WOOD };
    TILE_DATA[TileType::PINE_BRANCH] = { L"Pine Branch", L'-', RGB(115, 74, 47), {TileTag::TREE_PART, TileTag::TREE_BRANCH}, TileType::PINE_WOOD };
    TILE_DATA[TileType::PINE_NEEDLE] = { L"Pine Needles", L'♠', RGB(0, 80, 0), {TileTag::TREE_PART, TileTag::TREE_LEAF}, TileType::EMPTY };
    TILE_DATA[TileType::POPLAR_TRUNK] = { L"Poplar Trunk", L'║', RGB(191, 179, 155), {TileTag::TREE_PART, TileTag::TREE_TRUNK}, TileType::POPLAR_WOOD };
    TILE_DATA[TileType::POPLAR_BRANCH] = { L"Poplar Branch", L'|', RGB(191, 179, 155), {TileTag::TREE_PART, TileTag::TREE_BRANCH}, TileType::POPLAR_WOOD };
    TILE_DATA[TileType::POPLAR_LEAF] = { L"Poplar Leaves", L'♥', RGB(255, 215, 0), {TileTag::TREE_PART, TileTag::TREE_LEAF}, TileType::EMPTY };
    TILE_DATA[TileType::CECROPIA_TRUNK] = { L"Cecropia Trunk", L'0', RGB(170, 169, 173), {TileTag::TREE_PART, TileTag::TREE_TRUNK}, TileType::CECROPIA_WOOD };
    TILE_DATA[TileType::CECROPIA_BRANCH] = { L"Cecropia Stalk", L'/', RGB(170, 169, 173), {TileTag::TREE_PART, TileTag::TREE_BRANCH}, TileType::CECROPIA_WOOD };
    TILE_DATA[TileType::CECROPIA_LEAF] = { L"Cecropia Leaves", L'☘', RGB(74, 114, 48), {TileTag::TREE_PART, TileTag::TREE_LEAF}, TileType::EMPTY };
    TILE_DATA[TileType::COCOA_TRUNK] = { L"Cocoa Trunk", L'0', RGB(112, 66, 20), {TileTag::TREE_PART, TileTag::TREE_TRUNK}, TileType::COCOA_WOOD };
    TILE_DATA[TileType::COCOA_BRANCH] = { L"Cocoa Branch", L'|', RGB(112, 66, 20), {TileTag::TREE_PART, TileTag::TREE_BRANCH}, TileType::COCOA_WOOD };
    TILE_DATA[TileType::COCOA_LEAF] = { L"Cocoa Leaves", L'♣', RGB(0, 70, 0), {TileTag::TREE_PART, TileTag::TREE_LEAF}, TileType::EMPTY };
    TILE_DATA[TileType::CYPRESS_TRUNK] = { L"Cypress Trunk", L'║', RGB(116, 95, 68), {TileTag::TREE_PART, TileTag::TREE_TRUNK}, TileType::CYPRESS_WOOD };
    TILE_DATA[TileType::CYPRESS_BRANCH] = { L"Cypress Branch", L'|', RGB(116, 95, 68), {TileTag::TREE_PART, TileTag::TREE_BRANCH}, TileType::CYPRESS_WOOD };
    TILE_DATA[TileType::CYPRESS_FOLIAGE] = { L"Cypress Foliage", L'♠', RGB(46, 70, 46), {TileTag::TREE_PART, TileTag::TREE_LEAF}, TileType::EMPTY };
    TILE_DATA[TileType::MAPLE_TRUNK] = { L"Maple Trunk", L'0', RGB(129, 93, 56), {TileTag::TREE_PART, TileTag::TREE_TRUNK}, TileType::MAPLE_WOOD };
    TILE_DATA[TileType::MAPLE_BRANCH] = { L"Maple Branch", L'|', RGB(129, 93, 56), {TileTag::TREE_PART, TileTag::TREE_BRANCH}, TileType::MAPLE_WOOD };
    TILE_DATA[TileType::MAPLE_LEAF] = { L"Maple Leaves", L'♥', RGB(227, 66, 52), {TileTag::TREE_PART, TileTag::TREE_LEAF}, TileType::EMPTY };
    TILE_DATA[TileType::PALM_TRUNK] = { L"Palm Trunk", L'║', RGB(210, 180, 140), {TileTag::TREE_PART, TileTag::TREE_TRUNK}, TileType::PALM_WOOD };
    TILE_DATA[TileType::PALM_FROND] = { L"Palm Frond", L'¥', RGB(0, 128, 0), {TileTag::TREE_PART, TileTag::TREE_LEAF}, TileType::EMPTY };
    TILE_DATA[TileType::TEAK_TRUNK] = { L"Teak Trunk", L'0', RGB(138, 104, 53), {TileTag::TREE_PART, TileTag::TREE_TRUNK}, TileType::TEAK_WOOD };
    TILE_DATA[TileType::TEAK_BRANCH] = { L"Teak Branch", L'|', RGB(138, 104, 53), {TileTag::TREE_PART, TileTag::TREE_BRANCH}, TileType::TEAK_WOOD };
    TILE_DATA[TileType::TEAK_LEAF] = { L"Teak Leaves", L'♣', RGB(60, 90, 30), {TileTag::TREE_PART, TileTag::TREE_LEAF}, TileType::EMPTY };
    TILE_DATA[TileType::SAGUARO_TRUNK] = { L"Saguaro Trunk", L'┃', RGB(0, 107, 60), {TileTag::CACTUS_PART, TileTag::TREE_TRUNK}, TileType::SAGUARO_WOOD };
    TILE_DATA[TileType::SAGUARO_ARM] = { L"Saguaro Arm", L'┃', RGB(0, 128, 70), {TileTag::CACTUS_PART, TileTag::TREE_BRANCH}, TileType::SAGUARO_WOOD };
    TILE_DATA[TileType::PRICKLYPEAR_PAD] = { L"Prickly Pear Pad", L'O', RGB(84, 140, 61), {TileTag::CACTUS_PART, TileTag::TREE_BRANCH}, TileType::PRICKLYPEAR_WOOD };
    TILE_DATA[TileType::PRICKLYPEAR_TUNA] = { L"Prickly Pear Fruit", L'o', RGB(199, 21, 133), {TileTag::CACTUS_PART, TileTag::TREE_LEAF}, TileType::EMPTY };
    TILE_DATA[TileType::CHOLLA_TRUNK] = { L"Cholla Trunk", L'|', RGB(141, 120, 81), {TileTag::CACTUS_PART, TileTag::TREE_TRUNK}, TileType::CHOLLA_WOOD };
    TILE_DATA[TileType::CHOLLA_JOINT] = { L"Cholla Joint", L'#', RGB(161, 140, 101), {TileTag::CACTUS_PART, TileTag::TREE_BRANCH}, TileType::CHOLLA_WOOD };

    // Resources (Items on ground)
    /// Stone Chunks
    TILE_DATA[TileType::STONE_CHUNK] = { L"Stone Chunk", L'*', RGB(128,128,128), {TileTag::ITEM, TileTag::CHUNK, TileTag::STONE}, TileType::EMPTY };
    TILE_DATA[TileType::CHALK_CHUNK] = { L"Chalk Chunk", L'*', RGB(245,245,245), {TileTag::ITEM, TileTag::CHUNK, TileTag::SEDIMENTARY}, TileType::EMPTY };
    TILE_DATA[TileType::CHERT_CHUNK] = { L"Chert Chunk", L'*', RGB(210,180,140), {TileTag::ITEM, TileTag::CHUNK, TileTag::SEDIMENTARY}, TileType::EMPTY };
    TILE_DATA[TileType::CLAYSTONE_CHUNK] = { L"Claystone Chunk", L'*', RGB(188,143,143), {TileTag::ITEM, TileTag::CHUNK, TileTag::SEDIMENTARY}, TileType::EMPTY };
    TILE_DATA[TileType::CONGLOMERATE_CHUNK] = { L"Conglomerate Chunk", L'*', RGB(160,160,160), {TileTag::ITEM, TileTag::CHUNK, TileTag::SEDIMENTARY}, TileType::EMPTY };
    TILE_DATA[TileType::DOLOMITE_CHUNK] = { L"Dolomite Chunk", L'*', RGB(192,192,192), {TileTag::ITEM, TileTag::CHUNK, TileTag::SEDIMENTARY}, TileType::EMPTY };
    TILE_DATA[TileType::LIMESTONE_CHUNK] = { L"Limestone Chunk", L'*', RGB(211,211,211), {TileTag::ITEM, TileTag::CHUNK, TileTag::SEDIMENTARY}, TileType::EMPTY };
    TILE_DATA[TileType::MUDSTONE_CHUNK] = { L"Mudstone Chunk", L'*', RGB(139,115,85), {TileTag::ITEM, TileTag::CHUNK, TileTag::SEDIMENTARY}, TileType::EMPTY };
    TILE_DATA[TileType::ROCK_SALT_CHUNK] = { L"Rock Salt Chunk", L'*', RGB(255,228,225), {TileTag::ITEM, TileTag::CHUNK, TileTag::SEDIMENTARY}, TileType::EMPTY };
    TILE_DATA[TileType::SANDSTONE_CHUNK] = { L"Sandstone Chunk", L'*', RGB(218,165,32), {TileTag::ITEM, TileTag::CHUNK, TileTag::SEDIMENTARY}, TileType::EMPTY };
    TILE_DATA[TileType::SHALE_CHUNK] = { L"Shale Chunk", L'*', RGB(128,128,128), {TileTag::ITEM, TileTag::CHUNK, TileTag::SEDIMENTARY}, TileType::EMPTY };
    TILE_DATA[TileType::SILTSTONE_CHUNK] = { L"Siltstone Chunk", L'*', RGB(198,179,155), {TileTag::ITEM, TileTag::CHUNK, TileTag::SEDIMENTARY}, TileType::EMPTY };
    TILE_DATA[TileType::DIORITE_CHUNK] = { L"Diorite Chunk", L'*', RGB(119,136,153), {TileTag::ITEM, TileTag::CHUNK, TileTag::IGNEOUS_INTRUSIVE}, TileType::EMPTY };
    TILE_DATA[TileType::GABBRO_CHUNK] = { L"Gabbro Chunk", L'*', RGB(80,80,80), {TileTag::ITEM, TileTag::CHUNK, TileTag::IGNEOUS_INTRUSIVE}, TileType::EMPTY };
    TILE_DATA[TileType::GRANITE_CHUNK] = { L"Granite Chunk", L'*', RGB(150,150,150), {TileTag::ITEM, TileTag::CHUNK, TileTag::IGNEOUS_INTRUSIVE}, TileType::EMPTY };
    TILE_DATA[TileType::ANDESITE_CHUNK] = { L"Andesite Chunk", L'*', RGB(130,130,130), {TileTag::ITEM, TileTag::CHUNK, TileTag::IGNEOUS_EXTRUSIVE}, TileType::EMPTY };
    TILE_DATA[TileType::BASALT_CHUNK] = { L"Basalt Chunk", L'*', RGB(40,40,40), {TileTag::ITEM, TileTag::CHUNK, TileTag::IGNEOUS_EXTRUSIVE}, TileType::EMPTY };
    TILE_DATA[TileType::DACITE_CHUNK] = { L"Dacite Chunk", L'*', RGB(180,180,180), {TileTag::ITEM, TileTag::CHUNK, TileTag::IGNEOUS_EXTRUSIVE}, TileType::EMPTY };
    TILE_DATA[TileType::OBSIDIAN_CHUNK] = { L"Obsidian Chunk", L'*', RGB(20,20,20), {TileTag::ITEM, TileTag::CHUNK, TileTag::IGNEOUS_EXTRUSIVE}, TileType::EMPTY };
    TILE_DATA[TileType::RHYOLITE_CHUNK] = { L"Rhyolite Chunk", L'*', RGB(255,192,203), {TileTag::ITEM, TileTag::CHUNK, TileTag::IGNEOUS_EXTRUSIVE}, TileType::EMPTY };
    TILE_DATA[TileType::GNEISS_CHUNK] = { L"Gneiss Chunk", L'*', RGB(105,105,105), {TileTag::ITEM, TileTag::CHUNK, TileTag::METAMORPHIC}, TileType::EMPTY };
    TILE_DATA[TileType::MARBLE_CHUNK] = { L"Marble Chunk", L'*', RGB(255,250,250), {TileTag::ITEM, TileTag::CHUNK, TileTag::METAMORPHIC}, TileType::EMPTY };
    TILE_DATA[TileType::PHYLLITE_CHUNK] = { L"Phyllite Chunk", L'*', RGB(176,196,222), {TileTag::ITEM, TileTag::CHUNK, TileTag::METAMORPHIC}, TileType::EMPTY };
    TILE_DATA[TileType::QUARTZITE_CHUNK] = { L"Quartzite Chunk", L'*', RGB(238,232,170), {TileTag::ITEM, TileTag::CHUNK, TileTag::METAMORPHIC}, TileType::EMPTY };
    TILE_DATA[TileType::SCHIST_CHUNK] = { L"Schist Chunk", L'*', RGB(0,128,128), {TileTag::ITEM, TileTag::CHUNK, TileTag::METAMORPHIC}, TileType::EMPTY };
    TILE_DATA[TileType::SLATE_CHUNK] = { L"Slate Chunk", L'*', RGB(70,70,70), {TileTag::ITEM, TileTag::CHUNK, TileTag::METAMORPHIC}, TileType::EMPTY };
    TILE_DATA[TileType::CORESTONE_CHUNK] = { L"Corestone Chunk", L'*', RGB(25,25,25), {TileTag::ITEM, TileTag::CHUNK, TileTag::INNER_STONE}, TileType::EMPTY };
    /// Woods
    TILE_DATA[TileType::OAK_WOOD] = { L"Oak Wood", L'=', RGB(139,69,19), {TileTag::ITEM, TileTag::WOOD}, TileType::EMPTY, 4.0f, 0.8f };
    TILE_DATA[TileType::ACACIA_WOOD] = { L"Acacia Wood", L'=', RGB(205,133,63), {TileTag::ITEM, TileTag::WOOD}, TileType::EMPTY };
    TILE_DATA[TileType::SPRUCE_WOOD] = { L"Spruce Wood", L'=', RGB(139,90,43), {TileTag::ITEM, TileTag::WOOD}, TileType::EMPTY };
    TILE_DATA[TileType::BIRCH_WOOD] = { L"Birch Wood", L'=', RGB(245,222,179), {TileTag::ITEM, TileTag::WOOD}, TileType::EMPTY };
    TILE_DATA[TileType::PINE_WOOD] = { L"Pine Wood", L'=', RGB(160,82,45), {TileTag::ITEM, TileTag::WOOD}, TileType::EMPTY, 2.0f, 1.5f };
    TILE_DATA[TileType::POPLAR_WOOD] = { L"Poplar Wood", L'=', RGB(222,184,135), {TileTag::ITEM, TileTag::WOOD}, TileType::EMPTY };
    TILE_DATA[TileType::CECROPIA_WOOD] = { L"Cecropia Wood", L'=', RGB(188,143,143), {TileTag::ITEM, TileTag::WOOD}, TileType::EMPTY };
    TILE_DATA[TileType::COCOA_WOOD] = { L"Cocoa Wood", L'=', RGB(139,69,19), {TileTag::ITEM, TileTag::WOOD}, TileType::EMPTY };
    TILE_DATA[TileType::CYPRESS_WOOD] = { L"Cypress Wood", L'=', RGB(210,180,140), {TileTag::ITEM, TileTag::WOOD}, TileType::EMPTY };
    TILE_DATA[TileType::MAPLE_WOOD] = { L"Maple Wood", L'=', RGB(205,92,92), {TileTag::ITEM, TileTag::WOOD}, TileType::EMPTY };
    TILE_DATA[TileType::PALM_WOOD] = { L"Palm Wood", L'=', RGB(238,221,130), {TileTag::ITEM, TileTag::WOOD}, TileType::EMPTY };
    TILE_DATA[TileType::TEAK_WOOD] = { L"Teak Wood", L'=', RGB(165,42,42), {TileTag::ITEM, TileTag::WOOD}, TileType::EMPTY };
    TILE_DATA[TileType::SAGUARO_WOOD] = { L"Saguaro Wood", L'=', RGB(152,251,152), {TileTag::ITEM, TileTag::WOOD}, TileType::EMPTY };
    TILE_DATA[TileType::PRICKLYPEAR_WOOD] = { L"Prickly Pear Wood", L'=', RGB(84, 140, 61), {TileTag::ITEM, TileTag::WOOD}, TileType::EMPTY };
    TILE_DATA[TileType::CHOLLA_WOOD] = { L"Cholla Wood", L'=', RGB(141, 120, 81), {TileTag::ITEM, TileTag::WOOD}, TileType::EMPTY };

    // Refined Metals

    TILE_DATA[TileType::COPPER_METAL] = { L"Copper", L'I', RGB(184,115,51), {TileTag::ITEM, TileTag::METAL, TileTag::MINERAL}, TileType::EMPTY, 0.5f, 1.0f, TileType::EMPTY, L"Cu" };
    TILE_DATA[TileType::TIN_METAL] = { L"Tin", L'I', RGB(192,192,192), {TileTag::ITEM, TileTag::METAL, TileTag::MINERAL}, TileType::EMPTY, 0.6f, 1.1f, TileType::EMPTY, L"Sn" };
    TILE_DATA[TileType::NICKEL_METAL] = { L"Nickel", L'I', RGB(150,150,150), {TileTag::ITEM, TileTag::METAL, TileTag::MINERAL}, TileType::EMPTY, 0.8f, 1.5f, TileType::EMPTY, L"Ni" };
    TILE_DATA[TileType::ZINC_METAL] = { L"Zinc", L'I', RGB(175,175,175), {TileTag::ITEM, TileTag::METAL, TileTag::MINERAL}, TileType::EMPTY, 0.7f, 1.4f, TileType::EMPTY, L"Zn" };
    TILE_DATA[TileType::IRON_METAL] = { L"Iron", L'I', RGB(100,100,100), {TileTag::ITEM, TileTag::METAL, TileTag::MINERAL}, TileType::EMPTY, 1.0f, 1.8f, TileType::EMPTY, L"Fe" };
    TILE_DATA[TileType::LEAD_METAL] = { L"Lead", L'I', RGB(70,80,90), {TileTag::ITEM, TileTag::METAL, TileTag::MINERAL}, TileType::EMPTY, 0.9f, 1.6f, TileType::EMPTY, L"Pb" };
    TILE_DATA[TileType::SILVER_METAL] = { L"Silver", L'I', RGB(220,220,220), {TileTag::ITEM, TileTag::METAL, TileTag::MINERAL}, TileType::EMPTY, 1.2f, 2.5f, TileType::EMPTY, L"Ag" };
    TILE_DATA[TileType::TUNGSTEN_METAL] = { L"Tungsten", L'I', RGB(80,90,100), {TileTag::ITEM, TileTag::METAL, TileTag::MINERAL}, TileType::EMPTY, 1.5f, 2.8f, TileType::EMPTY, L"W" };
    TILE_DATA[TileType::GOLD_METAL] = { L"Gold", L'I', RGB(255,215,0), {TileTag::ITEM, TileTag::METAL, TileTag::MINERAL}, TileType::EMPTY, 1.8f, 4.0f, TileType::EMPTY, L"Au" };
    TILE_DATA[TileType::PLATINUM_METAL] = { L"Platinum", L'I', RGB(230,230,230), {TileTag::ITEM, TileTag::METAL, TileTag::MINERAL}, TileType::EMPTY, 2.0f, 4.5f, TileType::EMPTY, L"Pt" };
    TILE_DATA[TileType::ALUMINUM_METAL] = { L"Aluminum", L'I', RGB(180,190,200), {TileTag::ITEM, TileTag::METAL, TileTag::MINERAL}, TileType::EMPTY, 1.1f, 2.2f, TileType::EMPTY, L"Al" };
    TILE_DATA[TileType::CHROMIUM_METAL] = { L"Chromium", L'I', RGB(110,120,130), {TileTag::ITEM, TileTag::METAL, TileTag::MINERAL}, TileType::EMPTY, 1.3f, 2.4f, TileType::EMPTY, L"Cr" };
    TILE_DATA[TileType::BISMUTH_METAL] = { L"Bismuth", L'I', RGB(100,200,200), {TileTag::ITEM, TileTag::METAL, TileTag::MINERAL}, TileType::EMPTY, 1.4f, 3.5f, TileType::EMPTY, L"Bi" };
    TILE_DATA[TileType::RHODIUM_METAL] = { L"Rhodium", L'I', RGB(200,100,150), {TileTag::ITEM, TileTag::METAL, TileTag::MINERAL}, TileType::EMPTY, 2.5f, 5.5f, TileType::EMPTY, L"Rh" };
    TILE_DATA[TileType::OSMIUM_METAL] = { L"Osmium", L'I', RGB(80,80,80), {TileTag::ITEM, TileTag::METAL, TileTag::MINERAL}, TileType::EMPTY, 2.7f, 6.0f, TileType::EMPTY, L"Os" };
    TILE_DATA[TileType::IRIDIUM_METAL] = { L"Iridium", L'I', RGB(120,120,120), {TileTag::ITEM, TileTag::METAL, TileTag::MINERAL}, TileType::EMPTY, 2.8f, 6.5f, TileType::EMPTY, L"Ir" };
    TILE_DATA[TileType::COBALT_METAL] = { L"Cobalt", L'I', RGB(50,50,200), {TileTag::ITEM, TileTag::METAL, TileTag::MINERAL}, TileType::EMPTY, 2.1f, 4.2f, TileType::EMPTY, L"Co" };
    TILE_DATA[TileType::PALLADIUM_METAL] = { L"Palladium", L'I', RGB(180,180,180), {TileTag::ITEM, TileTag::METAL, TileTag::MINERAL}, TileType::EMPTY, 2.2f, 4.8f, TileType::EMPTY, L"Pd" };
    TILE_DATA[TileType::MITHRIL_METAL] = { L"Mithril", L'I', RGB(100,200,255), {TileTag::ITEM, TileTag::METAL, TileTag::MINERAL}, TileType::EMPTY, 3.0f, 9.0f, TileType::EMPTY, L"M" };
    TILE_DATA[TileType::ORICHALCUM_METAL] = { L"Orichalcum", L'I', RGB(255,140,0), {TileTag::ITEM, TileTag::METAL, TileTag::MINERAL}, TileType::EMPTY, 3.2f, 9.5f, TileType::EMPTY, L"Or" };
    TILE_DATA[TileType::ADAMANTIUM_METAL] = { L"Adamantium", L'I', RGB(255,0,0), {TileTag::ITEM, TileTag::METAL, TileTag::MINERAL}, TileType::EMPTY, 4.0f, 12.0f, TileType::EMPTY, L"Ad" };
    TILE_DATA[TileType::TITANIUM_METAL] = { L"Titanium", L'I', RGB(150,150,160), {TileTag::ITEM, TileTag::METAL, TileTag::MINERAL}, TileType::EMPTY, 3.8f, 11.5f, TileType::EMPTY, L"Ti" };

    // Ores

    TILE_DATA[TileType::BISMUTHINITE_ORE] = { L"Bismuthinite", L'o', RGB(70,160,160), {TileTag::ITEM, TileTag::ORE, TileTag::MINERAL}, TileType::BISMUTH_METAL, 3.0f, 1.7f };
    TILE_DATA[TileType::CASSITERITE_ORE] = { L"Cassiterite", L'o', RGB(150,120,90), {TileTag::ITEM, TileTag::ORE, TileTag::MINERAL}, TileType::TIN_METAL, 2.5f, 0.6f };
    TILE_DATA[TileType::NATIVE_COPPER_ORE] = { L"Native Copper", L'o', RGB(205,92,92), {TileTag::ITEM, TileTag::ORE, TileTag::MINERAL}, TileType::COPPER_METAL, 2.0f, 0.5f };
    TILE_DATA[TileType::GALENA_ORE] = { L"Galena", L'o', RGB(90,100,110), {TileTag::ITEM, TileTag::ORE, TileTag::MINERAL}, TileType::LEAD_METAL, 3.0f, 0.8f }; // Could drop silver too, but simplify for now
    TILE_DATA[TileType::GARNIERITE_ORE] = { L"Garnierite", L'o', RGB(100,140,100), {TileTag::ITEM, TileTag::ORE, TileTag::MINERAL}, TileType::NICKEL_METAL, 2.8f, 0.7f };
    TILE_DATA[TileType::NATIVE_GOLD_ORE] = { L"Native Gold", L'o', RGB(255,223,0), {TileTag::ITEM, TileTag::ORE, TileTag::MINERAL}, TileType::GOLD_METAL, 4.0f, 2.0f };
    TILE_DATA[TileType::HEMATITE_ORE] = { L"Hematite", L'o', RGB(100,50,50), {TileTag::ITEM, TileTag::ORE, TileTag::MINERAL}, TileType::IRON_METAL, 3.5f, 0.9f };
    TILE_DATA[TileType::HORN_SILVER_ORE] = { L"Horn Silver", L'o', RGB(200,200,200), {TileTag::ITEM, TileTag::ORE, TileTag::MINERAL}, TileType::SILVER_METAL, 3.0f, 1.2f };
    TILE_DATA[TileType::LIMONITE_ORE] = { L"Limonite", L'o', RGB(150,100,50), {TileTag::ITEM, TileTag::ORE, TileTag::MINERAL}, TileType::IRON_METAL, 3.2f, 0.8f };
    TILE_DATA[TileType::MAGNETITE_ORE] = { L"Magnetite", L'o', RGB(50,50,50), {TileTag::ITEM, TileTag::ORE, TileTag::MINERAL}, TileType::IRON_METAL, 3.8f, 1.0f };
    TILE_DATA[TileType::MALACHITE_ORE] = { L"Malachite", L'o', RGB(0,180,0), {TileTag::ITEM, TileTag::ORE, TileTag::MINERAL}, TileType::COPPER_METAL, 2.2f, 0.55f };
    TILE_DATA[TileType::NATIVE_PLATINUM_ORE] = { L"Native Platinum", L'o', RGB(200,200,200), {TileTag::ITEM, TileTag::ORE, TileTag::MINERAL}, TileType::PLATINUM_METAL, 4.5f, 2.2f };
    TILE_DATA[TileType::NATIVE_SILVER_ORE] = { L"Native Silver", L'o', RGB(230,230,230), {TileTag::ITEM, TileTag::ORE, TileTag::MINERAL}, TileType::SILVER_METAL, 3.0f, 1.0f };
    TILE_DATA[TileType::SPHALERITE_ORE] = { L"Sphalerite", L'o', RGB(180,140,80), {TileTag::ITEM, TileTag::ORE, TileTag::MINERAL}, TileType::ZINC_METAL, 2.7f, 0.75f };
    TILE_DATA[TileType::TETRAHEDRITE_ORE] = { L"Tetrahedrite", L'o', RGB(150,90,90), {TileTag::ITEM, TileTag::ORE, TileTag::MINERAL}, TileType::COPPER_METAL, 2.3f, 0.65f }; // Also drops copper, ignore silver for now
    TILE_DATA[TileType::NATIVE_ALUMINUM_ORE] = { L"Native Aluminum", L'o', RGB(200,210,220), {TileTag::ITEM, TileTag::ORE, TileTag::MINERAL}, TileType::ALUMINUM_METAL, 2.9f, 1.1f };
    TILE_DATA[TileType::ADAMANTITE_ORE] = { L"Adamantite", L'o', RGB(200,0,0), {TileTag::ITEM, TileTag::ORE, TileTag::MINERAL}, TileType::ADAMANTIUM_METAL, 7.0f, 6.0f };
    TILE_DATA[TileType::WOLFRAMITE_ORE] = { L"Wolframite", L'o', RGB(70,80,90), {TileTag::ITEM, TileTag::ORE, TileTag::MINERAL}, TileType::TUNGSTEN_METAL, 3.2f, 1.4f };
    TILE_DATA[TileType::SPERRYLITE_ORE] = { L"Sperrylite", L'o', RGB(180,180,180), {TileTag::ITEM, TileTag::ORE, TileTag::MINERAL}, TileType::PLATINUM_METAL, 4.8f, 2.4f };
    TILE_DATA[TileType::IRIDOSMINE_ORE] = { L"Iridosmine", L'o', RGB(100,100,100), {TileTag::ITEM, TileTag::ORE, TileTag::MINERAL}, TileType::IRIDIUM_METAL, 5.0f, 3.2f };
    TILE_DATA[TileType::COBALTITE_ORE] = { L"Cobaltite", L'o', RGB(30,30,150), {TileTag::ITEM, TileTag::ORE, TileTag::MINERAL}, TileType::COBALT_METAL, 4.0f, 2.1f };
    TILE_DATA[TileType::PALLADINITE_ORE] = { L"Palladinite", L'o', RGB(160,160,160), {TileTag::ITEM, TileTag::ORE, TileTag::MINERAL}, TileType::PALLADIUM_METAL, 4.2f, 2.4f };
    TILE_DATA[TileType::MITHRITE_ORE] = { L"Mithrite", L'o', RGB(50,150,200), {TileTag::ITEM, TileTag::ORE, TileTag::MINERAL}, TileType::MITHRIL_METAL, 6.5f, 4.5f };
    TILE_DATA[TileType::ORICALCITE_ORE] = { L"Oricalcite", L'o', RGB(200,100,0), {TileTag::ITEM, TileTag::ORE, TileTag::MINERAL}, TileType::ORICHALCUM_METAL, 6.8f, 4.7f };
    TILE_DATA[TileType::RUTILE_ORE] = { L"Rutile", L'o', RGB(120,120,130), {TileTag::ITEM, TileTag::ORE, TileTag::MINERAL}, TileType::TITANIUM_METAL, 6.0f, 5.5f };

    // Architect Placeables
    TILE_DATA[TileType::BLUEPRINT] = { L"Construction Site", L'x', RGB(0, 200, 255), {TileTag::BLUEPRINT_TAG}, TileType::EMPTY };
    TILE_DATA[TileType::WALL] = { L"Wall", L'█', RGB(139,69,19), {TileTag::STRUCTURE}, TileType::EMPTY }; // MODIFIED: Color changed to imply wood
    TILE_DATA[TileType::STAIR_UP] = { L"Stair Up", L'<', RGB(200, 200, 200), {TileTag::STRUCTURE}, TileType::EMPTY };
    TILE_DATA[TileType::STAIR_DOWN] = { L"Stair Down", L'>', RGB(200, 200, 200), {TileTag::STRUCTURE}, TileType::EMPTY };
    TILE_DATA[TileType::STONE_WALL] = { L"Stone Wall", L'█', RGB(120, 120, 120), {TileTag::STRUCTURE}, TileType::EMPTY }; // NEW
    TILE_DATA[TileType::CHAIR] = { L"Chair", L'h', RGB(139,69,19), {TileTag::FURNITURE}, TileType::EMPTY };
    TILE_DATA[TileType::TABLE] = { L"Table", L'π', RGB(139,69,19), {TileTag::FURNITURE}, TileType::EMPTY };
    TILE_DATA[TileType::TORCH] = { L"Torch", L'!', RGB(255,165,0), {TileTag::LIGHTS}, TileType::EMPTY };
    TILE_DATA[TileType::RESEARCH_BENCH] = { L"Research Bench", L'R', RGB(180,180,220), {TileTag::PRODUCTION}, TileType::EMPTY };
    TILE_DATA[TileType::CARPENTRY_WORKBENCH] = { L"Carpentry Workbench", L'b', RGB(184, 115, 51), {TileTag::PRODUCTION}, TileType::EMPTY };
    TILE_DATA[TileType::STONECUTTING_TABLE] = { L"Stonecutting Table", L'b', RGB(150, 150, 150), {TileTag::PRODUCTION}, TileType::EMPTY };
    TILE_DATA[TileType::SMITHY] = { L"Smithy", L'b', RGB(100, 100, 100), {TileTag::PRODUCTION}, TileType::EMPTY };
    TILE_DATA[TileType::BLAST_FURNACE] = { L"Blast Furnace", L'B', RGB(255, 140, 0), {TileTag::PRODUCTION}, TileType::EMPTY };
    TILE_DATA[TileType::ADVANCED_RESEARCH_BENCH] = { L"Adv. Research Bench", L'R', RGB(150, 220, 255), {TileTag::PRODUCTION}, TileType::EMPTY };
    TILE_DATA[TileType::GROWING_ZONE] = { L"Growing Zone", L'\"', RGB(100, 150, 0), {TileTag::STRUCTURE}, TileType::EMPTY };
    TILE_DATA[TileType::MINE_SHAFT] = { L"Mine Shaft", L'M', RGB(80, 80, 80), {TileTag::STRUCTURE, TileTag::PRODUCTION}, TileType::EMPTY };
    TILE_DATA[TileType::WINDMILL] = { L"Windmill", L'W', RGB(150, 150, 100), {TileTag::STRUCTURE, TileTag::PRODUCTION}, TileType::EMPTY };
    TILE_DATA[TileType::COINING_MILL] = { L"Coining Mill", L'C', RGB(180, 180, 180), {TileTag::PRODUCTION}, TileType::EMPTY };
    TILE_DATA[TileType::THEATRE_STAGE] = { L"Theatre Stage", L'§', RGB(120, 80, 50), {TileTag::FURNITURE}, TileType::EMPTY };
    TILE_DATA[TileType::DRUG_LAB] = { L"Drug Lab", L'L', RGB(100, 200, 100), {TileTag::PRODUCTION}, TileType::EMPTY };
    TILE_DATA[TileType::LIGHTHOUSE_BUILDING] = { L"Lighthouse", L'▲', RGB(200, 200, 200), {TileTag::STRUCTURE, TileTag::LIGHTS}, TileType::EMPTY };
    TILE_DATA[TileType::PRINTING_PRESS_FURNITURE] = { L"Printing Press", L'P', RGB(100, 50, 20), {TileTag::FURNITURE, TileTag::PRODUCTION}, TileType::EMPTY };
    TILE_DATA[TileType::TYPEWRITER] = { L"Typewriter", L't', RGB(50, 50, 50), {TileTag::FURNITURE}, TileType::EMPTY };
    TILE_DATA[TileType::ASSEMBLY_LINE] = { L"Assembly Line", L'=', RGB(100, 100, 100), {TileTag::PRODUCTION}, TileType::EMPTY };
    TILE_DATA[TileType::RADIO_FURNITURE] = { L"Radio", L'®', RGB(70, 70, 70), {TileTag::FURNITURE, TileTag::LIGHTS}, TileType::EMPTY };
    TILE_DATA[TileType::COMPUTER_FURNITURE] = { L"Computer", L'☺', RGB(80, 80, 150), {TileTag::FURNITURE, TileTag::PRODUCTION}, TileType::EMPTY };
    TILE_DATA[TileType::WIND_TURBINE] = { L"Wind Turbine", L'T', RGB(150, 150, 150), {TileTag::STRUCTURE, TileTag::PRODUCTION}, TileType::EMPTY };
    TILE_DATA[TileType::SERVER] = { L"Server", L'§', RGB(60, 60, 60), {TileTag::STRUCTURE, TileTag::PRODUCTION}, TileType::EMPTY };
    TILE_DATA[TileType::SWITCH] = { L"Switch", L'S', RGB(60, 60, 60), {TileTag::STRUCTURE, TileTag::PRODUCTION}, TileType::EMPTY };
    TILE_DATA[TileType::ROUTER] = { L"Router", L'R', RGB(60, 60, 60), {TileTag::STRUCTURE, TileTag::PRODUCTION}, TileType::EMPTY };
    TILE_DATA[TileType::HOSPITAL_BED] = { L"Hospital Bed", L'H', RGB(200, 200, 255), {TileTag::FURNITURE}, TileType::EMPTY }; // NEW
    TILE_DATA[TileType::HYDROPONICS_BASIN] = { L"Hydroponics Basin", L'~', RGB(0, 150, 0), {TileTag::PRODUCTION}, TileType::EMPTY };







    TILE_DATA[TileType::PICKAXE] = { L"Pickaxe", L'/', RGB(150, 100, 50), {TileTag::ITEM}, TileType::EMPTY };
    TILE_DATA[TileType::RAFT] = { L"Raft", L'~', RGB(139, 100, 19), {TileTag::ITEM}, TileType::EMPTY };
    
    TILE_DATA[TileType::WHEEL] = { L"Wheel", L'o', RGB(100, 100, 100), {TileTag::ITEM}, TileType::EMPTY };
    TILE_DATA[TileType::COIN] = { L"Coin", L'$', RGB(255, 215, 0), {TileTag::ITEM}, TileType::EMPTY };
    TILE_DATA[TileType::LENS] = { L"Lens", L'o', RGB(173, 216, 230), {TileTag::ITEM}, TileType::EMPTY };
    TILE_DATA[TileType::GLASSES] = { L"Glasses", L'e', RGB(100, 100, 100), {TileTag::ITEM}, TileType::EMPTY };
    TILE_DATA[TileType::BOW] = { L"Bow", L'\\', RGB(139, 69, 19), {TileTag::ITEM}, TileType::EMPTY };
    TILE_DATA[TileType::ARROW] = { L"Arrow", L'|', RGB(150, 150, 150), {TileTag::ITEM}, TileType::EMPTY };
    TILE_DATA[TileType::KNIGHT_ARMOR] = { L"Knight Armor", L'[', RGB(120, 120, 120), {TileTag::ITEM}, TileType::EMPTY };
    TILE_DATA[TileType::KNIGHT_WEAPON] = { L"Knight Weapon", L'/', RGB(100, 100, 100), {TileTag::ITEM}, TileType::EMPTY };
    TILE_DATA[TileType::COMPASS_ITEM] = { L"Compass", L'¤', RGB(150, 150, 150), {TileTag::ITEM}, TileType::EMPTY };
    TILE_DATA[TileType::GUNPOWDER_ITEM] = { L"Gunpowder", L'/', RGB(50, 50, 50), {TileTag::ITEM}, TileType::EMPTY };
    TILE_DATA[TileType::WIRE] = { L"Wire", L'-', RGB(180, 0, 0), {TileTag::ITEM}, TileType::EMPTY };
    TILE_DATA[TileType::LIGHTBULB] = { L"Lightbulb", L'O', RGB(255, 255, 150), {TileTag::ITEM}, TileType::EMPTY };
    TILE_DATA[TileType::CHEMICALS_ITEM] = { L"Chemicals", L'~', RGB(100, 100, 200), {TileTag::ITEM}, TileType::EMPTY };
    TILE_DATA[TileType::DRUGS_ITEM] = { L"Drugs", L'?', RGB(200, 50, 200), {TileTag::ITEM}, TileType::EMPTY };
    TILE_DATA[TileType::BOAT_ITEM] = { L"Boat", L'=', RGB(100, 70, 40), {TileTag::ITEM}, TileType::EMPTY };
    TILE_DATA[TileType::SMARTPHONE] = { L"Smartphone", L'📱', RGB(0,0,0), {TileTag::ITEM}, TileType::EMPTY };
    TILE_DATA[TileType::TABLET] = { L"Tablet", L'💻', RGB(0,0,0), {TileTag::ITEM}, TileType::EMPTY };
    TILE_DATA[TileType::SMARTWATCH] = { L"Smartwatch", L'⌚', RGB(0,0,0), {TileTag::ITEM}, TileType::EMPTY };







    TILE_DATA[TileType::SPIKE_TRAP] = { L"Spike Trap", L'^', RGB(100, 50, 20), {TileTag::SECURITY, TileTag::STRUCTURE}, TileType::EMPTY };
    TILE_DATA[TileType::COLUMN] = { L"Column", L'|', RGB(150, 150, 150), {TileTag::STRUCTURE}, TileType::EMPTY };
    TILE_DATA[TileType::TELESCOPE_FURNITURE] = { L"Telescope", L'T', RGB(80, 80, 80), {TileTag::FURNITURE}, TileType::EMPTY };
    TILE_DATA[TileType::BLACKBOARD_FURNITURE] = { L"Blackboard", L'#', RGB(50, 50, 50), {TileTag::FURNITURE}, TileType::EMPTY };
    TILE_DATA[TileType::SCHOOL_CHAIR_FURNITURE] = { L"School Chair", L'h', RGB(100, 70, 40), {TileTag::FURNITURE}, TileType::EMPTY };
    TILE_DATA[TileType::SCHOOL_DESK_FURNITURE] = { L"School Desk", L'π', RGB(100, 70, 40), {TileTag::FURNITURE}, TileType::EMPTY };
    TILE_DATA[TileType::ACOUSTIC_WALL_ITEM] = { L"Acoustic Wall", L'#', RGB(80, 80, 80), {TileTag::STRUCTURE}, TileType::EMPTY };






    // Unlocks for research
    std::set<TileType> g_unlockedBuildings;

    // Biome Data
    BIOME_DATA[Biome::OCEAN] = { L"Ocean", RGB(0,0,139) };
    BIOME_DATA[Biome::TUNDRA] = { L"Tundra", RGB(224,255,255) };
    BIOME_DATA[Biome::BOREAL_FOREST] = { L"Boreal Forest", RGB(0,100,0) };
    BIOME_DATA[Biome::TEMPERATE_FOREST] = { L"Temperate Forest", RGB(34,139,34) };
    BIOME_DATA[Biome::JUNGLE] = { L"Jungle", RGB(0,80,0) };
    BIOME_DATA[Biome::DESERT] = { L"Desert", RGB(244,164,96) };

    initResearchData();
    initPawnData();
    initCritterData();
    initBiomeCritterSpawns();

    // NEW: Helper vectors for ore generation
    std::vector<TileType> sedimentaryStones = { TileType::CHALK, TileType::CHERT, TileType::CLAYSTONE, TileType::CONGLOMERATE, TileType::DOLOMITE, TileType::LIMESTONE, TileType::MUDSTONE, TileType::ROCK_SALT, TileType::SANDSTONE, TileType::SHALE, TileType::SILTSTONE };
    std::vector<TileType> igneousExtrusiveStones = { TileType::ANDESITE, TileType::BASALT, TileType::DACITE, TileType::OBSIDIAN, TileType::RHYOLITE };
    std::vector<TileType> igneousIntrusiveStones = { TileType::DIORITE, TileType::GABBRO, TileType::GRANITE };
    std::vector<TileType> metamorphicStones = { TileType::GNEISS, TileType::MARBLE, TileType::PHYLLITE, TileType::QUARTZITE, TileType::SCHIST, TileType::SLATE };

    std::vector<TileType> allIgneousStones;
    allIgneousStones.insert(allIgneousStones.end(), igneousExtrusiveStones.begin(), igneousExtrusiveStones.end());
    allIgneousStones.insert(allIgneousStones.end(), igneousIntrusiveStones.begin(), igneousIntrusiveStones.end());

    std::vector<TileType> allStones;
    allStones.insert(allStones.end(), sedimentaryStones.begin(), sedimentaryStones.end());
    allStones.insert(allStones.end(), igneousExtrusiveStones.begin(), igneousExtrusiveStones.end());
    allStones.insert(allStones.end(), igneousIntrusiveStones.begin(), igneousIntrusiveStones.end());
    allStones.insert(allStones.end(), metamorphicStones.begin(), metamorphicStones.end());

    // NEW: Populate g_tagNames (for stockpile UI categories)
    g_tagNames.clear();
    g_tagNames[TileTag::WOOD] = L"Woods";
    g_tagNames[TileTag::SEDIMENTARY] = L"Sedimentary Rocks";
    g_tagNames[TileTag::IGNEOUS_INTRUSIVE] = L"Igneous Intrusive Rocks";
    g_tagNames[TileTag::IGNEOUS_EXTRUSIVE] = L"Igneous Extrusive Rocks";
    g_tagNames[TileTag::METAMORPHIC] = L"Metamorphic Rocks";
    g_tagNames[TileTag::INNER_STONE] = L"Core Stones";
    g_tagNames[TileTag::CHUNK] = L"Chunks (Generic)"; // Used for STONE_CHUNK specifically
    g_tagNames[TileTag::MINERAL] = L"Minerals"; // For things like raw ores/gems if added
    g_tagNames[TileTag::SOIL] = L"Soils"; // For loose dirt, sand if haulable
    // NEW: Metals and Ores
    g_tagNames[TileTag::METAL] = L"Metals (Refined)";
    g_tagNames[TileTag::ORE] = L"Ores (Raw)";


    TILE_WORLD_DEPTH = 0;
    BIOSPHERE_Z_LEVEL = 0;
    HYDROSPHERE_Z_LEVEL = 0;
    ATMOSPHERE_TOP_Z = 0;
    TROPOSPHERE_TOP_Z_LEVEL = 0;
    currentZ = 0;

    // Z-Level Calculation
    TILE_WORLD_DEPTH = 0; BIOSPHERE_Z_LEVEL = 0; ATMOSPHERE_TOP_Z = 0; HYDROSPHERE_Z_LEVEL = 0; // <-- Add reset here
    
    int z_accumulator = 0;
    for (const auto& stratum : strataDefinition) {
        if (stratum.type == Stratum::BIOSPHERE) BIOSPHERE_Z_LEVEL = z_accumulator;
        if (stratum.type == Stratum::HYDROSPHERE) HYDROSPHERE_Z_LEVEL = z_accumulator;
        if (stratum.type == Stratum::TROPOSPHERE) TROPOSPHERE_TOP_Z_LEVEL = z_accumulator + stratum.depth - 1; // Top *inclusive* Z-level of troposphere
        // ATMOSPHERE_TOP_Z should be the very highest point, considering all atmospheric layers, which is typically the last stratum if it's atmospheric.
        // Assuming Exosphere is the top-most defined atmospheric stratum for ATMOSPHERE_TOP_Z
        if (stratum.type == Stratum::EXOSPHERE) ATMOSPHERE_TOP_Z = z_accumulator + stratum.depth; // Top *exclusive* bound of highest atmospheric layer
        z_accumulator += stratum.depth;
    }
    TILE_WORLD_DEPTH = z_accumulator; // Total world depth
    currentZ = BIOSPHERE_Z_LEVEL;

    // Populate the UNIFIED spawn list for the debug menu ---
    g_spawnMenuList.clear();

    // 1. Add Tiles to the spawn list
    for (const auto& pair : TILE_DATA) {
        bool isStructure = std::find(pair.second.tags.begin(), pair.second.tags.end(), TileTag::STRUCTURE) != pair.second.tags.end();
        bool isFurniture = std::find(pair.second.tags.begin(), pair.second.tags.end(), TileTag::FURNITURE) != pair.second.tags.end();
        bool isLight = std::find(pair.second.tags.begin(), pair.second.tags.end(), TileTag::LIGHTS) != pair.second.tags.end();
        bool isProduction = std::find(pair.second.tags.begin(), pair.second.tags.end(), TileTag::PRODUCTION) != pair.second.tags.end();
        bool isTreePart = std::find(pair.second.tags.begin(), pair.second.tags.end(), TileTag::TREE_PART) != pair.second.tags.end();
        bool isTreeBase = (pair.first >= TileType::OAK && pair.first <= TileType::CHOLLA);

        if (pair.first != TileType::EMPTY && !isTreePart && !isTreeBase && !isStructure && !isFurniture && !isLight && !isProduction) {
            Spawnable s;
            s.type = SpawnableType::TILE;
            s.name = pair.second.name;
            s.tile_type = pair.first;
            g_spawnMenuList.push_back(s);
        }
    }

    // 2. Add Critters to the spawn list
    for (const auto& pair : g_CritterData) {
        Spawnable s;
        s.type = SpawnableType::CRITTER;
        s.name = pair.second.name;
        s.critter_type = pair.first;
        g_spawnMenuList.push_back(s);
    }


    // 3. Sort the final unified list by name for easy finding
    std::sort(g_spawnMenuList.begin(), g_spawnMenuList.end(),
        [](const Spawnable& a, const Spawnable& b) {
            return a.name < b.name;
        });

    scanForFonts();
    loadFontSelection();




    // NEW: Populate g_tagNames (for stockpile UI categories)
    g_tagNames.clear();
    g_tagNames[TileTag::WOOD] = L"Woods";
    g_tagNames[TileTag::SEDIMENTARY] = L"Sedimentary Rocks";
    g_tagNames[TileTag::IGNEOUS_INTRUSIVE] = L"Igneous Intrusive Rocks";
    g_tagNames[TileTag::IGNEOUS_EXTRUSIVE] = L"Igneous Extrusive Rocks";
    g_tagNames[TileTag::METAMORPHIC] = L"Metamorphic Rocks";
    g_tagNames[TileTag::INNER_STONE] = L"Core Stones";
    g_tagNames[TileTag::CHUNK] = L"Chunks (Generic)"; // Used for STONE_CHUNK specifically
    g_tagNames[TileTag::MINERAL] = L"Minerals"; // For things like raw ores/gems if added
    g_tagNames[TileTag::SOIL] = L"Soils"; // For loose dirt, sand if haulable
    // Add more as needed, e.g., METALS, FOOD, etc.

    // Populate g_haulableItemsGrouped
    g_haulableItemsGrouped.clear();
    for (const auto& pair : TILE_DATA) {
        TileType type = pair.first;
        const TileData& data = pair.second;

        // An item is haulable if it has the ITEM tag, AND is not a blueprint/structure/furniture/light/production building.
        bool isHaulableItem = false;
        if (std::find(data.tags.begin(), data.tags.end(), TileTag::ITEM) != data.tags.end()) {
            // Also ensure it's not a placed structure type that happens to have ITEM tag (e.g. if you mistakenly tagged WALL as ITEM)
            if (std::find(data.tags.begin(), data.tags.end(), TileTag::STRUCTURE) == data.tags.end() &&
                std::find(data.tags.begin(), data.tags.end(), TileTag::FURNITURE) == data.tags.end() &&
                std::find(data.tags.begin(), data.tags.end(), TileTag::LIGHTS) == data.tags.end() &&
                std::find(data.tags.begin(), data.tags.end(), TileTag::PRODUCTION) == data.tags.end() &&
                std::find(data.tags.begin(), data.tags.end(), TileTag::BLUEPRINT_TAG) == data.tags.end() &&
                std::find(data.tags.begin(), data.tags.end(), TileTag::STOCKPILE_ZONE) == data.tags.end())
            {
                isHaulableItem = true;
            }
        }

        // Also if it's a "drop" from a minable/choppable object, it should be haulable even if its base type
        // (e.g., OAK_WOOD) isn't explicitly tagged 'ITEM' (though it should be).
        // This logic makes sure dropped resources are always considered haulable.
        if (data.drops != TileType::EMPTY && data.drops != type) {
            if (TILE_DATA.count(data.drops) && std::find(TILE_DATA.at(data.drops).tags.begin(), TILE_DATA.at(data.drops).tags.end(), TileTag::ITEM) != TILE_DATA.at(data.drops).tags.end()) {
                isHaulableItem = true;
            }
        }

        if (isHaulableItem) {
            TileTag primaryTag = TileTag::NONE;
            bool foundSpecificTag = false;

            // Prioritize specific material tags
            if (std::find(data.tags.begin(), data.tags.end(), TileTag::WOOD) != data.tags.end()) { primaryTag = TileTag::WOOD; foundSpecificTag = true; }
            // NEW: Ores and Metals first for strong categorization
            else if (std::find(data.tags.begin(), data.tags.end(), TileTag::ORE) != data.tags.end()) { primaryTag = TileTag::ORE; foundSpecificTag = true; }
            else if (std::find(data.tags.begin(), data.tags.end(), TileTag::METAL) != data.tags.end()) { primaryTag = TileTag::METAL; foundSpecificTag = true; }
            else if (std::find(data.tags.begin(), data.tags.end(), TileTag::SEDIMENTARY) != data.tags.end()) { primaryTag = TileTag::SEDIMENTARY; foundSpecificTag = true; }
            else if (std::find(data.tags.begin(), data.tags.end(), TileTag::IGNEOUS_INTRUSIVE) != data.tags.end()) { primaryTag = TileTag::IGNEOUS_INTRUSIVE; foundSpecificTag = true; }
            else if (std::find(data.tags.begin(), data.tags.end(), TileTag::IGNEOUS_EXTRUSIVE) != data.tags.end()) { primaryTag = TileTag::IGNEOUS_EXTRUSIVE; foundSpecificTag = true; }
            else if (std::find(data.tags.begin(), data.tags.end(), TileTag::METAMORPHIC) != data.tags.end()) { primaryTag = TileTag::METAMORPHIC; foundSpecificTag = true; }
            else if (std::find(data.tags.begin(), data.tags.end(), TileTag::INNER_STONE) != data.tags.end()) { primaryTag = TileTag::INNER_STONE; foundSpecificTag = true; }
            else if (std::find(data.tags.begin(), data.tags.end(), TileTag::CHUNK) != data.tags.end()) { primaryTag = TileTag::CHUNK; foundSpecificTag = true; }
            else if (std::find(data.tags.begin(), data.tags.end(), TileTag::MINERAL) != data.tags.end()) { primaryTag = TileTag::MINERAL; foundSpecificTag = true; }
            else if (std::find(data.tags.begin(), data.tags.end(), TileTag::SOIL) != data.tags.end()) { primaryTag = TileTag::SOIL; foundSpecificTag = true; }

            if (foundSpecificTag && g_tagNames.count(primaryTag)) {
                g_haulableItemsGrouped[primaryTag].push_back(type);
            }
        }
    }

    // Sort items within each group by name
    for (auto& pair : g_haulableItemsGrouped) {
        std::sort(pair.second.begin(), pair.second.end(),
            [](TileType a, TileType b) { return TILE_DATA.at(a).name < TILE_DATA.at(b).name; });
    }

    // Determine the ordered list of categories for display
    stockpilePanel_displayCategoriesOrder = {
        TileTag::WOOD,
        TileTag::METAL, // NEW: Metals category
        TileTag::ORE,   // NEW: Ores category
        TileTag::SEDIMENTARY,
        TileTag::IGNEOUS_INTRUSIVE,
        TileTag::IGNEOUS_EXTRUSIVE,
        TileTag::METAMORPHIC,
        TileTag::INNER_STONE,
        TileTag::CHUNK,
        TileTag::MINERAL,
        //TileTag::SOIL, // Uncomment if you want to haul loose soil/sand
        //TileTag::FLUID, // Uncomment if you want to haul liquids
    };

    // Add any categories from g_haulableItemsGrouped that were not explicitly ordered
    // This ensures all haulable items defined eventually have a place in the UI.
    for (const auto& pair : g_haulableItemsGrouped) {
        bool found = false;
        for (TileTag orderedTag : stockpilePanel_displayCategoriesOrder) {
            if (orderedTag == pair.first) {
                found = true;
                break;
            }
        }
        if (!found) {
            stockpilePanel_displayCategoriesOrder.push_back(pair.first);
        }
    }

    // Initialize all categories to expanded
    for (TileTag tag : stockpilePanel_displayCategoriesOrder) {
        stockpilePanel_categoryExpanded[tag] = true;
    }
}





// --- Function to Scan for .ttf Files ---
void scanForFonts() {
    g_availableFonts.clear();
    g_availableFonts.push_back(L"(Default)"); // Always have the default option

    // Create the Fonts directory if it doesn't exist
    if (!CreateDirectory(L"Fonts", NULL)) {
        if (GetLastError() != ERROR_ALREADY_EXISTS) {
            // Optional: Handle error if directory couldn't be created
            return;
        }
    }

    WIN32_FIND_DATAW findData;
    HANDLE hFind = FindFirstFileW(L"Fonts\\*.ttf", &findData);

    if (hFind != INVALID_HANDLE_VALUE) {
        do {
            // Check if it's a file and not a directory
            if (!(findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
                std::wstring filename = findData.cFileName;
                // Remove the ".ttf" extension to get the font name
                size_t pos = filename.rfind(L".ttf");
                if (pos != std::wstring::npos) {
                    filename.erase(pos, 4);
                    g_availableFonts.push_back(filename);
                }
            }
        } while (FindNextFileW(hFind, &findData) != 0);
        FindClose(hFind);
    }
}

void initPawnData() {
    g_Backstories.clear();
    g_SkillDescriptions.clear();

    g_Backstories[L"Survivor"] = { L"Survivor", L"{name} has endured hardships that would break most. This has made them resilient and resourceful, but also wary of others." };
    g_Backstories[L"Cave Explorer"] = { L"Cave Explorer", L"Years spent navigating treacherous underground networks have made {name} nimble and an expert at seeing value in stone." };
    g_Backstories[L"Starship Engineer"] = { L"Starship Engineer", L"{name} maintained the delicate systems of a void-faring vessel. Highly intelligent and a fast learner, {name} is not accustomed to manual labor." };
    g_Backstories[L"Geneticist"] = { L"Geneticist", L"A brilliant scientist who unlocked the secrets of the genome. Research comes naturally to {name}, who also has a basic understanding of medicine." };
    g_Backstories[L"Farmer"] = { L"Farmer", L"On a frontier world, {name} cultivated crops from dawn till dusk. A lifetime of hard work has made them industrious, though not particularly book-smart." };
    g_Backstories[L"Soldier"] = { L"Soldier", L"{name} was trained for combat and survival in harsh environments. Tough and disciplined, but finds intellectual pursuits challenging." };
    g_Backstories[L"Artisan"] = { L"Artisan", L"{name} was a celebrated creator, known for crafting beautiful and functional items. They are skilled with their hands but have little interest in heavy labor or academic pursuits." };
    g_Backstories[L"Medic"] = { L"Medic", L"{name} served as a field medic, treating wounds under extreme pressure. They have a deep understanding of health but are squeamish about violence." };
    g_Backstories[L"Prospector"] = { L"Prospector", L"Always chasing the next big find, {name} spent years surveying desolate asteroids. This lonely work honed {name}'s mining skills and resilience." };
    g_Backstories[L"Bureaucrat"] = { L"Bureaucrat", L"{name} navigated the endless paperwork of a galactic empire. While not skilled in any practical sense, {name} is a surprisingly fast researcher and gets along with most people." };

    g_SkillDescriptions[L"Mining"] = L"The ability to dig into rock and extract useful minerals and stone blocks. Higher skill levels yield more resources per rock.";
    g_SkillDescriptions[L"Chopping"] = L"Felling trees and cacti to gather wood. A higher skill means faster chopping.";
    g_SkillDescriptions[L"Farming"] = L"Sowing and harvesting crops. Skilled farmers have a greater chance to yield more food per plant.";
    g_SkillDescriptions[L"Construction"] = L"The ability to build structures and furniture from blueprints. Skilled constructors build faster and occasionally produce higher quality items.";
    g_SkillDescriptions[L"Hauling"] = L"The task of moving resources and items around the colony. Not a skill, but a labor priority.";
    g_SkillDescriptions[L"Cooking"] = L"Preparing raw ingredients into meals. Higher skill produces better meals that provide more happiness.";
    g_SkillDescriptions[L"Hunting"] = L"The art of tracking and killing wild animals for food and resources. Not yet implemented.";
    g_SkillDescriptions[L"Research"] = L"The ability to perform scientific research at a research bench. Higher skill contributes more research points per day.";
    g_SkillDescriptions[L"Deconstruction"] = L"Tearing down built structures and furniture. Uses the Construction skill. Higher skill works faster and may recover more resources.";
}

void initCritterData() {
    g_CritterData.clear();

    // The critter's tags

    // Main Groups
    g_CritterTagNames[CritterTag::NONE] = L"None";
    g_CritterTagNames[CritterTag::MAMMAL] = L"Mammal";
    g_CritterTagNames[CritterTag::MARINE_MAMMAL] = L"Marine Mammal";
    g_CritterTagNames[CritterTag::BIRD] = L"Bird";
    g_CritterTagNames[CritterTag::INSECT] = L"Insect";
    g_CritterTagNames[CritterTag::RODENT] = L"Rodent";
    g_CritterTagNames[CritterTag::REPTILE] = L"Reptile";
    g_CritterTagNames[CritterTag::ARACHNID] = L"Arachnid";
    g_CritterTagNames[CritterTag::CRUSTACEAN] = L"Crustacean";
    g_CritterTagNames[CritterTag::CNIDARIAN] = L"Cnidarian";
    g_CritterTagNames[CritterTag::AMPHIBIAN] = L"Amphibian";
    g_CritterTagNames[CritterTag::FISH] = L"Fish";
    g_CritterTagNames[CritterTag::CRYPTID] = L"Cryptid";
    g_CritterTagNames[CritterTag::YOUKAI] = L"Youkai";
    g_CritterTagNames[CritterTag::MYTHICAL] = L"Mythical";
    g_CritterTagNames[CritterTag::BEAST] = L"Beast";
    g_CritterTagNames[CritterTag::MEGAFAUNA] = L"Megafauna";
    g_CritterTagNames[CritterTag::CELESTIAL] = L"Celestial";
    g_CritterTagNames[CritterTag::DEMON] = L"Demon";
    g_CritterTagNames[CritterTag::ABERRATION] = L"Aberration";
    g_CritterTagNames[CritterTag::ENTITY] = L"Entity";
    // Sub-Tags
    g_CritterTagNames[CritterTag::LIVESTOCK] = L"Livestock";
    g_CritterTagNames[CritterTag::DOMESTIC] = L"Domestic";
    g_CritterTagNames[CritterTag::UNDEAD] = L"Undead";
    g_CritterTagNames[CritterTag::PRIMATE] = L"Primate";
    g_CritterTagNames[CritterTag::HUMANOID] = L"Humanoid";
    g_CritterTagNames[CritterTag::MARSUPIAL] = L"Marsupial";
    g_CritterTagNames[CritterTag::MOLLUSK] = L"Mollusk";
    g_CritterTagNames[CritterTag::ANNELID] = L"Annelid";
    g_CritterTagNames[CritterTag::DINOSAUR] = L"Dinosaur";
    g_CritterTagNames[CritterTag::DRAGONOID] = L"Dragonoid";
    g_CritterTagNames[CritterTag::FAE] = L"Fae";
    g_CritterTagNames[CritterTag::CONSTRUCT] = L"Construct";
    g_CritterTagNames[CritterTag::CHIMERA] = L"Chimera";
    g_CritterTagNames[CritterTag::GIANT] = L"Giant";
    g_CritterTagNames[CritterTag::PLANTFOLK] = L"Plantfolk";
    g_CritterTagNames[CritterTag::CHIROPTERA] = L"Chiroptera";
    g_CritterTagNames[CritterTag::CETACEAN] = L"Cetacean";
    g_CritterTagNames[CritterTag::MUSTELID] = L"Mustelid";
    g_CritterTagNames[CritterTag::PACHYDERM] = L"Pachyderm";
    // Sub-Sub-Tags (Behavior/Attribute)
    g_CritterTagNames[CritterTag::PREDATOR] = L"Predator";
    g_CritterTagNames[CritterTag::VERMIN] = L"Vermin";
    g_CritterTagNames[CritterTag::SCAVENGER] = L"Scavenger";
    g_CritterTagNames[CritterTag::FLYING] = L"Flying";
    g_CritterTagNames[CritterTag::AQUATIC] = L"Aquatic";
    g_CritterTagNames[CritterTag::VENOMOUS] = L"Venomous";
    g_CritterTagNames[CritterTag::NOCTURNAL] = L"Nocturnal";
    g_CritterTagNames[CritterTag::MIGRATORY] = L"Migratory";
    g_CritterTagNames[CritterTag::PARASITIC] = L"Parasitic";
    g_CritterTagNames[CritterTag::PACK_ANIMAL] = L"Pack Animal";
    g_CritterTagNames[CritterTag::BURROWING] = L"Burrowing";
    g_CritterTagNames[CritterTag::DIURNAL] = L"Diurnal";
    g_CritterTagNames[CritterTag::SWARMING] = L"Swarming";
    g_CritterTagNames[CritterTag::PARTHENOGENIC] = L"Parthenogenic";
    g_CritterTagNames[CritterTag::SYNTHETIC] = L"Synthetic";
    g_CritterTagNames[CritterTag::MUTATED] = L"Mutated";
    g_CritterTagNames[CritterTag::MOUNT] = L"Mount";

    // The critters themselvew

    // Mammals
    g_CritterData[CritterType::DEER] = { L"Deer", L'd', RGB(184, 134, 11), {CritterTag::MAMMAL, CritterTag::DIURNAL}, 80 };
    g_CritterData[CritterType::WOLF] = { L"Wolf", L'w', RGB(169, 169, 169), {CritterTag::MAMMAL, CritterTag::PREDATOR, CritterTag::NOCTURNAL, CritterTag::PACK_ANIMAL}, 60 };
    g_CritterData[CritterType::PIG] = { L"Pig", L'p', RGB(255, 182, 193), {CritterTag::MAMMAL, CritterTag::LIVESTOCK, CritterTag::SCAVENGER}, 100 };
    g_CritterData[CritterType::COW] = { L"Cow", L'c', RGB(245, 245, 220), {CritterTag::MAMMAL, CritterTag::LIVESTOCK, CritterTag::PACK_ANIMAL}, 120 };
    g_CritterData[CritterType::SHEEP] = { L"Sheep", L's', RGB(230, 230, 230), {CritterTag::MAMMAL, CritterTag::LIVESTOCK, CritterTag::PACK_ANIMAL}, 110 };
    g_CritterData[CritterType::GOAT] = { L"Goat", L'g', RGB(210, 180, 140), {CritterTag::MAMMAL, CritterTag::LIVESTOCK}, 90 };
    g_CritterData[CritterType::CAT] = { L"Cat", L'c', RGB(150, 75, 0), {CritterTag::MAMMAL, CritterTag::DOMESTIC, CritterTag::PREDATOR}, 70 };
    g_CritterData[CritterType::DOG] = { L"Dog", L'd', RGB(139, 69, 19), {CritterTag::MAMMAL, CritterTag::DOMESTIC, CritterTag::PACK_ANIMAL}, 75 };
    g_CritterData[CritterType::MONKEY] = { L"Monkey", L'm', RGB(160, 82, 45), {CritterTag::MAMMAL, CritterTag::PRIMATE}, 50 };
    g_CritterData[CritterType::KANGAROO] = { L"Kangaroo", L'k', RGB(205, 133, 63), {CritterTag::MAMMAL, CritterTag::MARSUPIAL}, 85 };
    g_CritterData[CritterType::BEAR] = { L"Bear", L'B', RGB(139, 69, 19), {CritterTag::MAMMAL, CritterTag::PREDATOR, CritterTag::MEGAFAUNA}, 130 };

    // Rodents
    g_CritterData[CritterType::RAT] = { L"Rat", L'r', RGB(90, 90, 90), {CritterTag::RODENT, CritterTag::VERMIN, CritterTag::NOCTURNAL, CritterTag::SCAVENGER}, 50 };
    g_CritterData[CritterType::SQUIRREL] = { L"Squirrel", L's', RGB(150, 75, 0), {CritterTag::RODENT, CritterTag::DIURNAL}, 45 };

    // Marine Mammals
    g_CritterData[CritterType::DOLPHIN] = { L"Dolphin", L'd', RGB(0, 191, 255), {CritterTag::MARINE_MAMMAL, CritterTag::AQUATIC, CritterTag::CETACEAN}, 50 };
    g_CritterData[CritterType::WHALE] = { L"Whale", L'W', RGB(70, 130, 180), {CritterTag::MARINE_MAMMAL, CritterTag::AQUATIC, CritterTag::MEGAFAUNA, CritterTag::CETACEAN}, 200 };

    // Birds
    g_CritterData[CritterType::PIGEON] = { L"Pigeon", L'p', RGB(128, 128, 128), {CritterTag::BIRD, CritterTag::FLYING, CritterTag::VERMIN}, 40 };
    g_CritterData[CritterType::HAWK] = { L"Hawk", L'h', RGB(139, 69, 19), {CritterTag::BIRD, CritterTag::FLYING, CritterTag::PREDATOR}, 30 };

    // Fish
    g_CritterData[CritterType::TROUT] = { L"Trout", L'f', RGB(192, 192, 192), {CritterTag::FISH, CritterTag::AQUATIC}, 60 };
    g_CritterData[CritterType::TUNA] = { L"Tuna", L't', RGB(0, 0, 139), {CritterTag::FISH, CritterTag::AQUATIC}, 55 };

    // Reptiles
    g_CritterData[CritterType::SNAKE] = { L"Snake", L's', RGB(0, 100, 0), {CritterTag::REPTILE, CritterTag::PREDATOR, CritterTag::VENOMOUS}, 90 };
    g_CritterData[CritterType::LIZARD] = { L"Lizard", L'l', RGB(50, 205, 50), {CritterTag::REPTILE, CritterTag::DIURNAL}, 70 };

    // Arachnids
    g_CritterData[CritterType::SPIDER] = { L"Spider", L'x', RGB(40, 40, 40), {CritterTag::ARACHNID, CritterTag::PREDATOR, CritterTag::NOCTURNAL}, 80 };

    // Crustaceans
    g_CritterData[CritterType::CRAB] = { L"Crab", L'c', RGB(255, 69, 0), {CritterTag::CRUSTACEAN, CritterTag::AQUATIC, CritterTag::SCAVENGER}, 110 };

    // Undead
    g_CritterData[CritterType::ZOMBIE] = { L"Zombie", L'z', RGB(0, 128, 0), {CritterTag::UNDEAD, CritterTag::CRYPTID, CritterTag::PREDATOR}, 150 };
    g_CritterData[CritterType::SKELETON] = { L"Skeleton", L's', RGB(240, 240, 240), {CritterTag::UNDEAD, CritterTag::CRYPTID, CritterTag::CONSTRUCT}, 100 };

    // Mythical
    g_CritterData[CritterType::GRIFFIN] = { L"Griffin", L'G', RGB(218, 165, 32), {CritterTag::MYTHICAL, CritterTag::BEAST, CritterTag::PREDATOR, CritterTag::FLYING, CritterTag::MOUNT}, 40 };

    // Dragonoids
    g_CritterData[CritterType::WYVERN] = { L"Wyvern", L'W', RGB(178, 34, 34), {CritterTag::DRAGONOID, CritterTag::REPTILE, CritterTag::PREDATOR, CritterTag::FLYING, CritterTag::MEGAFAUNA}, 35 };
}

void initBiomeCritterSpawns() {
    g_BiomeCritters.clear();

    // Land Critters
    g_BiomeCritters[Biome::TUNDRA] = { CritterType::WOLF, CritterType::DEER };
    g_BiomeCritters[Biome::BOREAL_FOREST] = { CritterType::WOLF, CritterType::DEER, CritterType::SQUIRREL, CritterType::HAWK, CritterType::BEAR };
    g_BiomeCritters[Biome::TEMPERATE_FOREST] = { CritterType::DEER, CritterType::PIG, CritterType::SQUIRREL, CritterType::PIGEON, CritterType::SNAKE, CritterType::WOLF, CritterType::CAT };
    g_BiomeCritters[Biome::JUNGLE] = { CritterType::MONKEY, CritterType::SNAKE, CritterType::WYVERN, CritterType::SPIDER };
    g_BiomeCritters[Biome::DESERT] = { CritterType::KANGAROO, CritterType::SNAKE, CritterType::LIZARD, CritterType::SPIDER, CritterType::HAWK };

    // Aquatic critters can spawn in any biome that has water.
    g_BiomeCritters[Biome::OCEAN] = { CritterType::DOLPHIN, CritterType::WHALE, CritterType::TROUT, CritterType::TUNA, CritterType::CRAB };
}

bool isWalkable(int x, int y, int z) {
    // Check if coordinates are within world boundaries
    if (x < 0 || x >= WORLD_WIDTH || y < 0 || y >= WORLD_HEIGHT || z < 0 || z >= TILE_WORLD_DEPTH) {
        return false; // Out of bounds
    }

    // Get the MapCell data for the given coordinates
    const auto& cell = Z_LEVELS[z][y][x];
    // Get the tags associated with the tile type in this cell
    const auto& tags = TILE_DATA.at(cell.type).tags;

    // A pawn cannot walk on fluid tiles (like water or molten core)
    if (std::find(tags.begin(), tags.end(), TileTag::FLUID) != tags.end()) {
        return false;
    }

    // A pawn cannot walk through solid structures like walls, or through a tree's occupied space
    if (cell.type == TileType::WALL || cell.type == TileType::STONE_WALL || cell.tree != nullptr) {
        return false;
    }

    // A pawn also cannot walk onto a blueprint if the blueprint is for a blocking structure
    // (e.g., you can't walk onto a "blueprint for a wall", but you can walk onto a "blueprint for a floor")
    if (cell.type == TileType::BLUEPRINT) {
        // If the target type of the blueprint is a blocking structure, it's not walkable
        const auto& target_tags = TILE_DATA.at(cell.target_type).tags;
        if (std::find(target_tags.begin(), target_tags.end(), TileTag::STRUCTURE) != target_tags.end() &&
            cell.target_type != TileType::WOOD_FLOOR && // Allow walking on floor blueprints
            cell.target_type != TileType::DIRT_FLOOR && // Just in case, if these become blueprints
            cell.target_type != TileType::GRASS // etc.
            ) {
            return false;
        }
    }

    // Most other tile types (dirt, grass, stone floor, empty space, items on ground, furniture, stairs) are considered walkable.
    return true;
}

// Function that should be defined before updateGame() if used there
bool isDeconstructable(TileType type) {
    if (TILE_DATA.count(type) == 0) return false;
    const auto& tags = TILE_DATA.at(type).tags;
    return (type == TileType::BLUEPRINT || // Allow deconstructing (canceling) blueprints
        std::find(tags.begin(), tags.end(), TileTag::STRUCTURE) != tags.end() ||
        std::find(tags.begin(), tags.end(), TileTag::FURNITURE) != tags.end() ||
        std::find(tags.begin(), tags.end(), TileTag::LIGHTS) != tags.end() ||
        std::find(tags.begin(), tags.end(), TileTag::PRODUCTION) != tags.end());
}


// The new pathfinding function, should be defined before updateGame()
std::vector<Point3D> findPath(Point3D start, Point3D end) {
    if (start.x == end.x && start.y == end.y && start.z == end.z) {
        return { start }; // Already at destination
    }

    std::queue<Point3D> q;
    std::map<Point3D, Point3D> came_from; // To reconstruct the path
    std::set<Point3D> visited;           // To avoid cycles and redundant checks

    q.push(start);
    visited.insert(start);

    bool path_found = false;

    while (!q.empty()) {
        Point3D current = q.front();
        q.pop();

        if (current.x == end.x && current.y == end.y && current.z == end.z) {
            path_found = true;
            break;
        }

        // Neighbors on the same Z-level (8 directions)
        for (int dy = -1; dy <= 1; ++dy) {
            for (int dx = -1; dx <= 1; ++dx) {
                if (dx == 0 && dy == 0) continue;

                Point3D neighbor = { current.x + dx, current.y + dy, current.z };

                if (isWalkable(neighbor.x, neighbor.y, neighbor.z) && visited.find(neighbor) == visited.end()) {
                    visited.insert(neighbor);
                    came_from[neighbor] = current;
                    q.push(neighbor);
                }
            }
        }

        // Check for stairs to move between Z-levels
        TileType currentTile = Z_LEVELS[current.z][current.y][current.x].type;

        // Try to go DOWN
        if (currentTile == TileType::STAIR_DOWN && current.z > 0) {
            Point3D neighbor = { current.x, current.y, current.z - 1 };
            // Check if the tile below is a STAIR_UP AND is walkable (the stair itself is walkable)
            if (Z_LEVELS[neighbor.z][neighbor.y][neighbor.x].type == TileType::STAIR_UP &&
                isWalkable(neighbor.x, neighbor.y, neighbor.z) && visited.find(neighbor) == visited.end()) {
                visited.insert(neighbor);
                came_from[neighbor] = current;
                q.push(neighbor);
            }
        }

        // Try to go UP
        if (currentTile == TileType::STAIR_UP && current.z < TILE_WORLD_DEPTH - 1) {
            Point3D neighbor = { current.x, current.y, current.z + 1 };
            // Check if the tile above is a STAIR_DOWN AND is walkable (the stair itself is walkable)
            if (Z_LEVELS[neighbor.z][neighbor.y][neighbor.x].type == TileType::STAIR_DOWN &&
                isWalkable(neighbor.x, neighbor.y, neighbor.z) && visited.find(neighbor) == visited.end()) {
                visited.insert(neighbor);
                came_from[neighbor] = current;
                q.push(neighbor);
            }
        }
    }

    std::vector<Point3D> path;
    if (path_found) {
        Point3D current = end;
        while (!(current.x == start.x && current.y == start.y && current.z == start.z)) {
            path.push_back(current);
            current = came_from[current];
        }
        path.push_back(start);
        std::reverse(path.begin(), path.end());
    }
    return path;
}



bool isCritterWalkable(int x, int y, int z) {
    if (x < 0 || x >= WORLD_WIDTH || y < 0 || y >= WORLD_HEIGHT || z < 0 || z >= TILE_WORLD_DEPTH) {
        return false; // Out of bounds
    }
    const auto& cell = Z_LEVELS[z][y][x];
    const auto& tags = TILE_DATA.at(cell.type).tags;

    // Critters cannot walk on fluid tiles
    if (std::find(tags.begin(), tags.end(), TileTag::FLUID) != tags.end()) {
        return false;
    }

    // Unwalkable if it's a solid structure (like a wall)
    if (cell.type == TileType::WALL || cell.type == TileType::STONE_WALL) {
        return false;
    }

    // Unlike pawns, critters can move through tiles with trees.
    // Most other tile types are walkable (ground, floor, blueprints, furniture, stairs etc.)
    return true;
}

// NEW: Pre-computes a map of all tiles reachable by any colonist.
// This is called once when entering build mode to prevent lag during preview.
void computeGlobalReachability() {
    // 1. Initialize the map to be all false.
    g_isTileReachable.assign(TILE_WORLD_DEPTH, std::vector<std::vector<bool>>(WORLD_HEIGHT, std::vector<bool>(WORLD_WIDTH, false)));
    if (colonists.empty()) {
        return; // If there are no colonists, nothing is reachable.
    }

    // 2. Setup a queue for a multi-source Breadth-First Search (BFS).
    std::queue<Point3D> q;

    // 3. Add all colonist positions as the starting sources for the search.
    for (const auto& pawn : colonists) {
        if (pawn.x >= 0 && pawn.y >= 0 && pawn.z >= 0) { // Safety check
            if (!g_isTileReachable[pawn.z][pawn.y][pawn.x]) {
                q.push({ pawn.x, pawn.y, pawn.z });
                g_isTileReachable[pawn.z][pawn.y][pawn.x] = true;
            }
        }
    }

    // 4. Run the BFS to find all reachable tiles.
    while (!q.empty()) {
        Point3D current = q.front();
        q.pop();

        // Explore neighbors on the same z-level.
        for (int dy = -1; dy <= 1; ++dy) {
            for (int dx = -1; dx <= 1; ++dx) {
                if (dx == 0 && dy == 0) continue;

                Point3D neighbor = { current.x + dx, current.y + dy, current.z };
                if (isWalkable(neighbor.x, neighbor.y, neighbor.z) && !g_isTileReachable[neighbor.z][neighbor.y][neighbor.x]) {
                    g_isTileReachable[neighbor.z][neighbor.y][neighbor.x] = true;
                    q.push(neighbor);
                }
            }
        }

        // Explore neighbors on different z-levels via stairs.
        TileType currentTile = Z_LEVELS[current.z][current.y][current.x].type;
        // Check for going down
        if (currentTile == TileType::STAIR_DOWN && current.z > 0 && Z_LEVELS[current.z - 1][current.y][current.x].type == TileType::STAIR_UP) {
            Point3D neighbor = { current.x, current.y, current.z - 1 };
            if (!g_isTileReachable[neighbor.z][neighbor.y][neighbor.x]) {
                g_isTileReachable[neighbor.z][neighbor.y][neighbor.x] = true;
                q.push(neighbor);
            }
        }
        // Check for going up
        if (currentTile == TileType::STAIR_UP && current.z < TILE_WORLD_DEPTH - 1 && Z_LEVELS[current.z + 1][current.y][current.x].type == TileType::STAIR_DOWN) {
            Point3D neighbor = { current.x, current.y, current.z + 1 };
            if (!g_isTileReachable[neighbor.z][neighbor.y][neighbor.x]) {
                g_isTileReachable[neighbor.z][neighbor.y][neighbor.x] = true;
                q.push(neighbor);
            }
        }
    }
}

bool isReachable(Point3D start, Point3D end) {
    // If start and end are the same, it's reachable.
    if (start.x == end.x && start.y == end.y && start.z == end.z) {
        return true;
    }

    // --- 3D Breadth-First Search (BFS) for Pathfinding ---

    std::queue<Point3D> q;
    // Visited set prevents us from checking the same tile multiple times.
    std::set<Point3D> visited;

    // The maximum number of tiles to check. This is a safety break
    // to prevent the game from freezing if a path is extremely long or complex.
    const int MAX_SEARCH_NODES = 20000;
    int nodesProcessed = 0;

    // Start the search from the pawn's current position.
    q.push(start);
    visited.insert(start);

    while (!q.empty()) {
        nodesProcessed++;
        if (nodesProcessed > MAX_SEARCH_NODES) {
            return false; // Path is too long or complex, assume unreachable.
        }

        Point3D current = q.front();
        q.pop();

        // Check if we have reached the destination.
        if (current.x == end.x && current.y == end.y && current.z == end.z) {
            return true;
        }

        // --- Explore Neighbors ---

        // 1. Check adjacent tiles on the SAME Z-level (x, y movement)
        for (int dy = -1; dy <= 1; ++dy) {
            for (int dx = -1; dx <= 1; ++dx) {
                if (dx == 0 && dy == 0) continue;

                Point3D neighbor = { current.x + dx, current.y + dy, current.z };

                if (isWalkable(neighbor.x, neighbor.y, neighbor.z) && visited.find(neighbor) == visited.end()) {
                    visited.insert(neighbor);
                    q.push(neighbor);
                }
            }
        }

        // 2. Check for stairs to move between Z-levels
        TileType currentTile = Z_LEVELS[current.z][current.y][current.x].type;

        // Check for STAIRS DOWN
        if (currentTile == TileType::STAIR_DOWN && current.z > 0) {
            // The tile below must be a corresponding STAIR_UP to be usable.
            if (Z_LEVELS[current.z - 1][current.y][current.x].type == TileType::STAIR_UP) {
                Point3D neighbor = { current.x, current.y, current.z - 1 };
                if (visited.find(neighbor) == visited.end()) {
                    visited.insert(neighbor);
                    q.push(neighbor);
                }
            }
        }

        // Check for STAIRS UP
        if (currentTile == TileType::STAIR_UP && current.z < TILE_WORLD_DEPTH - 1) {
            // The tile above must be a corresponding STAIR_DOWN to be usable.
            if (Z_LEVELS[current.z + 1][current.y][current.x].type == TileType::STAIR_DOWN) {
                Point3D neighbor = { current.x, current.y, current.z + 1 };
                if (visited.find(neighbor) == visited.end()) {
                    visited.insert(neighbor);
                    q.push(neighbor);
                }
            }
        }
    }

    return false; // The queue is empty but we never found the destination, so no path exists.
}

bool isReachableByAnyColonist(Point3D target) {
    if (colonists.empty()) {
        return false; // No one can reach it if there's no one to try
    }
    for (const auto& pawn : colonists) {
        if (isReachable({ pawn.x, pawn.y, pawn.z }, target)) {
            return true; // Found at least one pawn that can reach the target
        }
    }
    return false; // No pawns can reach the target
}

bool CanBuildOn(int x, int y, int z, TileType toBuild) {
    if (x < 0 || x >= WORLD_WIDTH || y < 0 || y >= WORLD_HEIGHT || z < 0 || z >= TILE_WORLD_DEPTH) {
        return false;
    }

    const MapCell& cell = Z_LEVELS[z][y][x];
    const auto& tags = TILE_DATA.at(cell.type).tags;

    // Special case: Allow building wood floors directly on water tiles
    if (toBuild == TileType::WOOD_FLOOR && cell.type == TileType::WATER) {
        return true;
    }

    // Check for explicit blocking conditions first
    if (cell.tree != nullptr ||
        cell.type == TileType::BLUEPRINT ||
        std::find(tags.begin(), tags.end(), TileTag::STRUCTURE) != tags.end() ||
        std::find(tags.begin(), tags.end(), TileTag::FURNITURE) != tags.end() ||
        std::find(tags.begin(), tags.end(), TileTag::LIGHTS) != tags.end() ||
        std::find(tags.begin(), tags.end(), TileTag::PRODUCTION) != tags.end() ||
        std::find(tags.begin(), tags.end(), TileTag::FLUID) != tags.end() ||
        std::find(tags.begin(), tags.end(), TileTag::STONE) != tags.end() ||
        std::find(tags.begin(), tags.end(), TileTag::MINERAL) != tags.end() ||
        std::find(tags.begin(), tags.end(), TileTag::ORE) != tags.end())
    {
        return false;
    }

    // Allow building on any other tile that isn't explicitly blocked (e.g., floors, empty space)
    return true;
}

std::vector<POINT> BresenhamLine(int x1, int y1, int x2, int y2) {
    std::vector<POINT> points;
    int dx = abs(x2 - x1);
    int dy = -abs(y2 - y1);
    int sx = (x1 < x2) ? 1 : -1;
    int sy = (y1 < y2) ? 1 : -1;
    int err = dx + dy;
    int e2;

    while (true) {
        points.push_back({ (long)x1, (long)y1 });
        if (x1 == x2 && y1 == y2) break;
        e2 = 2 * err;
        if (e2 >= dy) {
            err += dy;
            x1 += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y1 += sy;
        }
    }
    return points;
}

void resetGame() {
    worldName = L"New World"; solarSystemName = L"Sol System"; g_homeSystemStarIndex = -1; colonists.clear(); rerollablePawns.clear(); jobQueue.clear(); resources.clear(); solarSystem.clear(); distantStars.clear(); a_trees.clear(); a_fallingTrees.clear(); nextTreeId = 0; g_stockpiledResources.clear(); g_critters.clear();
    Z_LEVELS.clear();
    for (int y = 0; y < WORLD_HEIGHT; ++y) { designations[y].assign(WORLD_WIDTH, L' '); }
    landingSiteX = -1; landingSiteY = -1; cursorX = PLANET_MAP_WIDTH / 2; cursorY = PLANET_MAP_HEIGHT / 2;
    currentTab = Tab::NONE; inspectedPawnIndex = -1; followedPawnIndex = -1; gameSpeed = 1; currentZ = BIOSPHERE_Z_LEVEL;
    g_startingTimezoneOffset = 0.0f;
    worldGen_selectedOption = 0; worldGen_isNaming = false;
    numberOfPlanets = 5; selectedWorldType = WorldType::EARTH_LIKE;
    planetCustomization_selected = 0; planetCustomization_isEditing = false;
    currentArchitectMode = ArchitectMode::NONE; isDrawingDesignationRect = false; designationStartX = -1;
    isSelectingArchitectGizmo = false; architectGizmoSelection = 0;
    cameraX = (WORLD_WIDTH - VIEWPORT_WIDTH_TILES) / 2; cameraY = (WORLD_HEIGHT - VIEWPORT_HEIGHT_TILES) / 2;
    gameTicks = 3600 * 12; updateTime(); weatherChangeCooldown = 0;
    isDebugMode = false; currentDebugState = DebugMenuState::NONE;
    g_lightSources.clear();

    // Reset Research
    g_allResearch.clear(); g_completedResearch.clear(); g_currentResearchProject = L""; g_researchProgress = 0;
    researchUI_selectedEra = ResearchEra::NEOLITHIC; researchUI_selectedCategory = ResearchCategory::ALL; researchUI_selectedProjectIndex = 0;

    // NEW: Reset and initialize unlocked content
    g_unlockedBuildings.clear();
    g_unlockedBuildings.insert(TileType::TORCH); // Torches are available from the start
    g_unlockedBuildings.insert(TileType::RESEARCH_BENCH); // The basic research bench is available from the start

    // --- START OF FIX ---
    // Repopulate the research and pawn data after clearing it.
    initResearchData();
    initPawnData();
    // --- END OF FIX ---

    // NEW: Reset Stockpile Data
    g_stockpiles.clear();
    nextStockpileId = 0;
    inspectedStockpileIndex = -1;
    stockpilePanel_selectedLineIndex = -1; // Reset to "Accept All" / "Decline All" special selection
    stockpilePanel_scrollOffset = 0;
    // Reset category expansion state to default (all expanded)
    for (auto& pair : stockpilePanel_categoryExpanded) {
        pair.second = true;
    }
    stuffsUI_scrollOffset = 0; // Reset scroll offset for stuffs panel
}
Pawn generatePawn() {
    static std::vector<std::wstring> firstNamesMale;
    static std::vector<std::wstring> firstNamesFemale;

    // Load names from files on the first call only.
    if (firstNamesMale.empty()) {
        firstNamesMale = readNamesFromFile(L"Data\\firstname_male.txt");
        // If the file is missing or empty, use a fallback list.
        if (firstNamesMale.empty()) {
            firstNamesMale = { L"Ronald", L"James", L"William", L"Ken", L"Marcus", L"David" };
        }
    }
    if (firstNamesFemale.empty()) {
        firstNamesFemale = readNamesFromFile(L"Data\\firstname_female.txt");
        // If the file is missing or empty, use a fallback list.
        if (firstNamesFemale.empty()) {
            firstNamesFemale = { L"Amy", L"Ashley", L"Maria", L"Sarah", L"Jessica", L"Anya" };
        }
    }


    static std::vector<std::wstring> lastNames;

    // Load last names from file on the first call only.
    if (lastNames.empty()) {
        lastNames = readNamesFromFile(L"Data\\lastname.txt");
        // If the file is missing or empty, use a fallback list.
        if (lastNames.empty()) {
            lastNames = { L"Harris", L"Thompson", L"Perez", L"Smith", L"Jones", L"Miller" };
        }
    }


    // New: Create a temporary list of backstory keys
    std::vector<std::wstring> backstoryKeys;
    for (const auto& pair : g_Backstories) backstoryKeys.push_back(pair.first);

    const std::vector<std::wstring> traitList = { L"Industrious", L"Lazy", L"Optimist", L"Pessimist", L"Tough", L"Quick-sleeper", L"Greedy" };
    Pawn pawn; pawn.gender = (rand() % 2 == 0) ? L"Male" : L"Female";
    pawn.name = (pawn.gender == L"Male" ? firstNamesMale[rand() % firstNamesMale.size()] : firstNamesFemale[rand() % firstNamesFemale.size()]);
    pawn.name += L" " + lastNames[rand() % lastNames.size()];
    pawn.age = rand() % 50 + 18;
    // New: Assign backstory from map keys
    if (!backstoryKeys.empty()) {
        pawn.backstory = backstoryKeys[rand() % backstoryKeys.size()];
    }
    else {
        pawn.backstory = L"Survivor"; // A safe default if data isn't loaded
    }

    int numTraits = rand() % 3 + 1; std::vector<std::wstring> availableTraits = traitList;
    for (int i = 0; i < numTraits && !availableTraits.empty(); ++i) {
        int traitIndex = rand() % availableTraits.size();
        pawn.traits.push_back(availableTraits[traitIndex]);
        availableTraits.erase(availableTraits.begin() + traitIndex);
    }
    for (const auto& jobName : JobTypeNames) { pawn.skills[jobName] = rand() % 11; }
    pawn.priorities[JobType::Haul] = 2;
    for (int i = 0; i < static_cast<int>(JobTypeNames.size()); ++i) { pawn.priorities[(JobType)i] = 2; }
    return pawn;
}
StratumInfo getStratumInfoForZ(int z) {
    if (z >= TILE_WORLD_DEPTH) {
        int space_z = z - TILE_WORLD_DEPTH;
        if (space_z == 0) return { L"Planet View", Stratum::OUTER_SPACE_PLANET_VIEW, 1 };
        if (space_z == 1) return { L"System View", Stratum::OUTER_SPACE_SYSTEM_VIEW, 1 };
        return { L"The Beyond", Stratum::OUTER_SPACE_BEYOND, 1 };
    }
    int z_accumulator = 0;
    for (const auto& stratum : strataDefinition) {
        z_accumulator += stratum.depth;
        if (z < z_accumulator) return stratum;
    }
    return strataDefinition.back();
}
std::wstring getDaySuffix(int day) {
    if (day % 10 == 1 && day != 11) return L"st";
    if (day % 10 == 2 && day != 12) return L"nd";
    if (day % 10 == 3 && day != 13) return L"rd";
    return L"th";
}
void spawnTree(int x, int y, TileType type) {
    Tree tree;
    tree.id = nextTreeId++;
    tree.rootX = x;
    tree.rootY = y;
    tree.rootZ = BIOSPHERE_Z_LEVEL;
    tree.type = type;

    int height = 3 + rand() % 8; // Height from 3 to 10 z-levels (total)
    int maxZ = BIOSPHERE_Z_LEVEL + height - 1;
    if (maxZ >= ATMOSPHERE_TOP_Z) maxZ = ATMOSPHERE_TOP_Z - 1;

    struct TreeGenParams { TileType trunk, branch, leaf; };
    TreeGenParams params;

    switch (type) {
    case TileType::OAK: params = { TileType::OAK_TRUNK, TileType::OAK_BRANCH, TileType::OAK_LEAF }; break;
    case TileType::ACACIA: params = { TileType::ACACIA_TRUNK, TileType::ACACIA_BRANCH, TileType::ACACIA_LEAF }; break;
    case TileType::SPRUCE: params = { TileType::SPRUCE_TRUNK, TileType::SPRUCE_BRANCH, TileType::SPRUCE_NEEDLE }; break;
    case TileType::BIRCH: params = { TileType::BIRCH_TRUNK, TileType::BIRCH_BRANCH, TileType::BIRCH_LEAF }; break;
    case TileType::PINE: params = { TileType::PINE_TRUNK, TileType::PINE_BRANCH, TileType::PINE_NEEDLE }; break;
    case TileType::POPLAR: params = { TileType::POPLAR_TRUNK, TileType::POPLAR_BRANCH, TileType::POPLAR_LEAF }; break;
    case TileType::CECROPIA: params = { TileType::CECROPIA_TRUNK, TileType::CECROPIA_BRANCH, TileType::CECROPIA_LEAF }; break;
    case TileType::COCOA: params = { TileType::COCOA_TRUNK, TileType::COCOA_BRANCH, TileType::COCOA_LEAF }; break;
    case TileType::CYPRESS: params = { TileType::CYPRESS_TRUNK, TileType::CYPRESS_BRANCH, TileType::CYPRESS_FOLIAGE }; break;
    case TileType::MAPLE: params = { TileType::MAPLE_TRUNK, TileType::MAPLE_BRANCH, TileType::MAPLE_LEAF }; break;
    case TileType::TEAK: params = { TileType::TEAK_TRUNK, TileType::TEAK_BRANCH, TileType::TEAK_LEAF }; break;

    case TileType::PALM: {
        for (int z = BIOSPHERE_Z_LEVEL; z <= maxZ; ++z) tree.parts.push_back({ x, y, z, TileType::PALM_TRUNK });
        int canopyZ = maxZ;
        for (int ly = -2; ly <= 2; ++ly) for (int lx = -2; lx <= 2; ++lx) {
            if (lx == 0 && ly == 0) continue;
            int px = x + lx, py = y + ly;
            if (px < 0 || px >= WORLD_WIDTH || py < 0 || py >= WORLD_HEIGHT) continue; // Boundary check
            if (abs(lx) == abs(ly) || abs(lx) + abs(ly) > 3) continue;
            if (rand() % 2 == 0) tree.parts.push_back({ px, py, canopyZ, TileType::PALM_FROND });
        }
        goto finished_generation;
    }
    case TileType::SAGUARO: {
        for (int z = BIOSPHERE_Z_LEVEL; z <= maxZ; ++z) {
            tree.parts.push_back({ x, y, z, TileType::SAGUARO_TRUNK });
            if (z > BIOSPHERE_Z_LEVEL + 2 && rand() % 4 == 0) { // Add an arm
                int armDir = (rand() % 2 == 0) ? -1 : 1;
                int armX = x + armDir;
                if (armX < 0 || armX >= WORLD_WIDTH) continue; // Boundary check
                int armHeight = z + (rand() % 3) + 1;
                for (int az = z; az < armHeight && az <= maxZ; ++az) tree.parts.push_back({ armX, y, az, TileType::SAGUARO_ARM });
            }
        }
        goto finished_generation;
    }
    case TileType::PRICKLYPEAR: {
        for (int i = 0; i < (height / 2) + 1; ++i) {
            int px = x + (rand() % 3 - 1); int py = y + (rand() % 3 - 1);
            if (px < 0 || px >= WORLD_WIDTH || py < 0 || py >= WORLD_HEIGHT) continue; // Boundary check
            if (Z_LEVELS[BIOSPHERE_Z_LEVEL][py][px].tree == nullptr) { // Avoid overlap
                tree.parts.push_back({ px, py, BIOSPHERE_Z_LEVEL, TileType::PRICKLYPEAR_PAD });
                if (rand() % 4 == 0) tree.parts.push_back({ px, py, BIOSPHERE_Z_LEVEL + 1, TileType::PRICKLYPEAR_TUNA });
            }
        }
        goto finished_generation;
    }
    case TileType::CHOLLA: {
        for (int z = BIOSPHERE_Z_LEVEL; z < maxZ; ++z) {
            tree.parts.push_back({ x, y, z, TileType::CHOLLA_TRUNK });
            if (rand() % 2 == 0) {
                int jx = x + (rand() % 3 - 1); int jy = y + (rand() % 3 - 1);
                if (jx < 0 || jx >= WORLD_WIDTH || jy < 0 || jy >= WORLD_HEIGHT) continue; // Boundary check
                if ((jx != x || jy != y) && Z_LEVELS[z][jy][jx].tree == nullptr) tree.parts.push_back({ jx, jy, z, TileType::CHOLLA_JOINT });
            }
        }
        goto finished_generation;
    }
    default: params = { TileType::TRUNK, TileType::BRANCH, TileType::LEAF }; break;
    }

    // Generic procedural generation for standard trees
    for (int z = BIOSPHERE_Z_LEVEL; z <= maxZ; ++z) {
        int z_offset = z - BIOSPHERE_Z_LEVEL;
        float height_ratio = (float)z_offset / height;

        // Trunk
        tree.parts.push_back({ x, y, z, params.trunk });
        if (height_ratio < 0.3 && rand() % 2 == 0) { // Thicker base
            int tx = x + ((rand() % 2 == 0) ? 1 : -1);
            if (tx >= 0 && tx < WORLD_WIDTH) tree.parts.push_back({ tx, y, z, params.trunk }); // Boundary check
        }

        // Branches
        if (height_ratio > 0.2 && height_ratio < 0.9) {
            if (rand() % 100 < 40) { // Chance to grow a branch at this level
                int branch_len = 1 + rand() % 3;
                int bdx = (rand() % 3) - 1; int bdy = (rand() % 3) - 1;
                if (bdx == 0 && bdy == 0) bdx = 1; // Must go somewhere
                for (int l = 1; l <= branch_len; ++l) {
                    int bx = x + l * bdx; int by = y + l * bdy;
                    if (bx < 0 || bx >= WORLD_WIDTH || by < 0 || by >= WORLD_HEIGHT) break; // Boundary check
                    tree.parts.push_back({ bx, by, z, params.branch });
                }
            }
        }
        // Leaves/Canopy
        if (height_ratio > 0.5) {
            int canopy_radius = (int)(3 * (height_ratio - 0.4));
            for (int ly = -canopy_radius; ly <= canopy_radius; ++ly) for (int lx = -canopy_radius; lx <= canopy_radius; ++lx) {
                if (abs(lx) + abs(ly) > canopy_radius) continue; // Roughly circular canopy
                int px = x + lx; int py = y + ly;
                if (px < 0 || px >= WORLD_WIDTH || py < 0 || py >= WORLD_HEIGHT) continue; // Boundary check
                if (rand() % 100 < 35) tree.parts.push_back({ px, py, z, params.leaf });
            }
        }
    }

finished_generation:
    a_trees[tree.id] = tree;
    for (const auto& part : a_trees[tree.id].parts) {
        if (part.x >= 0 && part.x < WORLD_WIDTH && part.y >= 0 && part.y < WORLD_HEIGHT && part.z >= 0 && part.z < TILE_WORLD_DEPTH) {
            MapCell& cell = Z_LEVELS[part.z][part.y][part.x];
            if (cell.tree == nullptr || TILE_DATA.at(cell.type).tags.empty()) {
                cell.type = part.type;
                cell.tree = &a_trees[tree.id];
            }
        }
    }
}

void generateFullWorld(Biome biome) {
    Z_LEVELS.assign(TILE_WORLD_DEPTH, std::vector<std::vector<MapCell>>(WORLD_HEIGHT, std::vector<MapCell>(WORLD_WIDTH)));

    // --- STEP 1: Define generation parameters ---
    std::map<Stratum, std::vector<TileType>> stratumStones;
    stratumStones[Stratum::CRUST] = { TileType::SANDSTONE, TileType::SHALE, TileType::LIMESTONE, TileType::CHALK, TileType::CHERT, TileType::CLAYSTONE, TileType::CONGLOMERATE, TileType::DOLOMITE, TileType::MUDSTONE, TileType::ROCK_SALT, TileType::SANDSTONE, TileType::SHALE, TileType::SILTSTONE };
    stratumStones[Stratum::LITHOSPHERE] = { TileType::GRANITE, TileType::BASALT, TileType::GABBRO, TileType::SCHIST, TileType::ANDESITE, TileType::DACITE, TileType::RHYOLITE };
    stratumStones[Stratum::ASTHENOSPHERE] = { TileType::GNEISS, TileType::PHYLLITE, TileType::OBSIDIAN };
    stratumStones[Stratum::UPPER_MANTLE] = { TileType::DIORITE, TileType::GABBRO, TileType::GNEISS, TileType::MARBLE, TileType::QUARTZITE, TileType::SLATE };
    stratumStones[Stratum::LOWER_MANTLE] = { TileType::BASALT, TileType::DIORITE };
    stratumStones[Stratum::OUTER_CORE] = { TileType::MOLTEN_CORE };
    stratumStones[Stratum::INNER_CORE] = { TileType::CORESTONE };

    std::map<Biome, TileType> biomeGround;
    biomeGround[Biome::TUNDRA] = TileType::SNOW;
    biomeGround[Biome::BOREAL_FOREST] = TileType::GRASS;
    biomeGround[Biome::TEMPERATE_FOREST] = TileType::GRASS; // Still grass for temperate
    biomeGround[Biome::JUNGLE] = TileType::JUNGLE_GRASS;     // <--- NEW: Use Jungle Grass
    biomeGround[Biome::DESERT] = TileType::SAND;

    std::map<Biome, std::vector<TileType>> biomeTrees;
    biomeTrees[Biome::TUNDRA] = { TileType::SPRUCE, TileType::PINE };
    biomeTrees[Biome::BOREAL_FOREST] = { TileType::SPRUCE, TileType::PINE, TileType::BIRCH };
    biomeTrees[Biome::TEMPERATE_FOREST] = { TileType::OAK, TileType::MAPLE, TileType::PINE, TileType::CYPRESS };
    biomeTrees[Biome::JUNGLE] = { TileType::TEAK, TileType::CECROPIA, TileType::COCOA, TileType::PALM };
    biomeTrees[Biome::DESERT] = { TileType::SAGUARO, TileType::ACACIA, TileType::PRICKLYPEAR, TileType::CHOLLA };

    // --- STEP 2: Initial Strata and Rock Generation ---
    for (int z = 0; z < TILE_WORLD_DEPTH; ++z) {
        for (int y = 0; y < WORLD_HEIGHT; ++y) {
            for (int x = 0; x < WORLD_WIDTH; ++x) {
                TileType base_rock_type;
                StratumInfo sInfo = getStratumInfoForZ(z);
                if (sInfo.type == Stratum::BIOSPHERE) {
                    base_rock_type = (rand() % 5 == 0) ? TileType::DIRT_FLOOR : biomeGround[biome];
                }
                else if (sInfo.type == Stratum::HYDROSPHERE) {
                    base_rock_type = TileType::DIRT_FLOOR;
                }
                else if (sInfo.type >= Stratum::ATMOSPHERE) {
                    base_rock_type = TileType::EMPTY;
                }
                else {
                    if (!stratumStones[sInfo.type].empty()) {
                        base_rock_type = stratumStones[sInfo.type][rand() % stratumStones[sInfo.type].size()];
                    }
                    else {
                        base_rock_type = TileType::EMPTY;
                    }
                }
                Z_LEVELS[z][y][x].underlying_type = base_rock_type;
                Z_LEVELS[z][y][x].type = base_rock_type;
            }
        }
    }

    // --- STEP 3: Generate Caves and fill deep underground with Stone Floors ---
    for (int z = 0; z < BIOSPHERE_Z_LEVEL; ++z) {
        if (z == HYDROSPHERE_Z_LEVEL) continue;
        StratumInfo sInfo = getStratumInfoForZ(z);
        if (sInfo.type == Stratum::OUTER_CORE || sInfo.type == Stratum::INNER_CORE) continue;

        std::vector<std::vector<int>> noiseMap(WORLD_HEIGHT, std::vector<int>(WORLD_WIDTH));
        for (int y = 0; y < WORLD_HEIGHT; ++y) for (int x = 0; x < WORLD_WIDTH; ++x) noiseMap[y][x] = (rand() % 100 < 45) ? 1 : 0;

        for (int i = 0; i < 4; ++i) {
            std::vector<std::vector<int>> newNoiseMap = noiseMap;
            // --- START OF FIX 1 ---
            for (int y = 0; y < WORLD_HEIGHT; ++y) {
                for (int x = 0; x < WORLD_WIDTH; ++x) {
                    int wallCount = 0;
                    for (int ny = -1; ny <= 1; ++ny) {
                        for (int nx = -1; nx <= 1; ++nx) {
                            int checkX = x + nx;
                            int checkY = y + ny;
                            if (checkX >= 0 && checkX < WORLD_WIDTH && checkY >= 0 && checkY < WORLD_HEIGHT) {
                                if (noiseMap[checkY][checkX] == 1) wallCount++;
                            }
                            else {
                                wallCount++; // Count borders as walls to keep map edges solid
                            }
                        }
                    }
                    if (noiseMap[y][x] == 1) {
                        newNoiseMap[y][x] = (wallCount >= 5) ? 1 : 0; // A wall needs 4+ neighbors to survive (5 including itself)
                    }
                    else {
                        newNoiseMap[y][x] = (wallCount >= 5) ? 1 : 0; // An empty space needs 5 neighbors to become a wall
                    }
                }
            }
            // --- END OF FIX 1 ---
            noiseMap = newNoiseMap;
        }

        for (int y = 0; y < WORLD_HEIGHT; ++y) for (int x = 0; x < WORLD_WIDTH; ++x) {
            if (noiseMap[y][x] == 0) {
                if (z < HYDROSPHERE_Z_LEVEL) {
                    Z_LEVELS[z][y][x].type = TileType::STONE_FLOOR;
                }
                else {
                    Z_LEVELS[z][y][x].type = TileType::EMPTY;
                }
            }
        }
    }

    // --- STEP 4: Generate Surface Water ---
    bool isForestBiome = (biome == Biome::BOREAL_FOREST || biome == Biome::TEMPERATE_FOREST || biome == Biome::JUNGLE);
    if (isForestBiome) {
        std::vector<std::vector<int>> waterMap(WORLD_HEIGHT, std::vector<int>(WORLD_WIDTH, 0));
        for (int y = 0; y < WORLD_HEIGHT; ++y) for (int x = 0; x < WORLD_WIDTH; ++x) waterMap[y][x] = (rand() % 100 < 35) ? 1 : 0;

        for (int i = 0; i < 5; ++i) {
            auto newWaterMap = waterMap;
            // --- START OF FIX 2 ---
            for (int y = 0; y < WORLD_HEIGHT; ++y) {
                for (int x = 0; x < WORLD_WIDTH; ++x) {
                    int waterNeighbors = 0;
                    for (int ny = -1; ny <= 1; ++ny) {
                        for (int nx = -1; nx <= 1; ++nx) {
                            int checkX = x + nx;
                            int checkY = y + ny;
                            if (checkX >= 0 && checkX < WORLD_WIDTH && checkY >= 0 && checkY < WORLD_HEIGHT) {
                                if (waterMap[checkY][checkX] == 1) waterNeighbors++;
                            }
                            else {
                                waterNeighbors++; // Count borders as water to prevent lakes touching the edge
                            }
                        }
                    }
                    if (waterNeighbors >= 5) newWaterMap[y][x] = 1; else newWaterMap[y][x] = 0;
                }
            }
            // --- END OF FIX 2 ---
            waterMap = newWaterMap;
        }

        for (int y = 0; y < WORLD_HEIGHT; ++y) for (int x = 0; x < WORLD_WIDTH; ++x) {
            if (waterMap[y][x] == 1) Z_LEVELS[BIOSPHERE_Z_LEVEL][y][x].type = TileType::WATER;
        }

        int numRivers = 2 + rand() % 3;
        for (int i = 0; i < numRivers; ++i) {
            int currentX = rand() % WORLD_WIDTH;
            int currentY = 0;
            while (currentY < WORLD_HEIGHT) {
                for (int wy = -1; wy <= 1; ++wy) for (int wx = -1; wx <= 1; ++wx) {
                    int carveX = currentX + wx, carveY = currentY + wy;
                    if (carveX >= 0 && carveX < WORLD_WIDTH && carveY >= 0 && carveY < WORLD_HEIGHT) {
                        if (Z_LEVELS[BIOSPHERE_Z_LEVEL][carveY][carveX].type != TileType::WATER) {
                            Z_LEVELS[BIOSPHERE_Z_LEVEL][carveY][carveX].type = TileType::WATER;
                        }
                    }
                }
                currentY += 1;
                currentX += (rand() % 3) - 1;
                currentX = max(0, min(WORLD_WIDTH - 1, currentX));
            }
        }
    }

    // --- STEP 5: Mirror Surface Water onto the Hydrosphere Level ---
    for (int y = 0; y < WORLD_HEIGHT; ++y) {
        for (int x = 0; x < WORLD_WIDTH; ++x) {
            if (Z_LEVELS[BIOSPHERE_Z_LEVEL][y][x].type == TileType::WATER) {
                Z_LEVELS[HYDROSPHERE_Z_LEVEL][y][x].type = TileType::WATER;
            }
        }
    }

    // --- STEP 6: Ore Generation ---
    std::set<TileType> sedimentaryStones_s = { TileType::CHALK, TileType::CHERT, TileType::CLAYSTONE, TileType::CONGLOMERATE, TileType::DOLOMITE, TileType::LIMESTONE, TileType::MUDSTONE, TileType::ROCK_SALT, TileType::SANDSTONE, TileType::SHALE, TileType::SILTSTONE };
    std::set<TileType> igneousExtrusiveStones_s = { TileType::ANDESITE, TileType::BASALT, TileType::DACITE, TileType::OBSIDIAN, TileType::RHYOLITE };
    std::set<TileType> igneousIntrusiveStones_s = { TileType::DIORITE, TileType::GABBRO, TileType::GRANITE };
    std::set<TileType> metamorphicStones_s = { TileType::GNEISS, TileType::MARBLE, TileType::PHYLLITE, TileType::QUARTZITE, TileType::SCHIST, TileType::SLATE };
    std::set<TileType> allIgneousStones_s;
    allIgneousStones_s.insert(igneousExtrusiveStones_s.begin(), igneousExtrusiveStones_s.end());
    allIgneousStones_s.insert(igneousIntrusiveStones_s.begin(), igneousIntrusiveStones_s.end());
    std::set<TileType> allStones_s;
    allStones_s.insert(sedimentaryStones_s.begin(), sedimentaryStones_s.end());
    allStones_s.insert(allIgneousStones_s.begin(), allIgneousStones_s.end());
    allStones_s.insert(metamorphicStones_s.begin(), metamorphicStones_s.end());

    for (int z = 0; z < BIOSPHERE_Z_LEVEL; ++z) {
        StratumInfo sInfo = getStratumInfoForZ(z);
        if (sInfo.type != Stratum::OUTER_CORE && sInfo.type != Stratum::INNER_CORE && sInfo.type != Stratum::HYDROSPHERE) {
            generateOresInStratum(z, sInfo, sedimentaryStones_s, igneousExtrusiveStones_s, igneousIntrusiveStones_s, metamorphicStones_s, allIgneousStones_s, allStones_s);
        }
    }

    // --- STEP 7: Tree Generation (must be last) ---
    if (biomeTrees.count(biome)) {
        std::vector<TileType> possibleTrees = biomeTrees.at(biome);
        if (!possibleTrees.empty()) {
            for (int y = 0; y < WORLD_HEIGHT; ++y) for (int x = 0; x < WORLD_WIDTH; ++x) {
                const auto& cell = Z_LEVELS[BIOSPHERE_Z_LEVEL][y][x];
                const auto& tile_tags = TILE_DATA.at(cell.type).tags;
                // Check for SOIL tag and ensure it's not water or already occupied by a tree
                if (std::find(tile_tags.begin(), tile_tags.end(), TileTag::SOIL) != tile_tags.end() && cell.type != TileType::WATER && cell.tree == nullptr) {
                    // Increased chance for trees to appear
                    if (rand() % 100 < 15) { // <--- MODIFIED: Increased from 5 to 15 for more trees
                        spawnTree(x, y, possibleTrees[rand() % possibleTrees.size()]);
                    }
                }
            }
        }
    }
}

// Add this new helper function definition anywhere with your other function definitions

// NEW: Helper to get a dynamic list of buildable items based on unlocks
std::vector<std::pair<std::wstring, TileType>> getAvailableGizmos(ArchitectCategory category) {
    std::vector<std::pair<std::wstring, TileType>> availableGizmos;

    // A master list of all potential gizmos for each category
    std::map<ArchitectCategory, std::vector<std::pair<std::wstring, TileType>>> allGizmos = {
    { ArchitectCategory::STRUCTURE, {
        {L"Wall", TileType::WALL},
        {L"Stone Wall", TileType::STONE_WALL},
        {L"Stair Down", TileType::STAIR_DOWN},
        {L"Stair Up", TileType::STAIR_UP},
        {L"Wood Floor", TileType::WOOD_FLOOR},
        {L"Growing Zone", TileType::GROWING_ZONE},
        {L"Mine Shaft", TileType::MINE_SHAFT},
        {L"Windmill", TileType::WINDMILL},
        {L"Lighthouse", TileType::LIGHTHOUSE_BUILDING},
        {L"Column", TileType::COLUMN},
        {L"Acoustic Wall", TileType::ACOUSTIC_WALL_ITEM}, // Now correctly listed as building
        {L"Spike Trap", TileType::SPIKE_TRAP},             // Now correctly listed as building
        {L"Wind Turbine", TileType::WIND_TURBINE},
        {L"Server", TileType::SERVER},
        {L"Switch", TileType::SWITCH},
        {L"Router", TileType::ROUTER}
    }},

    { ArchitectCategory::FURNITURE, {
        {L"Chair", TileType::CHAIR},
        {L"Table", TileType::TABLE},
        {L"Theatre Stage", TileType::THEATRE_STAGE}, // NEW
        {L"Printing Press", TileType::PRINTING_PRESS_FURNITURE}, // NEW
        {L"Typewriter", TileType::TYPEWRITER}, // NEW
        {L"Radio", TileType::RADIO_FURNITURE}, // NEW
        {L"Computer", TileType::COMPUTER_FURNITURE}, // NEW
        {L"Telescope", TileType::TELESCOPE_FURNITURE},
        {L"Blackboard", TileType::BLACKBOARD_FURNITURE}, // NEW
        {L"School Chair", TileType::SCHOOL_CHAIR_FURNITURE}, // NEW
        {L"School Desk", TileType::SCHOOL_DESK_FURNITURE}, // NEW
        {L"Hospital Bed", TileType::HOSPITAL_BED} // NEW
    }},
    { ArchitectCategory::LIGHTS, {
        {L"Torch", TileType::TORCH},
        {L"Electric Lamp", TileType::ELECTRIC_LAMP} // NEW
    }},
    { ArchitectCategory::PRODUCTION, {
        {L"Research Bench", TileType::RESEARCH_BENCH},
        {L"Adv. Research Bench", TileType::ADVANCED_RESEARCH_BENCH},
        {L"Carpentry Workbench", TileType::CARPENTRY_WORKBENCH},
        {L"Stonecutting Table", TileType::STONECUTTING_TABLE},
        {L"Smithy", TileType::SMITHY},
        {L"Blast Furnace", TileType::BLAST_FURNACE},
        {L"Coining Mill", TileType::COINING_MILL}, // NEW
        {L"Drug Lab", TileType::DRUG_LAB}, // NEW
        {L"Assembly Line", TileType::ASSEMBLY_LINE}, // NEW
        {L"Hydroponics Basin", TileType::HYDROPONICS_BASIN} // NEW
    }},
        // Other categories like ORDERS and ZONES are handled separately
    };

    if (allGizmos.count(category)) {
        for (const auto& gizmo : allGizmos.at(category)) {
            // Check if the gizmo's TileType is in the set of unlocked buildings
            if (g_unlockedBuildings.count(gizmo.second)) {
                availableGizmos.push_back(gizmo);
            }
        }
    }
    return availableGizmos;
}

void updateUnlockedContent(const ResearchProject& project) {
    // This map translates the unlock string to the actual TileType enum
    // In updateUnlockedContent(), modify the static const std::map<std::wstring, TileType> unlockMap:
    // In updateUnlockedContent(), modify the static const std::map<std::wstring, TileType> unlockMap:
    static const std::map<std::wstring, TileType> unlockMap = {
        {L"Wall", TileType::WALL},
        {L"Carpentry Workbench", TileType::CARPENTRY_WORKBENCH},
        {L"Chair", TileType::CHAIR},
        {L"Table", TileType::TABLE},
        {L"Torch", TileType::TORCH},
        {L"Stonecutting Table", TileType::STONECUTTING_TABLE},
        {L"Smithy", TileType::SMITHY},
        {L"Blast Furnace", TileType::BLAST_FURNACE},
        {L"Advanced Research Bench", TileType::ADVANCED_RESEARCH_BENCH},
        {L"Stone Wall", TileType::STONE_WALL},
        {L"Growing Zone", TileType::GROWING_ZONE},
        {L"Mine Shaft", TileType::MINE_SHAFT},
        {L"Pickaxe", TileType::PICKAXE},
        {L"Raft", TileType::RAFT},
        {L"Spike Trap", TileType::SPIKE_TRAP},
        {L"Wheel", TileType::WHEEL},
        {L"Windmill", TileType::WINDMILL},
        {L"Coining Mill", TileType::COINING_MILL},
        {L"Coin", TileType::COIN},
        {L"Theatre Stage", TileType::THEATRE_STAGE},
        {L"Lens", TileType::LENS},
        {L"Glasses", TileType::GLASSES},
        {L"Blackboard", TileType::BLACKBOARD_FURNITURE},
        {L"School Chair", TileType::SCHOOL_CHAIR_FURNITURE},
        {L"School Desk", TileType::SCHOOL_DESK_FURNITURE},
        {L"Acoustic Wall", TileType::ACOUSTIC_WALL_ITEM},
        {L"Column", TileType::COLUMN},
        {L"Telescope", TileType::TELESCOPE_FURNITURE},
        {L"Drug Lab", TileType::DRUG_LAB},
        {L"Various Drugs", TileType::DRUGS_ITEM},
        {L"Chemical Equipment", TileType::CHEMICALS_ITEM},
        {L"Boat", TileType::BOAT_ITEM},
        {L"Lighthouse", TileType::LIGHTHOUSE_BUILDING},
        {L"Gunpowder", TileType::GUNPOWDER_ITEM},
        {L"Printing Press", TileType::PRINTING_PRESS_FURNITURE},
        {L"Typewriter", TileType::TYPEWRITER},
        {L"Hospital Bed", TileType::HOSPITAL_BED},
        {L"Hydroponics Basin", TileType::HYDROPONICS_BASIN},
        {L"Wire", TileType::WIRE},
        {L"Lightbulb", TileType::LIGHTBULB},
        {L"Electric Lamp", TileType::ELECTRIC_LAMP},
        {L"Assembly Line", TileType::ASSEMBLY_LINE},
        {L"Radio", TileType::RADIO_FURNITURE},
        {L"Computer", TileType::COMPUTER_FURNITURE},
        {L"Mouse", TileType::MOUSE},
        {L"Screen", TileType::SCREEN},
        {L"Keyboard", TileType::KEYBOARD},
        {L"Wind Turbine", TileType::WIND_TURBINE},
        {L"Server", TileType::SERVER},
        {L"Switch", TileType::SWITCH},
        {L"Router", TileType::ROUTER},
        {L"Smartphone", TileType::SMARTPHONE},
        {L"Tablet", TileType::TABLET},
        {L"Smartwatch", TileType::SMARTWATCH},
        {L"Recipe: Bow", TileType::BOW},
        {L"Recipe: Arrow", TileType::ARROW},
        {L"Recipe: Knight Armor", TileType::KNIGHT_ARMOR},
        {L"Recipe: Knight Weapons", TileType::KNIGHT_WEAPON}
    };

    for (const auto& unlockStr : project.unlocks) {
        if (unlockStr.rfind(L"Building: ", 0) == 0) { // Check if it starts with "Building: "
            std::wstring buildingName = unlockStr.substr(10); // Get the name after "Building: "
            if (unlockMap.count(buildingName)) {
                g_unlockedBuildings.insert(unlockMap.at(buildingName));
            }
        }
        // You can add more checks here for "Recipe: ", "Work: ", etc. in the future
    }
}

// NEW: Ore Generation Function
// Updated generateOresInStratum Function Definition
void generateOresInStratum(int z, const StratumInfo& sInfo,
    const std::set<TileType>& sedimentaryStones,
    const std::set<TileType>& igneousExtrusiveStones,
    const std::set<TileType>& igneousIntrusiveStones,
    const std::set<TileType>& metamorphicStones,
    const std::set<TileType>& allIgneousStones,
    const std::set<TileType>& allStones) {

    // Lambda to check if a TileType is in a given set of host stones (O(logN) complexity)
    auto is_stone_type = [&](TileType type, const std::set<TileType>& group) {
        return group.count(type) > 0;
        };

    // Helper for spreading veins/clusters (remains largely the same, but uses std::set for host stones)
    auto spread_ore = [&](int startX, int startY, int startZ, TileType oreType, const std::set<TileType>& allowedHostStones, int density, int max_spread, bool linear = false) {
        if (startX < 0 || startX >= WORLD_WIDTH || startY < 0 || startY >= WORLD_HEIGHT || startZ < 0 || startZ >= TILE_WORLD_DEPTH) return;

        // Use a 2D array for visited flags for speed and direct indexing
        std::vector<std::vector<bool>> visited_map(WORLD_HEIGHT, std::vector<bool>(WORLD_WIDTH, false));
        std::queue<Point2D> q;

        q.push({ startX, startY });
        visited_map[startY][startX] = true;

        int tiles_placed = 0;
        int dx_linear = (rand() % 3) - 1;
        int dy_linear = (rand() % 3) - 1;
        if (dx_linear == 0 && dy_linear == 0) dx_linear = 1;

        while (!q.empty() && tiles_placed < max_spread) {
            Point2D current = q.front();
            q.pop();

            if (current.x < 0 || current.x >= WORLD_WIDTH || current.y < 0 || current.y >= WORLD_HEIGHT) continue;

            MapCell& current_cell = Z_LEVELS[startZ][current.y][current.x];
            // Check underlying_type against allowed host stones using the optimized is_stone_type
            bool isHostType = is_stone_type(current_cell.underlying_type, allowedHostStones);

            // Only place ore if it's a suitable host stone, not already an ore, and not an empty cave
            if (isHostType &&
                std::find(TILE_DATA.at(current_cell.type).tags.begin(), TILE_DATA.at(current_cell.type).tags.end(), TileTag::ORE) == TILE_DATA.at(current_cell.type).tags.end() &&
                current_cell.type != TileType::EMPTY)
            {
                current_cell.type = oreType;
                tiles_placed++;
            }

            if (linear) {
                int nx = current.x + dx_linear, ny = current.y + dy_linear;
                if (nx >= 0 && nx < WORLD_WIDTH && ny >= 0 && ny < WORLD_HEIGHT && !visited_map[ny][nx]) {
                    visited_map[ny][nx] = true;
                    q.push({ nx, ny });
                }
            }
            else { // Cluster spreading
                for (int cy = -1; cy <= 1; ++cy) {
                    for (int cx = -1; cx <= 1; ++cx) {
                        if (cx == 0 && cy == 0) continue;
                        if (rand() % 100 < density) { // Density based spread
                            int nx = current.x + cx, ny = current.y + cy;
                            if (nx >= 0 && nx < WORLD_WIDTH && ny >= 0 && ny < WORLD_HEIGHT && !visited_map[ny][nx]) {
                                visited_map[ny][nx] = true;
                                q.push({ nx, ny });
                            }
                        }
                    }
                }
            }
        }
        }; // End of spread_ore lambda


    // Define specific ores and their host stone types (these are now std::set<TileType>)
    // Removed ore types from host lists where they appeared as their own hosts.

    TileType oreIron = TileType::HEMATITE_ORE;
    std::set<TileType> iron_host_types = allStones; // Iron can appear in all stones for simplicity

    TileType oreCopper = TileType::NATIVE_COPPER_ORE;
    std::set<TileType> copper_host_types = { TileType::BASALT, TileType::ANDESITE, TileType::GABBRO, TileType::SCHIST };

    TileType oreTin = TileType::CASSITERITE_ORE;
    std::set<TileType> tin_host_types = { TileType::GRANITE, TileType::QUARTZITE, TileType::DIORITE };

    TileType oreZinc = TileType::SPHALERITE_ORE;
    std::set<TileType> zinc_host_types = { TileType::LIMESTONE, TileType::DOLOMITE, TileType::CHERT };

    TileType oreLead = TileType::GALENA_ORE;
    std::set<TileType> lead_host_types = { TileType::LIMESTONE, TileType::DOLOMITE, TileType::SHALE };

    TileType oreSilver = TileType::NATIVE_SILVER_ORE;
    std::set<TileType> silver_host_types = { TileType::QUARTZITE, TileType::SLATE };

    TileType oreGold = TileType::NATIVE_GOLD_ORE;
    std::set<TileType> gold_host_types = { TileType::QUARTZITE, TileType::GRANITE, TileType::SCHIST };

    TileType oreTungsten = TileType::WOLFRAMITE_ORE;
    std::set<TileType> tungsten_host_types = { TileType::GNEISS, TileType::MARBLE, TileType::GRANITE };

    TileType orePlatinum = TileType::NATIVE_PLATINUM_ORE;
    std::set<TileType> platinum_host_types = { TileType::GABBRO, TileType::BASALT };

    TileType oreAluminum = TileType::NATIVE_ALUMINUM_ORE;
    std::set<TileType> aluminum_host_types = { TileType::CLAYSTONE, TileType::MUDSTONE, TileType::SHALE };

    TileType oreBismuth = TileType::BISMUTHINITE_ORE;
    std::set<TileType> bismuth_host_types = { TileType::GRANITE, TileType::QUARTZITE };

    TileType oreOsmium = TileType::OSMIUM_METAL;
    std::set<TileType> osmium_host_types = { TileType::GABBRO, TileType::GNEISS };

    TileType oreIridium = TileType::IRIDIUM_METAL;
    std::set<TileType> iridium_host_types = { TileType::GABBRO, TileType::BASALT };

    TileType oreCobalt = TileType::COBALTITE_ORE;
    std::set<TileType> cobalt_host_types = { TileType::SCHIST, TileType::GNEISS };

    TileType orePalladium = TileType::PALLADINITE_ORE;
    std::set<TileType> palladium_host_types = { TileType::BASALT, TileType::GABBRO };

    TileType oreMithril = TileType::MITHRITE_ORE;
    std::set<TileType> mithril_host_types = { TileType::MARBLE, TileType::QUARTZITE, TileType::GNEISS };

    TileType oreOrichalcum = TileType::ORICALCITE_ORE;
    std::set<TileType> orichalcum_host_types = { TileType::DIORITE, TileType::GRANITE, TileType::GABBRO };

    TileType oreAdamantium = TileType::ADAMANTITE_ORE;
    std::set<TileType> adamantite_host_types = { TileType::CORESTONE };

    TileType oreTitanium = TileType::RUTILE_ORE;
    std::set<TileType> titanium_host_types = { TileType::SCHIST, TileType::GNEISS };


    // New Generation Logic: Generate a fixed number of ore veins (this part is efficient now)
    if (sInfo.type == Stratum::CRUST || sInfo.type == Stratum::LITHOSPHERE) { // Superficial ores
        int numVeins = 5 + rand() % 5; // Generate 5 to 9 veins of each common ore per layer
        for (int i = 0; i < numVeins; ++i) {
            int startX = rand() % WORLD_WIDTH;
            int startY = rand() % WORLD_HEIGHT;

            // Check if underlying tile is a valid host stone and not an empty cave
            if (is_stone_type(Z_LEVELS[z][startY][startX].underlying_type, iron_host_types) && Z_LEVELS[z][startY][startX].type != TileType::EMPTY)
                spread_ore(startX, startY, z, oreIron, iron_host_types, 60, 20, false);

            if (is_stone_type(Z_LEVELS[z][startY][startX].underlying_type, copper_host_types) && Z_LEVELS[z][startY][startX].type != TileType::EMPTY)
                spread_ore(startX, startY, z, oreCopper, copper_host_types, 55, 18, false);

            if (is_stone_type(Z_LEVELS[z][startY][startX].underlying_type, tin_host_types) && Z_LEVELS[z][startY][startX].type != TileType::EMPTY)
                spread_ore(startX, startY, z, oreTin, tin_host_types, 50, 15, false);

            if (is_stone_type(Z_LEVELS[z][startY][startX].underlying_type, zinc_host_types) && Z_LEVELS[z][startY][startX].type != TileType::EMPTY)
                spread_ore(startX, startY, z, oreZinc, zinc_host_types, 45, 15, false);

            if (is_stone_type(Z_LEVELS[z][startY][startX].underlying_type, lead_host_types) && Z_LEVELS[z][startY][startX].type != TileType::EMPTY)
                spread_ore(startX, startY, z, oreLead, lead_host_types, 40, 12, false);
        }
    }
    else if (sInfo.type == Stratum::ASTHENOSPHERE) { // Deeper ores
        int numVeins = 3 + rand() % 4; // Generate 3 to 6 veins of each deeper ore
        for (int i = 0; i < numVeins; ++i) {
            int startX = rand() % WORLD_WIDTH;
            int startY = rand() % WORLD_HEIGHT;

            if (is_stone_type(Z_LEVELS[z][startY][startX].underlying_type, gold_host_types) && Z_LEVELS[z][startY][startX].type != TileType::EMPTY)
                spread_ore(startX, startY, z, oreGold, gold_host_types, 70, 25, false);

            if (is_stone_type(Z_LEVELS[z][startY][startX].underlying_type, silver_host_types) && Z_LEVELS[z][startY][startX].type != TileType::EMPTY)
                spread_ore(startX, startY, z, oreSilver, silver_host_types, 65, 22, false);

            if (is_stone_type(Z_LEVELS[z][startY][startX].underlying_type, tungsten_host_types) && Z_LEVELS[z][startY][startX].type != TileType::EMPTY)
                spread_ore(startX, startY, z, oreTungsten, tungsten_host_types, 80, 30, false);

            if (is_stone_type(Z_LEVELS[z][startY][startX].underlying_type, platinum_host_types) && Z_LEVELS[z][startY][startX].type != TileType::EMPTY)
                spread_ore(startX, startY, z, orePlatinum, platinum_host_types, 75, 28, false);

            if (is_stone_type(Z_LEVELS[z][startY][startX].underlying_type, aluminum_host_types) && Z_LEVELS[z][startY][startX].type != TileType::EMPTY)
                spread_ore(startX, startY, z, oreAluminum, aluminum_host_types, 60, 20, false);

            if (is_stone_type(Z_LEVELS[z][startY][startX].underlying_type, bismuth_host_types) && Z_LEVELS[z][startY][startX].type != TileType::EMPTY)
                spread_ore(startX, startY, z, oreBismuth, bismuth_host_types, 60, 20, false);
        }
    }
    else if (sInfo.type == Stratum::UPPER_MANTLE || sInfo.type == Stratum::LOWER_MANTLE) {
        int numVeins = 2 + rand() % 3; // Generate 2 to 4 veins
        for (int i = 0; i < numVeins; ++i) {
            int startX = rand() % WORLD_WIDTH;
            int startY = rand() % WORLD_HEIGHT;

            if (is_stone_type(Z_LEVELS[z][startY][startX].underlying_type, cobalt_host_types) && Z_LEVELS[z][startY][startX].type != TileType::EMPTY)
                spread_ore(startX, startY, z, oreCobalt, cobalt_host_types, 85, 35, false);

            if (is_stone_type(Z_LEVELS[z][startY][startX].underlying_type, palladium_host_types) && Z_LEVELS[z][startY][startX].type != TileType::EMPTY)
                spread_ore(startX, startY, z, orePalladium, palladium_host_types, 80, 30, false);

            if (is_stone_type(Z_LEVELS[z][startY][startX].underlying_type, titanium_host_types) && Z_LEVELS[z][startY][startX].type != TileType::EMPTY)
                spread_ore(startX, startY, z, oreTitanium, titanium_host_types, 90, 40, false);
        }
    }
    else if (sInfo.type == Stratum::INNER_CORE) { // Fictional super rare ores
        int numVeins = 1 + rand() % 2; // Generate 1 to 2 veins
        for (int i = 0; i < numVeins; ++i) {
            int startX = rand() % WORLD_WIDTH;
            int startY = rand() % WORLD_HEIGHT;

            if (is_stone_type(Z_LEVELS[z][startY][startX].underlying_type, adamantite_host_types) && Z_LEVELS[z][startY][startX].type != TileType::EMPTY)
                spread_ore(startX, startY, z, oreAdamantium, adamantite_host_types, 95, 50, false);

            if (is_stone_type(Z_LEVELS[z][startY][startX].underlying_type, mithril_host_types) && Z_LEVELS[z][startY][startX].type != TileType::EMPTY)
                spread_ore(startX, startY, z, oreMithril, mithril_host_types, 90, 45, false);

            if (is_stone_type(Z_LEVELS[z][startY][startX].underlying_type, orichalcum_host_types) && Z_LEVELS[z][startY][startX].type != TileType::EMPTY)
                spread_ore(startX, startY, z, oreOrichalcum, orichalcum_host_types, 90, 45, false);
        }
    }
}


void FindAllContinents(std::vector<std::vector<Biome>>& pmap, std::vector<std::vector<POINT>>& outContinents) {
    std::vector<std::vector<bool>> visited(PLANET_MAP_HEIGHT, std::vector<bool>(PLANET_MAP_WIDTH, false));

    for (int y = 0; y < PLANET_MAP_HEIGHT; ++y) {
        for (int x = 0; x < PLANET_MAP_WIDTH; ++x) {
            if (pmap[y][x] != Biome::OCEAN && !visited[y][x]) {
                std::vector<POINT> currentContinent;
                std::queue<POINT> q;
                q.push({ (long)x, (long)y });
                visited[y][x] = true;
                while (!q.empty()) {
                    POINT p = q.front(); q.pop();
                    currentContinent.push_back(p);
                    for (int dy = -1; dy <= 1; ++dy) {
                        for (int dx = -1; dx <= 1; ++dx) {
                            if (dx == 0 && dy == 0) continue;
                            int nx = p.x + dx, ny = p.y + dy;
                            if (nx >= 0 && nx < PLANET_MAP_WIDTH && ny >= 0 && ny < PLANET_MAP_HEIGHT && !visited[ny][nx] && pmap[ny][nx] != Biome::OCEAN) {
                                visited[ny][nx] = true;
                                q.push({ (long)nx, (long)ny });
                            }
                        }
                    }
                }
                outContinents.push_back(currentContinent);
            }
        }
    }
}
void generatePlanetMap(Planet& planet) {
    planet.biomeMap.assign(PLANET_MAP_HEIGHT, std::vector<Biome>(PLANET_MAP_WIDTH, Biome::OCEAN));
    std::vector<std::vector<Biome>>& pmap = planet.biomeMap;

    if (planet.type == WorldType::SINGLE_CONTINENT) {
        float centerX = PLANET_MAP_WIDTH / 2.0f;
        float centerY = PLANET_MAP_HEIGHT / 2.0f;
        float radius = (PLANET_MAP_HEIGHT / 2.0f) * 0.9f;
        float beachWidth = 5.0f;

        for (int y = 0; y < PLANET_MAP_HEIGHT; ++y) {
            for (int x = 0; x < PLANET_MAP_WIDTH; ++x) {
                float dx = x - centerX, dy = y - centerY;
                float dist = sqrt(dx * dx + dy * dy);
                if (dist < radius) { // It's land
                    if (dist > radius - beachWidth) { // It's a beach
                        pmap[y][x] = Biome::DESERT; // Uses sand tiles
                    }
                    else { // It's inland
                        float latitude_percent = (float)y / PLANET_MAP_HEIGHT;
                        if (latitude_percent < 0.15 || latitude_percent > 0.85) pmap[y][x] = Biome::TUNDRA;
                        else if (latitude_percent < 0.3 || latitude_percent > 0.7) pmap[y][x] = Biome::BOREAL_FOREST;
                        else if (latitude_percent < 0.4 || latitude_percent > 0.6) pmap[y][x] = Biome::TEMPERATE_FOREST;
                        else pmap[y][x] = (rand() % 3 == 0) ? Biome::DESERT : Biome::JUNGLE;
                    }
                }
            }
        }
    }
    else { // Handle Earth-Like and Archipelago with a more advanced method

        // Step 1: Cellular Automata to create natural landmasses
        std::vector<std::vector<int>> noiseMap(PLANET_MAP_HEIGHT, std::vector<int>(PLANET_MAP_WIDTH));
        for (int y = 0; y < PLANET_MAP_HEIGHT; ++y) for (int x = 0; x < PLANET_MAP_WIDTH; ++x) noiseMap[y][x] = (rand() % 100 < 45) ? 1 : 0;

        for (int i = 0; i < 4; ++i) { // 4 smoothing iterations
            std::vector<std::vector<int>> newNoiseMap = noiseMap;
            for (int y = 0; y < PLANET_MAP_HEIGHT; ++y) for (int x = 0; x < PLANET_MAP_WIDTH; ++x) {
                int wallCount = 0;
                for (int ny = -1; ny <= 1; ++ny) for (int nx = -1; nx <= 1; ++nx) {
                    if (ny == 0 && nx == 0) continue;
                    int checkX = x + nx, checkY = y + ny;
                    if (checkX >= 0 && checkX < PLANET_MAP_WIDTH && checkY >= 0 && checkY < PLANET_MAP_HEIGHT) {
                        if (noiseMap[checkY][checkX] == 1) wallCount++;
                    }
                    else { wallCount++; } // Count borders as walls
                }
                if (noiseMap[y][x] == 1) newNoiseMap[y][x] = (wallCount >= 4) ? 1 : 0;
                else newNoiseMap[y][x] = (wallCount >= 5) ? 1 : 0;
            }
            noiseMap = newNoiseMap;
        }

        // Step 2: Set preliminary biome map from noise
        for (int y = 0; y < PLANET_MAP_HEIGHT; ++y) for (int x = 0; x < PLANET_MAP_WIDTH; ++x) {
            if (noiseMap[y][x] == 1) {
                float latitude_percent = (float)y / PLANET_MAP_HEIGHT;
                if (latitude_percent < 0.1 || latitude_percent > 0.9) pmap[y][x] = Biome::TUNDRA;
                else if (latitude_percent < 0.2 || latitude_percent > 0.8) pmap[y][x] = Biome::BOREAL_FOREST;
                else if (latitude_percent < 0.4 || latitude_percent > 0.6) pmap[y][x] = Biome::TEMPERATE_FOREST;
                else { pmap[y][x] = (rand() % 2 == 0) ? Biome::DESERT : Biome::JUNGLE; }
            }
            else { pmap[y][x] = Biome::OCEAN; }
        }

        // Step 3: Prune smaller continents for "Earth-like" worlds
        if (planet.type == WorldType::EARTH_LIKE) {
            std::vector<std::vector<POINT>> allContinents;
            FindAllContinents(pmap, allContinents);

            std::sort(allContinents.begin(), allContinents.end(), [](const auto& a, const auto& b) { return a.size() > b.size(); });

            int continentsToKeep = 5; // A good number for "Earth-like"
            for (size_t i = continentsToKeep; i < allContinents.size(); ++i) {
                for (const auto& p : allContinents[i]) { pmap[p.y][p.x] = Biome::OCEAN; }
            }
        }
    }
}
void generateSolarSystem(int numPlanets, bool preserveNames = false) {
    std::vector<std::wstring> oldNames;
    if (preserveNames && !solarSystem.empty()) { for (const auto& p : solarSystem) oldNames.push_back(p.name); }
    solarSystem.clear();
    for (int i = 0; i < numPlanets; ++i) {
        Planet p;
        if (preserveNames && i < oldNames.size()) p.name = oldNames[i];
        else p.name = L"Planet " + std::to_wstring(i + 1);
        p.type = static_cast<WorldType>(rand() % 3);
        p.orbitalRadius = 50.0 + i * 40.0 + (rand() % 20); p.currentAngle = (rand() % 360) * 3.14159 / 180.0;
        p.orbitalSpeed = 0.001 + (rand() % 5) * 0.0005 / (i + 1); p.color = RGB(rand() % 200 + 55, rand() % 200 + 55, rand() % 200 + 55);
        p.size = 5 + rand() % 6; solarSystem.push_back(p);
    }
    homeMoon.orbitalRadius = 15.0; homeMoon.currentAngle = (rand() % 360) * 3.14159 / 180.0;
    homeMoon.orbitalSpeed = 0.01; homeMoon.color = RGB(200, 200, 200); homeMoon.size = 2;
}
void generateDistantStars() {
    distantStars.clear();
    g_homeSystemStarIndex = -1;

    // First, create the player's home system star
    Star homeStar;
    homeStar.x = 0.5f; // Start it in the horizontal center
    homeStar.y = 0.5f; // Start it in the vertical center
    homeStar.dx = -((rand() % 50) / 10000.0f) - 0.0001f; // Give it a standard speed
    homeStar.size = 2; // Make it slightly larger so it's noticeable
    homeStar.color = RGB(255, 255, 0); // Make it distinctly yellow
    distantStars.push_back(homeStar);
    g_homeSystemStarIndex = 0; // It's the first star in the vector

    // Now, generate the rest of the random stars
    for (int i = 0; i < 399; ++i) { // One less because we already made the home star
        Star s;
        s.x = static_cast<float>(rand()) / static_cast<float>(RAND_MAX);
        s.y = static_cast<float>(rand()) / static_cast<float>(RAND_MAX);
        s.dx = -((rand() % 50) / 10000.0f) - 0.0001f;
        s.size = 1 + (rand() % 100 < 5);

        int colorChance = rand() % 100;
        if (colorChance < 5) {
            s.color = RGB(200, 200, 255);
        }
        else if (colorChance < 10) {
            s.color = RGB(255, 255, 200);
        }
        else {
            s.color = RGB(220, 220, 220);
        }

        distantStars.push_back(s);
    }
}
void preparePawnSelection() { rerollablePawns.clear(); for (int i = 0; i < 3; ++i) rerollablePawns.push_back(generatePawn()); }
COLORREF applyLightLevel(COLORREF originalColor, float lightLevel) {
    BYTE r = (BYTE)(GetRValue(originalColor) * lightLevel);
    BYTE g = (BYTE)(GetGValue(originalColor) * lightLevel);
    BYTE b = (BYTE)(GetBValue(originalColor) * lightLevel);
    return RGB(r, g, b);
}
void renderWrappedText(HDC hdc, const std::wstring& text, RECT& rect, COLORREF color = RGB(255, 255, 255)) { SetTextColor(hdc, color); DrawText(hdc, text.c_str(), -1, &rect, DT_WORDBREAK | DT_NOCLIP); }
void renderMainMenu(HDC hdc, int width, int height) {
    // NEW: Check if we should be rendering the font menu instead
    if (isInFontMenu) {
        renderFontMenu(hdc, width, height);
        return;
    }

    RENDER_CENTERED_TEXT_INSPECTABLE(hdc, L"=== Colony Management ===", 100, width, RGB(255, 255, 255), L"Game Title");

    // MODIFIED: Added a new option
    std::vector<std::wstring> options = { L"Start New Game", L"Change Font", L"Exit" };

    for (size_t i = 0; i < options.size(); ++i) {
        RENDER_CENTERED_TEXT_INSPECTABLE(hdc, options[i], 150 + (i * 30), width, (i == menuUI_selectedOption) ? RGB(255, 255, 0) : RGB(255, 255, 255), L"Menu Option: " + options[i]);
    }
    RENDER_CENTERED_TEXT_INSPECTABLE(hdc, L"Use Up/Down to select, Enter/Space/Z to confirm.", height - 60, width, RGB(150, 150, 150), L"Control Hint");
}
void renderFontMenu(HDC hdc, int width, int height) {
    RENDER_CENTERED_TEXT_INSPECTABLE(hdc, L"=== Change Font ===", 100, width, RGB(255, 255, 255));

    int y = 150;
    for (size_t i = 0; i < g_availableFonts.size(); ++i) {
        std::wstringstream ss;
        ss << g_availableFonts[i];

        // Mark the currently active font
        if ((g_availableFonts[i] == L"(Default)" && g_currentFontName == L"Consolas") || (g_availableFonts[i] == g_currentFontName)) {
            ss << L"  <- Current";
        }
        
        RENDER_CENTERED_TEXT_INSPECTABLE(hdc, ss.str(), y, width, (i == fontMenu_selectedOption) ? RGB(255, 255, 0) : RGB(255, 255, 255));
        y += 25;
    }

    RENDER_CENTERED_TEXT_INSPECTABLE(hdc, L"Use Up/Down to select, Enter/Space/Z to confirm. ESC to go back.", height - 60, width, RGB(150, 150, 150));
}
void renderWorldGenerationMenu(HDC hdc, int width, int height) {
    int centerX = width / 2;
    int startY = height / 3 - 40; // Moved startY up a bit to make space
    int optionHeight = 25;

    RENDER_TEXT_INSPECTABLE(hdc, L"World Name:", centerX - 250, startY, RGB(255, 255, 255));
    RECT nameBox = { centerX - 100, startY - 5, centerX + 100, startY + optionHeight };
    std::wstring nameDisplay = worldName;
    if (worldGen_selectedOption == 0 && worldGen_isNaming && (GetTickCount() / 500) % 2) nameDisplay += L"_";
    RENDER_TEXT_INSPECTABLE(hdc, nameDisplay, nameBox.left + 5, startY, RGB(255, 255, 255), L"Input Field (Editable on Enter)");
    RENDER_BOX_INSPECTABLE(hdc, nameBox, worldGen_selectedOption == 0 ? RGB(255, 255, 0) : RGB(128, 128, 128), L"Selected Option: World Name");
    startY += optionHeight + 10;

    // --- START: ADDED SOLAR SYSTEM NAME INPUT ---
    RENDER_TEXT_INSPECTABLE(hdc, L"System Name:", centerX - 250, startY, RGB(255, 255, 255));
    RECT systemNameBox = { centerX - 100, startY - 5, centerX + 100, startY + optionHeight };
    std::wstring systemNameDisplay = solarSystemName;
    if (worldGen_selectedOption == 1 && worldGen_isNaming && (GetTickCount() / 500) % 2) systemNameDisplay += L"_";
    RENDER_TEXT_INSPECTABLE(hdc, systemNameDisplay, systemNameBox.left + 5, startY, RGB(255, 255, 255), L"Input Field (Editable on Enter)");
    RENDER_BOX_INSPECTABLE(hdc, systemNameBox, worldGen_selectedOption == 1 ? RGB(255, 255, 0) : RGB(128, 128, 128), L"Selected Option: System Name");
    startY += optionHeight + 10;
    // --- END: ADDED SOLAR SYSTEM NAME INPUT ---

    RENDER_TEXT_INSPECTABLE(hdc, L"Planet Count:", centerX - 250, startY, RGB(255, 255, 255));
    RECT countBox = { centerX - 100, startY - 5, centerX + 100, startY + optionHeight };
    std::wstring countDisplay = L"< " + std::to_wstring(numberOfPlanets) + L" >";
    RENDER_TEXT_INSPECTABLE(hdc, countDisplay, countBox.left + 5, startY, RGB(255, 255, 255), L"Value Selector (Left/Right to change)");
    RENDER_BOX_INSPECTABLE(hdc, countBox, worldGen_selectedOption == 2 ? RGB(255, 255, 0) : RGB(128, 128, 128), L"Selected Option: Planet Count");
    startY += optionHeight + 10;

    RENDER_TEXT_INSPECTABLE(hdc, L"World Type:", centerX - 250, startY, RGB(255, 255, 255));
    RECT typeBox = { centerX - 100, startY - 5, centerX + 100, startY + optionHeight };
    std::wstring typeDisplay = L"< " + WorldTypeNames[static_cast<int>(selectedWorldType)] + L" >";
    RENDER_TEXT_INSPECTABLE(hdc, typeDisplay, typeBox.left + 5, startY, RGB(255, 255, 255), L"Value Selector (Left/Right to change)");
    RENDER_BOX_INSPECTABLE(hdc, typeBox, worldGen_selectedOption == 3 ? RGB(255, 255, 0) : RGB(128, 128, 128), L"Selected Option: World Type");
    startY += optionHeight + 10;

    RENDER_CENTERED_TEXT_INSPECTABLE(hdc, L"Customize Planets...", startY, width, worldGen_selectedOption == 4 ? RGB(255, 255, 0) : RGB(255, 255, 255), L"Button: Go to Planet Customization Menu");
    startY += optionHeight + 10;

    RENDER_CENTERED_TEXT_INSPECTABLE(hdc, L"Finalize and Proceed", startY, width, worldGen_selectedOption == 5 ? RGB(0, 255, 0) : RGB(0, 255, 128), L"Button: Finalize and begin world generation");

    RENDER_CENTERED_TEXT_INSPECTABLE(hdc, L"Up/Down to select, Left/Right to change, Enter to confirm. ESC to go back.", height - 60, width, RGB(150, 150, 150), L"Control Hint");
}
void renderPlanetCustomizationMenu(HDC hdc, int width, int height) {
    RENDER_CENTERED_TEXT_INSPECTABLE(hdc, L"Customize Planets", 100, width, RGB(255, 255, 255), L"Menu Title");
    int y = 150;
    for (size_t i = 0; i < solarSystem.size(); ++i) {
        std::wstring nameDisplay = solarSystem[i].name;
        if (planetCustomization_isEditing && planetCustomization_selected == i && (GetTickCount() / 500) % 2) { nameDisplay += L"_"; }
        RENDER_CENTERED_TEXT_INSPECTABLE(hdc, nameDisplay, y, width, (planetCustomization_selected == i) ? RGB(255, 255, 0) : RGB(255, 255, 255), L"Planet Name (Editable)");
        y += 25;
    }
    RENDER_CENTERED_TEXT_INSPECTABLE(hdc, L"Up/Down to select, Enter to edit. Press ESC when finished.", height - 60, width, RGB(150, 150, 150), L"Control Hint");
}

// Helper functions for landing site selection
std::wstring generateContinentName() {
    const std::vector<std::wstring> prefixes = { L"Aka", L"Bora", L"Cor", L"Dra", L"El", L"Fen", L"Gor", L"Hel", L"Ish", L"Jen", L"Kel", L"Lumar" };
    const std::vector<std::wstring> middles = { L"ma", L"to", L"lan", L"gar", L"the", L"ni", L"si", L"lo", L"ra", L"goth", L"shen" };
    const std::vector<std::wstring> suffixes = { L"ia", L"os", L"a", L"dor", L"eth", L" Prime", L" Minor", L" Major", L"a-kar", L"a-sul" };
    return prefixes[rand() % prefixes.size()] + middles[rand() % middles.size()] + suffixes[rand() % suffixes.size()];
}
ContinentInfo findContinentInfo(int startX, int startY) {
    ContinentInfo info;
    if (solarSystem.empty() || solarSystem[0].biomeMap[startY][startX] == Biome::OCEAN) {
        return info; // Return empty info with found=false
    }

    std::queue<POINT> q;
    std::vector<std::vector<bool>> visited(PLANET_MAP_HEIGHT, std::vector<bool>(PLANET_MAP_WIDTH, false));

    q.push({ (long)startX, (long)startY });
    visited[startY][startX] = true;

    long long sumX = 0;
    double sumTemp = 0.0;
    std::map<Biome, int> biomeBaseTemp = { {Biome::TUNDRA, -15}, {Biome::BOREAL_FOREST, 5}, {Biome::TEMPERATE_FOREST, 15}, {Biome::JUNGLE, 25}, {Biome::DESERT, 30} };


    while (!q.empty()) {
        POINT p = q.front();
        q.pop();

        info.tiles.push_back(p);
        Biome currentBiome = solarSystem[0].biomeMap[p.y][p.x];
        info.biomes.insert(currentBiome);
        sumX += p.x;
        sumTemp += biomeBaseTemp.count(currentBiome) ? biomeBaseTemp.at(currentBiome) : 10;

        for (int dy = -1; dy <= 1; ++dy) {
            for (int dx = -1; dx <= 1; ++dx) {
                if (dx == 0 && dy == 0) continue;
                int nx = p.x + dx, ny = p.y + dy;
                if (nx >= 0 && nx < PLANET_MAP_WIDTH && ny >= 0 && ny < PLANET_MAP_HEIGHT && !visited[ny][nx]) {
                    visited[ny][nx] = true;
                    if (solarSystem[0].biomeMap[ny][nx] != Biome::OCEAN) {
                        q.push({ (long)nx, (long)ny });
                    }
                }
            }
        }
    }

    if (!info.tiles.empty()) {
        info.found = true;
        std::sort(info.tiles.begin(), info.tiles.end(), [](const POINT& a, const POINT& b) {
            if (a.y != b.y) return a.y < b.y;
            return a.x < b.x;
            });
        srand(info.tiles[0].y * PLANET_MAP_WIDTH + info.tiles[0].x);
        info.name = generateContinentName();
        srand(static_cast<unsigned>(time(0))); // Reset seed for other randomness

        info.avgTemp = sumTemp / info.tiles.size();
        float avgX = static_cast<float>(sumX) / info.tiles.size();
        info.timezoneOffset = ((avgX / PLANET_MAP_WIDTH) * 24.0f) - 12.0f;
    }

    return info;
}
bool isOceanOrOutOfBounds(int x, int y) {
    if (x < 0 || x >= PLANET_MAP_WIDTH || y < 0 || y >= PLANET_MAP_HEIGHT) return true;
    return solarSystem[0].biomeMap[y][x] == Biome::OCEAN;
}

void renderLandingSiteSelection(HDC hdc, int width, int height) {
    RENDER_CENTERED_TEXT_INSPECTABLE(hdc, L"=== Select Landing Continent ===", 50, width, RGB(255, 255, 255), L"Menu Title");
    RENDER_CENTERED_TEXT_INSPECTABLE(hdc, L"Use arrow keys to move. Press Enter/Space/Z to select.", 70, width, RGB(200, 200, 200), L"Control Hint");

    const int pixelSize = 4;
    int mapW = PLANET_MAP_WIDTH * pixelSize, mapH = PLANET_MAP_HEIGHT * pixelSize;
    int infoPanelWidth = 320;
    int ox = (width - mapW - infoPanelWidth) / 2;
    int oy = (height - mapH) / 2;

    RECT mapRect = { ox, oy, ox + mapW, oy + mapH };
    g_inspectorElements.push_back({ mapRect, L"Global Map of " + solarSystem[0].name });

    for (int y = 0; y < PLANET_MAP_HEIGHT; y++) {
        for (int x = 0; x < PLANET_MAP_WIDTH; x++) {
            RECT r = { ox + x * pixelSize, oy + y * pixelSize, ox + (x + 1) * pixelSize, oy + (y + 1) * pixelSize };
            HBRUSH brush = CreateSolidBrush(BIOME_DATA.at(solarSystem[0].biomeMap[y][x]).mapColor);
            FillRect(hdc, &r, brush);
            DeleteObject(brush);
        }
    }

    ContinentInfo continent = findContinentInfo(cursorX, cursorY);

    if (continent.found) {
        HPEN hPen = CreatePen(PS_SOLID, 2, RGB(255, 255, 0));
        HGDIOBJ hOldPen = SelectObject(hdc, hPen);
        for (const auto& p : continent.tiles) {
            int cx = p.x, cy = p.y;
            int screenX = ox + cx * pixelSize, screenY = oy + cy * pixelSize;
            if (isOceanOrOutOfBounds(cx, cy - 1)) { MoveToEx(hdc, screenX, screenY, NULL); LineTo(hdc, screenX + pixelSize, screenY); }
            if (isOceanOrOutOfBounds(cx, cy + 1)) { MoveToEx(hdc, screenX, screenY + pixelSize, NULL); LineTo(hdc, screenX + pixelSize, screenY + pixelSize); }
            if (isOceanOrOutOfBounds(cx - 1, cy)) { MoveToEx(hdc, screenX, screenY, NULL); LineTo(hdc, screenX, screenY + pixelSize); }
            if (isOceanOrOutOfBounds(cx + 1, cy)) { MoveToEx(hdc, screenX + pixelSize, screenY, NULL); LineTo(hdc, screenX + pixelSize, screenY + pixelSize); }
        }
        SelectObject(hdc, hOldPen);
        DeleteObject(hPen);
    }

    RECT panelRect = { width - infoPanelWidth - 20, 100, width - 20, height - 100 };
    HBRUSH panelBrush = CreateSolidBrush(RGB(10, 10, 30)); FillRect(hdc, &panelRect, panelBrush);
    RENDER_BOX_INSPECTABLE(hdc, panelRect, RGB(100, 100, 120), L"Continent Information Panel");
    DeleteObject(panelBrush);
    int px = panelRect.left + 15, py = panelRect.top + 15;

    if (continent.found) {
        RENDER_TEXT_INSPECTABLE(hdc, continent.name, px, py, RGB(255, 255, 0), L"Continent Name (Procedurally generated)");
        py += 30;
        wchar_t tempBuf[50];
        swprintf_s(tempBuf, L"Avg. Temp: %.1f C", continent.avgTemp);
        RENDER_TEXT_INSPECTABLE(hdc, tempBuf, px, py, RGB(255, 255, 255), L"Average Temperature on this continent");
        py += 20;
        swprintf_s(tempBuf, L"Timezone: UTC %+.1f", continent.timezoneOffset);
        RENDER_TEXT_INSPECTABLE(hdc, tempBuf, px, py, RGB(255, 255, 255), L"Local Time Zone based on longitude");
        py += 30;
        RENDER_TEXT_INSPECTABLE(hdc, L"Biomes:", px, py, RGB(255, 255, 255));
        py += 25;
        for (const auto& biome : continent.biomes) {
            RENDER_TEXT_INSPECTABLE(hdc, L" - " + BIOME_DATA.at(biome).name, px, py, BIOME_DATA.at(biome).mapColor, L"Biome Type: " + BIOME_DATA.at(biome).name);
            py += 20;
        }
    }
    else {
        RENDER_TEXT_INSPECTABLE(hdc, L"Vast Ocean", px, py, RGB(100, 150, 255), L"Ocean Tile");
    }

    RECT r = { ox + cursorX * pixelSize, oy + cursorY * pixelSize, ox + (cursorX + 1) * pixelSize, oy + (cursorY + 1) * pixelSize };
    HBRUSH brush = CreateSolidBrush(RGB(255, 255, 0));
    FrameRect(hdc, &r, brush);
    g_inspectorElements.push_back({ r, L"Landing Site Cursor" });
    DeleteObject(brush);
}
void renderRegionSelection(HDC hdc, int width, int height) {
    RENDER_CENTERED_TEXT_INSPECTABLE(hdc, L"=== Select Exact Landing Site ===", 20, width, RGB(255, 255, 0), L"Menu Title");
    RENDER_CENTERED_TEXT_INSPECTABLE(hdc, L"Use arrow keys to move. Press ENTER to confirm.", 40, width, RGB(200, 200, 200), L"Control Hint");
    renderGame(hdc, width, height);
}
void renderColonistSelection(HDC hdc, int width, int height) {
    RENDER_CENTERED_TEXT_INSPECTABLE(hdc, L"Choose Colonists", 80, width, RGB(255, 255, 255), L"Menu Title");
    int startX = 150, colWidth = 350;
    for (size_t i = 0; i < rerollablePawns.size(); ++i) {
        const auto& pawn = rerollablePawns[i];
        int x = startX + (i * colWidth), y = 150;
        RENDER_TEXT_INSPECTABLE(hdc, L"Colonist " + std::to_wstring(i + 1), x, y, RGB(255, 255, 0));
        y += 30;
        RENDER_TEXT_INSPECTABLE(hdc, L"Name: " + pawn.name + L" (" + pawn.gender.substr(0, 1) + L", " + std::to_wstring(pawn.age) + L")", x, y, RGB(255, 255, 255), L"Pawn Info: Name, Gender, Age");
        y += 20;
        std::wstring traitStr = L"Traits: ";
        for (size_t j = 0; j < pawn.traits.size(); ++j) { traitStr += pawn.traits[j] + (j < pawn.traits.size() - 1 ? L", " : L""); }
        RENDER_TEXT_INSPECTABLE(hdc, traitStr, x, y, RGB(255, 255, 255), L"Pawn Traits");
        y += 20;
        RENDER_TEXT_INSPECTABLE(hdc, L"Backstory: " + pawn.backstory, x, y, RGB(255, 255, 255), L"Pawn Backstory");
        y += 40;
        RENDER_TEXT_INSPECTABLE(hdc, L"Skills:", x, y, RGB(0, 255, 255), L"Pawn Skills Header");
        y += 25;
        RENDER_TEXT_INSPECTABLE(hdc, L"- Mining:       " + std::to_wstring(pawn.skills.at(L"Mining")), x, y, RGB(255, 255, 255), L"Skill Level: Mining");
        y += 20;
        RENDER_TEXT_INSPECTABLE(hdc, L"- Chopping:     " + std::to_wstring(pawn.skills.at(L"Chopping")), x, y, RGB(255, 255, 255), L"Skill Level: Chopping");
        y += 20;
        RENDER_TEXT_INSPECTABLE(hdc, L"- Construction: " + std::to_wstring(pawn.skills.at(L"Construction")), x, y, RGB(255, 255, 255), L"Skill Level: Construction");
        y += 20;
        RENDER_TEXT_INSPECTABLE(hdc, L"- Hauling:      " + std::to_wstring(pawn.skills.at(L"Hauling")), x, y, RGB(255, 255, 255), L"Skill Level: Hauling");
        y += 20;
        RENDER_TEXT_INSPECTABLE(hdc, L"- Research:     " + std::to_wstring(pawn.skills.at(L"Research")), x, y, RGB(255, 255, 255), L"Skill Level: Research");
    }
    RENDER_CENTERED_TEXT_INSPECTABLE(hdc, L"Press 'R' to Reroll. Press 'Enter/Space/Z' to start.", height - 80, width, RGB(0, 255, 128), L"Control Hint");
}
void renderPawnInfoPanel(HDC hdc, int width, int height) {
    if (inspectedPawnIndex < 0 || inspectedPawnIndex >= static_cast<int>(colonists.size())) return;
    Pawn& pawn = colonists[inspectedPawnIndex];

    // Panel Definitions
    RECT leftPanelRect = { 20, 80, 500, 450 };
    RECT rightPanelRect = { 510, 80, 990, 450 };

    // Draw Left Panel Background & Border
    HBRUSH panelBrush = CreateSolidBrush(RGB(10, 10, 20));
    FillRect(hdc, &leftPanelRect, panelBrush);
    RENDER_BOX_INSPECTABLE(hdc, leftPanelRect, RGB(100, 100, 120), L"Inspected Pawn Left Panel");

    int x = leftPanelRect.left + 15, y = leftPanelRect.top + 15;
    RENDER_TEXT_INSPECTABLE(hdc, pawn.name, x, y, RGB(255, 255, 0), L"Pawn Name");
    y += 30;

    // Draw Tabs
    std::vector<std::wstring> tabs = { L"Overview", L"Items", L"Health", L"Skills", L"Relations", L"Groups", L"Thoughts", L"Personality" };
    int tabX = x, tabY = y;
    int rowWidthLimit = leftPanelRect.right - x - 15;
    for (size_t i = 0; i < tabs.size(); ++i) {
        SIZE size; GetTextExtentPoint32(hdc, tabs[i].c_str(), static_cast<int>(tabs[i].length()), &size);
        if (tabX != x && tabX + size.cx > rowWidthLimit) { tabX = x; tabY += 20; }
        COLORREF color = (i == static_cast<size_t>(currentPawnInfoTab)) ? RGB(255, 255, 255) : RGB(150, 150, 150);
        HPEN hSelectedPen;
        if (i == static_cast<size_t>(currentPawnInfoTab)) {
            hSelectedPen = CreatePen(PS_SOLID, 1, RGB(255, 255, 255));
            HGDIOBJ hOldSelectedPen = SelectObject(hdc, hSelectedPen);
            MoveToEx(hdc, tabX, tabY + 16, NULL); LineTo(hdc, tabX + size.cx, tabY + 16);
            SelectObject(hdc, hOldSelectedPen); DeleteObject(hSelectedPen);
        }
        RENDER_TEXT_INSPECTABLE(hdc, tabs[i], tabX, tabY, color, L"Tab: " + tabs[i]);
        tabX += size.cx + 20;
    }

    y = tabY + 25; // Content start y

    // Prepare selectable content lines and detail information based on selection
    std::vector<std::pair<std::wstring, std::wstring>> selectableContent; // { "Display String", "Lookup Key" }
    std::wstring detailTitle, detailDescription;

    switch (currentPawnInfoTab) {
    case PawnInfoTab::OVERVIEW:
        selectableContent.push_back({ L"Status: " + pawn.currentTask, L"Status" });
        selectableContent.push_back({ L"Age: " + std::to_wstring(pawn.age), L"Age" });
        selectableContent.push_back({ L"Backstory: " + pawn.backstory, pawn.backstory });
        break;
    case PawnInfoTab::ITEMS:
        if (pawn.inventory.empty()) {
            selectableContent.push_back({ L"(Carrying nothing)", L"N/A" });
        }
        else {
            // NEW: Iterate through the map to show item stacks
            for (const auto& stack : pawn.inventory) {
                wchar_t buffer[100];
                swprintf_s(buffer, 100, L"%s x%d", TILE_DATA.at(stack.first).name.c_str(), stack.second);
                selectableContent.push_back({ buffer, L"Item" });
            }
        }
        break;
    case PawnInfoTab::SKILLS:
        for (const auto& skill : pawn.skills) {
            selectableContent.push_back({ skill.first + L": " + std::to_wstring(skill.second), skill.first });
        }
        break;
    default:
        selectableContent.push_back({ L"[This tab is not yet implemented.]", L"N/A" });
        break;
    }

    // 1. Clamp the selected line to be valid for the current list.
    if (!selectableContent.empty()) {
        pawnInfo_selectedLine = min((int)selectableContent.size() - 1, pawnInfo_selectedLine);
    }
    else {
        pawnInfo_selectedLine = 0;
    }

    // 2. Calculate scroll offset to keep the selected line in view.
    int line_height = 20;
    int maxVisibleItems = (leftPanelRect.bottom - y - 15) / line_height;
    int* currentOffset = (currentPawnInfoTab == PawnInfoTab::ITEMS) ? &pawnItems_scrollOffset : &pawnInfo_scrollOffset;

    if (pawnInfo_selectedLine < *currentOffset) {
        *currentOffset = pawnInfo_selectedLine;
    }
    else if (pawnInfo_selectedLine >= *currentOffset + maxVisibleItems) {
        *currentOffset = pawnInfo_selectedLine - maxVisibleItems + 1;
    }

    // Final clamp for the scroll offset itself
    if ((int)selectableContent.size() > 0) {
        *currentOffset = min(*currentOffset, (int)selectableContent.size() - maxVisibleItems);
        *currentOffset = max(0, *currentOffset);
    }
    else {
        *currentOffset = 0;
    }

    // 3. Render the selectable content lines
    int currentY = y;
    for (size_t i = *currentOffset; i < selectableContent.size() && (currentY + line_height < leftPanelRect.bottom - 15); ++i) {
        // The highlight is now based on pawnInfo_selectedLine
        COLORREF color = (i == pawnInfo_selectedLine) ? RGB(255, 255, 255) : RGB(150, 150, 150);
        if (i == pawnInfo_selectedLine) {
            SIZE size; GetTextExtentPoint32(hdc, selectableContent[i].first.c_str(), (int)selectableContent[i].first.length(), &size);
            HPEN selPen = CreatePen(PS_SOLID, 1, RGB(255, 255, 255));
            HGDIOBJ oldSelPen = SelectObject(hdc, selPen);
            MoveToEx(hdc, x, currentY + 16, NULL); LineTo(hdc, x + size.cx, currentY + 16);
            SelectObject(hdc, oldSelPen); DeleteObject(selPen);
        }
        RENDER_TEXT_INSPECTABLE(hdc, selectableContent[i].first, x, currentY, color);
        currentY += line_height;
    }

    // 4. Get detail info based on the selected line.
    bool hasDetail = false;
    if (!selectableContent.empty()) {
        // Use pawnInfo_selectedLine instead of the scroll offset
        std::wstring lookupKey = selectableContent[pawnInfo_selectedLine].second;
        if (currentPawnInfoTab == PawnInfoTab::OVERVIEW) {
            if (g_Backstories.count(lookupKey)) {
                std::wstring firstName = pawn.name.substr(0, pawn.name.find(L' '));
                detailTitle = g_Backstories[lookupKey].name;
                detailDescription = replacePlaceholder(g_Backstories[lookupKey].description, L"{name}", firstName);
                hasDetail = true;
            }
        }
        else if (currentPawnInfoTab == PawnInfoTab::SKILLS) {
            if (g_SkillDescriptions.count(lookupKey)) {
                detailTitle = lookupKey;
                detailDescription = g_SkillDescriptions[lookupKey];
                hasDetail = true;
            }
        }
    }

    // Draw Right Panel if there is detail content to show
    if (hasDetail) {
        FillRect(hdc, &rightPanelRect, panelBrush); // Background
        RENDER_BOX_INSPECTABLE(hdc, rightPanelRect, RGB(100, 100, 120), L"Inspected Pawn Right Panel");
        int rx = rightPanelRect.left + 15;
        int ry = rightPanelRect.top + 15;
        RENDER_TEXT_INSPECTABLE(hdc, detailTitle, rx, ry, RGB(255, 255, 0)); // Title in Yellow
        ry += 30;
        RECT descRect = { rx, ry, rightPanelRect.right - 15, rightPanelRect.bottom - 15 };
        renderWrappedText(hdc, detailDescription, descRect, RGB(255, 255, 255));
    }

    DeleteObject(panelBrush);
}
void renderWorkPanel(HDC hdc, int width, int height) {
    int bottomUiYStart = height - 220;
    int x = 20, y = bottomUiYStart;
    RENDER_TEXT_INSPECTABLE(hdc, L"Work (Arrows to navigate, PgUpPgDown to change):", x, y, RGB(255, 255, 0), L"Work Panel Title and Controls");
    y += 25;

    // Headers for job types
    int currentJobHeaderX = x + 150;
    for (size_t j = 0; j < JobTypeNames.size(); ++j) {
        COLORREF headerColor = RGB(255, 255, 255);
        // Highlight header if its column is currently selected
        if (workUI_selectedPawn != -1 && (int)j == workUI_selectedJob) {
            headerColor = RGB(255, 255, 0); // Yellow for selected column header
        }
        RENDER_TEXT_INSPECTABLE(hdc, JobTypeNames[j].substr(0, 3), currentJobHeaderX, y, headerColor, L"Job Type: " + JobTypeNames[j]);
        currentJobHeaderX += 40;
    }
    y += 20;

    // Colonist rows
    for (size_t i = 0; i < colonists.size(); ++i) {
        COLORREF colonistNameColor = (int)i == workUI_selectedPawn ? RGB(255, 255, 0) : RGB(255, 255, 255);
        RENDER_TEXT_INSPECTABLE(hdc, colonists[i].name, x, y, colonistNameColor, L"Colonist Name: " + colonists[i].name);

        int currentPrioX = x + 150;
        for (size_t j = 0; j < JobTypeNames.size(); ++j) {
            std::wstring priority = std::to_wstring(colonists[i].priorities.at((JobType)j));
            COLORREF prioColor = RGB(150, 150, 150); // Default for non-selected
            if ((int)i == workUI_selectedPawn && (int)j == workUI_selectedJob) {
                prioColor = RGB(255, 255, 255); // Highlight for currently selected cell
            }
            else if ((int)i == workUI_selectedPawn) {
                prioColor = RGB(200, 200, 200); // Slightly highlight for selected row
            }

            RENDER_TEXT_INSPECTABLE(hdc, priority, currentPrioX, y, prioColor, L"Job Priority for " + colonists[i].name + L" in " + JobTypeNames[j]);
            currentPrioX += 40;
        }
        y += 20;
    }
}

// Graph UI State
int researchGraphScrollX = 0; // Current horizontal scroll position
int totalGraphWidth = 0;      // Total width of the rendered graph
int availablePanelWidth = 0;  // Usable width within the research panel
float scaleFactor = 1.0f;     // Scaling factor for the graph elements
int scaledRankSpacingX = 0;   // Scaled horizontal spacing between ranks (columns)
int scaledNodeWidth = 0;      // Scaled width of a node
int scaledNodeHeight = 0;     // Scaled height of a node
int graphStartX = 0;          // Starting X position of the entire graph after centering/scrolling
int maxRank = 0;              // The maximum horizontal rank (column) in the graph
const COLORREF nodeTextColor = RGB(255, 255, 255); // White for text inside nodes

// --- Function to render the research graph ---
void renderResearchGraph(HDC hdc, int width, int height) {
    RECT panelRect = { 50, 30, width - 50, height - 70 }; // Panel for the graph

    // Draw panel background and border
    HBRUSH bgBrush = CreateSolidBrush(RGB(0, 0, 0));
    FillRect(hdc, &panelRect, bgBrush);
    DeleteObject(bgBrush);
    RENDER_BOX_INSPECTABLE(hdc, panelRect, RGB(100, 100, 100), L"Research Graph Panel Border"); // Slightly darker border

    const COLORREF yellow = RGB(255, 255, 0); // Available
    const COLORREF white = RGB(200, 200, 200);    // Default text color, slightly dimmer
    const COLORREF gray = RGB(80, 80, 80);        // Locked node background
    const COLORREF green = RGB(0, 200, 100);   // Completed node background
    const COLORREF red = RGB(200, 50, 50);      // Used for locked line color
    const COLORREF availableColor = yellow;      // Available node background
    const COLORREF defaultNodeColor = RGB(0, 120, 180); // A pleasant blue for default/unselected available

    // --- Step 1: Calculate the horizontal rank (layer) of each node ---
    std::map<std::wstring, int> nodeRanks; // Memoization for rank calculation
    std::map<int, std::vector<std::wstring>> nodesByRank; // Grouped nodes after ranking
    std::map<std::wstring, RECT> nodePositions; // Final screen positions

    std::function<int(const std::wstring&)> calculateRank =
        [&](const std::wstring& projectID) -> int {
        if (nodeRanks.count(projectID)) return nodeRanks[projectID];
        if (!g_allResearch.count(projectID)) { // Handle cases where prereq ID might be invalid
            nodeRanks[projectID] = -1; // Assign a rank that won't affect others
            return -1;
        }
        const auto& project = g_allResearch.at(projectID);
        if (project.prerequisites.empty()) {
            nodeRanks[projectID] = 0; return 0;
        }
        int maxPrereqRank = 0;
        for (const auto& prereqID : project.prerequisites) {
            int prereqRank = calculateRank(prereqID);
            if (prereqRank == -1) continue; // Skip invalid prereqs
            maxPrereqRank = max(maxPrereqRank, prereqRank);
        }
        int rank = maxPrereqRank + 1;
        nodeRanks[projectID] = rank;
        return rank;
        };

    maxRank = 0;
    for (const auto& pair : g_allResearch) {
        int rank = calculateRank(pair.first);
        if (rank != -1) { // Only add valid ranks
            nodesByRank[rank].push_back(pair.first);
            maxRank = max(maxRank, rank);
        }
    }

    // --- Step 2: Pre-calculate screen positions for all nodes ---
    const int BASE_NODE_WIDTH = 150; // Base width for text fitting
    const int NODE_HEIGHT = 40;
    const int NODE_VERTICAL_SPACING = 15;
    const int RANK_SPACING_X_BASE = BASE_NODE_WIDTH + 60; // Base horizontal space per column

    totalGraphWidth = (maxRank + 1) * RANK_SPACING_X_BASE;
    availablePanelWidth = panelRect.right - panelRect.left - 40; // Available drawing area inside the panel

    scaleFactor = 1.0f;
    if (totalGraphWidth > availablePanelWidth) {
        scaleFactor = static_cast<float>(availablePanelWidth) / totalGraphWidth;
    }

    scaledRankSpacingX = static_cast<int>(RANK_SPACING_X_BASE * scaleFactor);
    scaledNodeWidth = static_cast<int>(BASE_NODE_WIDTH * scaleFactor);
    scaledNodeHeight = static_cast<int>(NODE_HEIGHT * scaleFactor);
    if (scaledNodeHeight < 20) scaledNodeHeight = 20;
    if (scaledRankSpacingX < BASE_NODE_WIDTH + 20) scaledRankSpacingX = BASE_NODE_WIDTH + 20;

    graphStartX = panelRect.left + 20; // Default start
    // Center the entire graph horizontally if it fits within the panel width
    if ((maxRank + 1) * scaledRankSpacingX < availablePanelWidth) {
        graphStartX += (availablePanelWidth - (maxRank + 1) * scaledRankSpacingX) / 2;
    }
    graphStartX -= researchGraphScrollX; // Apply scrolling offset

    std::map<int, int> rankCurrentY; // Track Y positions per rank

    for (int rank = 0; rank <= maxRank; ++rank) {
        if (nodesByRank.count(rank)) {
            auto& projectsInRank = nodesByRank[rank];
            // Sort the projects within the rank by name for consistent layout
            std::sort(projectsInRank.begin(), projectsInRank.end(), [&](const std::wstring& a, const std::wstring& b) {
                return g_allResearch.at(a).name < g_allResearch.at(b).name;
                });

            int columnX = graphStartX + (rank * scaledRankSpacingX);
            int columnTotalHeight = 0;
            if (!projectsInRank.empty()) {
                columnTotalHeight = projectsInRank.size() * (scaledNodeHeight + NODE_VERTICAL_SPACING) - NODE_VERTICAL_SPACING;
            }
            // Center the column vertically within the available panel height
            int columnStartY = panelRect.top + (panelRect.bottom - panelRect.top - columnTotalHeight) / 2;
            rankCurrentY[rank] = columnStartY;

            for (size_t i = 0; i < projectsInRank.size(); ++i) {
                const auto& projectID = projectsInRank[i];
                const auto& project = g_allResearch.at(projectID);
                int nodeY = rankCurrentY[rank];

                SIZE textSize;
                // Use GetTextExtentPoint32 to determine the actual text width and height
                GetTextExtentPoint32(hdc, project.name.c_str(), (int)project.name.length(), &textSize);

                // Calculate node width based on text + padding, but enforce minimum scaled width
                int requiredNodeWidth = textSize.cx + 20;
                if (requiredNodeWidth < scaledNodeWidth) requiredNodeWidth = scaledNodeWidth; // Enforce minimum scaled width
                if (requiredNodeWidth < 100) requiredNodeWidth = 100; // Absolute minimum width

                RECT nodeRect = { columnX, nodeY, columnX + requiredNodeWidth, nodeY + scaledNodeHeight };
                nodePositions[projectID] = nodeRect;

                rankCurrentY[rank] += scaledNodeHeight + NODE_VERTICAL_SPACING; // Move to the next Y position within the column
            }
        }
    }

    // --- Step 3: Draw all dependency lines (BEHIND the nodes) ---
    HPEN linePenCompleted = CreatePen(PS_SOLID, 2, green); // Pen for completed prerequisites
    HPEN linePenLocked = CreatePen(PS_SOLID, 2, red);       // Pen for locked prerequisites

    // Iterate through all nodes to draw lines from their prerequisites
    for (const auto& pair : nodePositions) {
        const auto& projectID = pair.first;
        const auto& targetRect = pair.second;
        // Ensure projectID is valid in g_allResearch
        if (!g_allResearch.count(projectID)) continue;
        const auto& projectData = g_allResearch.at(projectID);

        POINT targetPoint = { targetRect.left, targetRect.top + scaledNodeHeight / 2 }; // Center of the target node's left edge

        // Iterate through prerequisites
        for (const auto& prereqID : projectData.prerequisites) {
            // Ensure the prerequisite exists in our calculated positions
            if (nodePositions.count(prereqID)) {
                const auto& sourceRect = nodePositions.at(prereqID);
                POINT sourcePoint = { sourceRect.right, sourceRect.top + scaledNodeHeight / 2 }; // Center of the source node's right edge

                bool prereqCompleted = g_completedResearch.count(prereqID);
                HGDIOBJ oldPen = SelectObject(hdc, prereqCompleted ? linePenCompleted : linePenLocked);

                // Draw the lines: Source -> Midpoint -> Target
                // Calculate midpoint X for a smoother curve (adjusting for spacing)
                int midX = sourcePoint.x + (targetPoint.x - sourcePoint.x) / 2;

                MoveToEx(hdc, sourcePoint.x, sourcePoint.y, NULL); // Start from source
                LineTo(hdc, midX, sourcePoint.y);                // Go horizontally to midpoint
                MoveToEx(hdc, midX, sourcePoint.y, NULL);         // Move to midpoint vertically
                LineTo(hdc, midX, targetPoint.y);                // Go vertically to target's Y
                LineTo(hdc, targetPoint.x, targetPoint.y);       // Go horizontally to target
                SelectObject(hdc, oldPen);
            }
        }
    }
    DeleteObject(linePenCompleted);
    DeleteObject(linePenLocked);

    // --- Step 4: Draw all nodes and their text (ON TOP of lines) ---
    for (int rank = 0; rank <= maxRank; ++rank) {
        if (nodesByRank.count(rank)) {
            const auto& projectsInRank = nodesByRank.at(rank);

            for (const auto& projectID : projectsInRank) {
                // Ensure projectID is valid in g_allResearch and nodePositions
                if (!g_allResearch.count(projectID) || !nodePositions.count(projectID)) continue;

                const auto& project = g_allResearch.at(projectID);
                RECT nodeRect = nodePositions.at(projectID); // Use the calculated rect

                COLORREF nodeColor;
                bool isCompleted = g_completedResearch.count(project.id);
                bool canResearch = true;
                for (const auto& prereqID : project.prerequisites) {
                    if (!g_allResearch.count(prereqID) || !g_completedResearch.count(prereqID)) { // Check if prereq exists AND is completed
                        canResearch = false;
                        break;
                    }
                }

                if (isCompleted) nodeColor = green;
                else if (!canResearch) nodeColor = gray;
                else nodeColor = defaultNodeColor; // Default color for available nodes

                // Draw node background (black) and then the status border
                HBRUSH nodeBgBrush = CreateSolidBrush(RGB(0, 0, 0));
                HGDIOBJ oldNodeBrush = SelectObject(hdc, nodeBgBrush);
                Rectangle(hdc, nodeRect.left, nodeRect.top, nodeRect.right, nodeRect.bottom);
                SelectObject(hdc, oldNodeBrush);
                DeleteObject(nodeBgBrush);

                RENDER_BOX_INSPECTABLE(hdc, nodeRect, nodeColor, L"Research Node: " + project.name);

                // Calculate text position and center it within the node
                SIZE textSize;
                std::wstring displayText = project.name;
                GetTextExtentPoint32(hdc, displayText.c_str(), (int)displayText.length(), &textSize);

                // Center the text within the node rectangle
                int textX = nodeRect.left + (nodeRect.right - nodeRect.left - textSize.cx) / 2;
                int textY = nodeRect.top + (nodeRect.bottom - nodeRect.top - textSize.cy) / 2;

                // Clamp text position to ensure it stays within node bounds
                textX = max(nodeRect.left, min(textX, nodeRect.right - textSize.cx));
                textY = max(nodeRect.top, min(textY, nodeRect.bottom - textSize.cy));

                RENDER_TEXT_INSPECTABLE(hdc, displayText, textX, textY, nodeTextColor, L"Research Project: " + project.name);
            }
        }
    }

    // --- Step 5: Draw Horizontal Scrollbar ---
    int scrollbarTrackWidth = panelRect.right - panelRect.left - 40;
    int scrollbarHeight = 15;
    int scrollbarX = panelRect.left + 20;
    int scrollbarY = panelRect.bottom - scrollbarHeight - 15;

    RECT trackRect = { scrollbarX, scrollbarY, scrollbarX + scrollbarTrackWidth, scrollbarY + scrollbarHeight };
    HBRUSH trackBgBrush = CreateSolidBrush(RGB(20, 20, 20));
    FillRect(hdc, &trackRect, trackBgBrush);
    DeleteObject(trackBgBrush);
    HBRUSH trackBorderBrush = CreateSolidBrush(RGB(50, 50, 50));
    FrameRect(hdc, &trackRect, trackBorderBrush);
    DeleteObject(trackBorderBrush);

    // Calculate thumb position and size
    float visibleRatio = static_cast<float>(availablePanelWidth) / totalGraphWidth;
    int thumbWidth = max(20, static_cast<int>(scrollbarTrackWidth * visibleRatio)); // Ensure minimum thumb width
    float maxScrollRange = max(0.0f, static_cast<float>(totalGraphWidth - availablePanelWidth)); // Max possible scroll value
    float scrollPercent = (maxScrollRange > 0) ? static_cast<float>(researchGraphScrollX) / maxScrollRange : 0.0f; // Current scroll percentage
    int thumbX = scrollbarX + static_cast<int>((scrollbarTrackWidth - thumbWidth) * scrollPercent);

    // Clamp thumb position within the track
    thumbX = max(scrollbarX, thumbX);
    thumbX = min(scrollbarX + scrollbarTrackWidth - thumbWidth, thumbX);

    RECT thumbRect = { thumbX, scrollbarY, thumbX + thumbWidth, scrollbarY + scrollbarHeight };

    HBRUSH thumbBgBrush = CreateSolidBrush(RGB(100, 100, 100));
    FillRect(hdc, &thumbRect, thumbBgBrush);
    DeleteObject(thumbBgBrush);
    HBRUSH thumbBorderBrush = CreateSolidBrush(RGB(150, 150, 150));
    FrameRect(hdc, &thumbRect, thumbBorderBrush);
    DeleteObject(thumbBorderBrush);

    // --- Step 6: Render hints ---
    std::wstring hint_text = L"ESC: Close | Arrows: Navigate | Shift+Arrows: Scroll | G: Toggle Graph/List | Enter: Research";
    RENDER_CENTERED_TEXT_INSPECTABLE(hdc, hint_text, panelRect.bottom - 25, width, RGB(150, 150, 150), L"Research Graph Controls Hint");
}


void renderStuffsPanel(HDC hdc, int width, int height) {
    // 1. Define UI areas
    int panelWidth = 850;
    int panelHeight = 400;
    int panelX = 30; // Position from the left edge
    int panelY = height - panelHeight - 60; // Position above the bottom tabs
    RECT panelRect = { panelX, panelY, panelX + panelWidth, panelY + panelHeight };
    int categoryPanelWidth = 200;
    int categoryX = panelRect.left + 20;
    int tableX = categoryX + categoryPanelWidth;

    // 2. Draw background and dividing line
    HBRUSH bgBrush = CreateSolidBrush(RGB(0, 0, 0));
    FillRect(hdc, &panelRect, bgBrush);
    DeleteObject(bgBrush);
    RENDER_BOX_INSPECTABLE(hdc, panelRect, RGB(100, 100, 100), L"Stuffs Panel");

    HPEN linePen = CreatePen(PS_SOLID, 1, RGB(100, 100, 100));
    HGDIOBJ oldPen = SelectObject(hdc, linePen);
    MoveToEx(hdc, tableX - 10, panelRect.top, NULL);
    LineTo(hdc, tableX - 10, panelRect.bottom);
    SelectObject(hdc, oldPen); DeleteObject(linePen);

    // 3. Render categories on the left
    int currentY = panelRect.top + 20;
    for (size_t i = 0; i < StuffsCategoryNames.size(); ++i) {
        COLORREF color = (i == static_cast<size_t>(currentStuffsCategory)) ? RGB(255, 255, 0) : RGB(255, 255, 255);
        RENDER_TEXT_INSPECTABLE(hdc, StuffsCategoryNames[i], categoryX, currentY, color, L"Stuffs Category: " + StuffsCategoryNames[i]);
        currentY += 20;
    }

    // Helper lambda to truncate text
    auto truncateText = [&](const std::wstring& text, int maxWidth) -> std::wstring {
        if (text.empty()) return L"";
        SIZE size;
        GetTextExtentPoint32W(hdc, text.c_str(), (int)text.length(), &size);
        if (size.cx <= maxWidth) return text;
        std::wstring truncated = text;
        const std::wstring ellipsis = L"...";
        SIZE ellipsisSize;
        GetTextExtentPoint32W(hdc, ellipsis.c_str(), (int)ellipsis.length(), &ellipsisSize);
        while (!truncated.empty()) {
            truncated.pop_back();
            GetTextExtentPoint32W(hdc, truncated.c_str(), (int)truncated.length(), &size);
            if (size.cx + ellipsisSize.cx <= maxWidth) return truncated + ellipsis;
        }
        return L"";
        };

    // 4. Collect and render items based on category
    if (currentStuffsCategory == StuffsCategory::CRITTERS) {
        // ... (This block remains unchanged) ...
        std::vector<CritterType> crittersToShow;
        for (const auto& pair : g_CritterData) { crittersToShow.push_back(pair.first); }
        if (g_stuffsAlphabeticalSort) { std::sort(crittersToShow.begin(), crittersToShow.end(), [](CritterType a, CritterType b) { return g_CritterData.at(a).name < g_CritterData.at(b).name; }); }
        int tableHeaderY = panelRect.top + 20;
        int nameColX = tableX + 20, tagsColX = nameColX + 170, spawnsColX = tagsColX + 240;
        int tagsColWidth = spawnsColX - tagsColX - 10, spawnsColWidth = panelRect.right - spawnsColX - 30;
        SetTextColor(hdc, RGB(200, 200, 200));
        TextOut(hdc, nameColX, tableHeaderY, L"Name", 4); TextOut(hdc, tagsColX, tableHeaderY, L"Tags", 4); TextOut(hdc, spawnsColX, tableHeaderY, L"Spawns In", 9);
        int itemListStartY = tableHeaderY + 25, itemListEndY = panelRect.bottom - 60, lineHeight = 18;
        int maxVisibleItems = max(0, (itemListEndY - itemListStartY) / lineHeight);
        if (crittersToShow.empty()) { stuffsUI_selectedItem = 0; stuffsUI_scrollOffset = 0; }
        else {
            stuffsUI_selectedItem = min(stuffsUI_selectedItem, (int)crittersToShow.size() - 1); stuffsUI_selectedItem = max(0, stuffsUI_selectedItem);
            if (stuffsUI_selectedItem < stuffsUI_scrollOffset) stuffsUI_scrollOffset = stuffsUI_selectedItem;
            else if (stuffsUI_selectedItem >= stuffsUI_scrollOffset + maxVisibleItems) stuffsUI_scrollOffset = stuffsUI_selectedItem - maxVisibleItems + 1;
            stuffsUI_scrollOffset = max(0, stuffsUI_scrollOffset);
            if ((int)crittersToShow.size() <= maxVisibleItems) stuffsUI_scrollOffset = 0; else stuffsUI_scrollOffset = min(stuffsUI_scrollOffset, (int)crittersToShow.size() - maxVisibleItems);
        }
        int currentItemY = itemListStartY;
        for (int i = 0; i < maxVisibleItems; ++i) {
            int itemIndex = stuffsUI_scrollOffset + i; if (itemIndex >= crittersToShow.size()) break;
            const CritterType currentCritterType = crittersToShow[itemIndex]; const auto& data = g_CritterData.at(currentCritterType);
            COLORREF textColor = (itemIndex == stuffsUI_selectedItem) ? RGB(255, 255, 0) : RGB(255, 255, 255);
            std::wstring nameStr = L" " + data.name;
            RENDER_TEXT_INSPECTABLE(hdc, std::wstring(1, data.character), nameColX, currentItemY, data.color);
            RENDER_TEXT_INSPECTABLE(hdc, nameStr, nameColX + 20, currentItemY, textColor);
            std::wstringstream tags_ss;
            for (size_t t = 0; t < data.tags.size(); ++t) { if (g_CritterTagNames.count(data.tags[t])) tags_ss << g_CritterTagNames.at(data.tags[t]) << (t < data.tags.size() - 1 ? L", " : L""); }
            RENDER_TEXT_INSPECTABLE(hdc, truncateText(tags_ss.str(), tagsColWidth), tagsColX, currentItemY, textColor);
            std::wstringstream spawns_ss; bool first_spawn = true;
            for (const auto& biome_pair : g_BiomeCritters) {
                const auto& critter_list = biome_pair.second;
                if (std::find(critter_list.begin(), critter_list.end(), currentCritterType) != critter_list.end()) {
                    if (!first_spawn) spawns_ss << L", ";
                    spawns_ss << BIOME_DATA.at(biome_pair.first).name; first_spawn = false;
                }
            }
            std::wstring spawnsStr = spawns_ss.str(); if (spawnsStr.empty()) spawnsStr = L"Events";
            RENDER_TEXT_INSPECTABLE(hdc, truncateText(spawnsStr, spawnsColWidth), spawnsColX, currentItemY, textColor);
            currentItemY += lineHeight;
        }
        if (crittersToShow.size() > maxVisibleItems) {
            int scrollbarX = panelRect.right - 25, scrollbarTop = itemListStartY, scrollbarHeight = itemListEndY - itemListStartY;
            RECT trackRect = { scrollbarX, scrollbarTop, scrollbarX + 10, scrollbarTop + scrollbarHeight };
            HBRUSH trackBgBrush = CreateSolidBrush(RGB(20, 20, 20)); FillRect(hdc, &trackRect, trackBgBrush); DeleteObject(trackBgBrush);
            HBRUSH trackBorderBrush = CreateSolidBrush(RGB(50, 50, 50)); FrameRect(hdc, &trackRect, trackBorderBrush); DeleteObject(trackBorderBrush);
            float thumbRatio = (float)maxVisibleItems / crittersToShow.size(); int thumbHeight = max(10, (int)(scrollbarHeight * thumbRatio));
            float scrollRange = crittersToShow.size() - maxVisibleItems; float scrollPercent = (scrollRange > 0) ? (float)stuffsUI_scrollOffset / scrollRange : 0.0f;
            int thumbY = scrollbarTop + (int)((scrollbarHeight - thumbHeight) * scrollPercent);
            RECT thumbRect = { scrollbarX + 1, thumbY, scrollbarX + 9, thumbY + thumbHeight };
            HBRUSH thumbBgBrush = CreateSolidBrush(RGB(100, 100, 100)); FillRect(hdc, &thumbRect, thumbBgBrush); DeleteObject(thumbBgBrush);
        }
    }
    else {
        std::vector<TileType> itemsToShow;
        for (const auto& pair : TILE_DATA) {
            bool shouldAdd = false;
            const auto& tags = pair.second.tags;
            if (pair.first == TileType::EMPTY || pair.first == TileType::BLUEPRINT || std::find(tags.begin(), tags.end(), TileTag::TREE_PART) != tags.end() || std::find(tags.begin(), tags.end(), TileTag::STRUCTURE) != tags.end() || std::find(tags.begin(), tags.end(), TileTag::FURNITURE) != tags.end() || std::find(tags.begin(), tags.end(), TileTag::LIGHTS) != tags.end() || std::find(tags.begin(), tags.end(), TileTag::PRODUCTION) != tags.end() || std::find(tags.begin(), tags.end(), TileTag::STOCKPILE_ZONE) != tags.end()) continue;
            switch (currentStuffsCategory) {
            case StuffsCategory::STONES: if (std::any_of(tags.begin(), tags.end(), [](TileTag t) { return t == TileTag::SEDIMENTARY || t == TileTag::IGNEOUS_INTRUSIVE || t == TileTag::IGNEOUS_EXTRUSIVE || t == TileTag::METAMORPHIC || t == TileTag::INNER_STONE || t == TileTag::STONE; }) && std::find(tags.begin(), tags.end(), TileTag::CHUNK) == tags.end() && std::find(tags.begin(), tags.end(), TileTag::ORE) == tags.end()) shouldAdd = true; break;
            case StuffsCategory::CHUNKS: if (std::find(tags.begin(), tags.end(), TileTag::CHUNK) != tags.end()) shouldAdd = true; break;
            case StuffsCategory::WOODS: if (std::find(tags.begin(), tags.end(), TileTag::WOOD) != tags.end()) shouldAdd = true; break;
            case StuffsCategory::METALS: if (std::find(tags.begin(), tags.end(), TileTag::METAL) != tags.end()) shouldAdd = true; break;
            case StuffsCategory::ORES: if (std::find(tags.begin(), tags.end(), TileTag::ORE) != tags.end()) shouldAdd = true; break;
            case StuffsCategory::TREES: switch (pair.first) { case TileType::OAK: case TileType::ACACIA: case TileType::SPRUCE: case TileType::BIRCH: case TileType::PINE: case TileType::POPLAR: case TileType::CECROPIA: case TileType::COCOA: case TileType::CYPRESS: case TileType::MAPLE: case TileType::PALM: case TileType::TEAK: case TileType::SAGUARO: case TileType::PRICKLYPEAR: case TileType::CHOLLA: shouldAdd = true; break; default: break; } break;
            }
            if (shouldAdd) itemsToShow.push_back(pair.first);
        }

        if (g_stuffsAlphabeticalSort) { std::sort(itemsToShow.begin(), itemsToShow.end(), [](TileType a, TileType b) { return TILE_DATA.at(a).name < TILE_DATA.at(b).name; }); }

        int tableHeaderY = panelRect.top + 20;
        int itemListStartY = tableHeaderY + 25, itemListEndY = panelRect.bottom - 60, lineHeight = 18;
        int maxVisibleItems = (itemListEndY - itemListStartY) / lineHeight;
        if (itemsToShow.empty()) { stuffsUI_selectedItem = 0; stuffsUI_scrollOffset = 0; }
        else {
            stuffsUI_selectedItem = min(stuffsUI_selectedItem, (int)itemsToShow.size() - 1); stuffsUI_selectedItem = max(0, stuffsUI_selectedItem);
            if (stuffsUI_selectedItem < stuffsUI_scrollOffset) stuffsUI_scrollOffset = stuffsUI_selectedItem;
            else if (stuffsUI_selectedItem >= stuffsUI_scrollOffset + maxVisibleItems) stuffsUI_scrollOffset = stuffsUI_selectedItem - maxVisibleItems + 1;
            stuffsUI_scrollOffset = max(0, stuffsUI_scrollOffset);
            if ((int)itemsToShow.size() <= maxVisibleItems) stuffsUI_scrollOffset = 0; else stuffsUI_scrollOffset = min(stuffsUI_scrollOffset, (int)itemsToShow.size() - maxVisibleItems);
        }

        if (currentStuffsCategory == StuffsCategory::ORES) { // NEW: ORES CATEGORY RENDERING
            int nameColX = tableX + 20, yieldsColX = nameColX + 200, hardColX = yieldsColX + 150, valColX = hardColX + 100;
            SetTextColor(hdc, RGB(200, 200, 200));
            TextOut(hdc, nameColX, tableHeaderY, L"Name", 4); TextOut(hdc, yieldsColX, tableHeaderY, L"Yields", 6); TextOut(hdc, hardColX, tableHeaderY, L"Hardness", 8); TextOut(hdc, valColX, tableHeaderY, L"Value", 5);
            int currentItemY = itemListStartY;
            for (int i = 0; i < maxVisibleItems; ++i) {
                int itemIndex = stuffsUI_scrollOffset + i; if (itemIndex >= itemsToShow.size()) break;
                const auto& data = TILE_DATA.at(itemsToShow[itemIndex]);
                COLORREF textColor = (itemIndex == stuffsUI_selectedItem) ? RGB(255, 255, 0) : RGB(255, 255, 255);
                std::wstring nameStr = L" " + data.name;
                RENDER_TEXT_INSPECTABLE(hdc, std::wstring(1, data.character), nameColX, currentItemY, data.color, L"Item Character");
                RENDER_TEXT_INSPECTABLE(hdc, nameStr, nameColX + 20, currentItemY, textColor, L"Item: " + data.name);

                std::wstring yieldsName = L"-";
                if (data.drops != TileType::EMPTY && TILE_DATA.count(data.drops)) {
                    yieldsName = TILE_DATA.at(data.drops).name;
                }
                RENDER_TEXT_INSPECTABLE(hdc, yieldsName, yieldsColX, currentItemY, textColor);

                wchar_t buf[20]; swprintf_s(buf, L"%.1f", data.hardness);
                RENDER_TEXT_INSPECTABLE(hdc, buf, hardColX, currentItemY, textColor);
                swprintf_s(buf, L"%.1f", data.value);
                RENDER_TEXT_INSPECTABLE(hdc, buf, valColX, currentItemY, textColor);
                currentItemY += lineHeight;
            }
        }
        else if (currentStuffsCategory == StuffsCategory::METALS) {
            int nameColX = tableX + 20, symbolColX = nameColX + 200, hardColX = symbolColX + 80, valColX = hardColX + 100;
            SetTextColor(hdc, RGB(200, 200, 200));
            TextOut(hdc, nameColX, tableHeaderY, L"Name", 4); TextOut(hdc, symbolColX, tableHeaderY, L"Symbol", 6); TextOut(hdc, hardColX, tableHeaderY, L"Hardness", 8); TextOut(hdc, valColX, tableHeaderY, L"Value", 5);
            int currentItemY = itemListStartY;
            for (int i = 0; i < maxVisibleItems; ++i) {
                int itemIndex = stuffsUI_scrollOffset + i; if (itemIndex >= itemsToShow.size()) break;
                const auto& data = TILE_DATA.at(itemsToShow[itemIndex]);
                COLORREF textColor = (itemIndex == stuffsUI_selectedItem) ? RGB(255, 255, 0) : RGB(255, 255, 255);
                std::wstring nameStr = L" " + data.name;
                RENDER_TEXT_INSPECTABLE(hdc, std::wstring(1, data.character), nameColX, currentItemY, data.color, L"Item Character");
                RENDER_TEXT_INSPECTABLE(hdc, nameStr, nameColX + 20, currentItemY, textColor, L"Item: " + data.name);
                RENDER_TEXT_INSPECTABLE(hdc, data.symbol, symbolColX, currentItemY, textColor);
                wchar_t buf[20]; swprintf_s(buf, L"%.1f", data.hardness);
                RENDER_TEXT_INSPECTABLE(hdc, buf, hardColX, currentItemY, textColor);
                swprintf_s(buf, L"%.1f", data.value);
                RENDER_TEXT_INSPECTABLE(hdc, buf, valColX, currentItemY, textColor);
                currentItemY += lineHeight;
            }
        }
        else { // All other item categories (stones, woods, etc.)
            int nameColX = tableX + 20, hardColX = nameColX + 200, valColX = hardColX + 100;
            SetTextColor(hdc, RGB(200, 200, 200));
            TextOut(hdc, nameColX, tableHeaderY, L"Name", 4); TextOut(hdc, hardColX, tableHeaderY, L"Hardness", 8); TextOut(hdc, valColX, tableHeaderY, L"Value", 5);
            int currentItemY = itemListStartY;
            for (int i = 0; i < maxVisibleItems; ++i) {
                int itemIndex = stuffsUI_scrollOffset + i; if (itemIndex >= itemsToShow.size()) break;
                const auto& data = TILE_DATA.at(itemsToShow[itemIndex]);
                COLORREF textColor = (itemIndex == stuffsUI_selectedItem) ? RGB(255, 255, 0) : RGB(255, 255, 255);
                wchar_t charToDisplay = data.character; COLORREF colorToDisplay = data.color;
                if (currentStuffsCategory == StuffsCategory::TREES) {
                    if (data.display_trunk_type != TileType::EMPTY && TILE_DATA.count(data.display_trunk_type)) { charToDisplay = TILE_DATA.at(data.display_trunk_type).character; colorToDisplay = TILE_DATA.at(data.display_trunk_type).color; }
                    else { charToDisplay = L'0'; colorToDisplay = RGB(139, 69, 19); }
                }
                std::wstring nameStr = L" " + data.name;
                RENDER_TEXT_INSPECTABLE(hdc, std::wstring(1, charToDisplay), nameColX, currentItemY, colorToDisplay, L"Item Character");
                RENDER_TEXT_INSPECTABLE(hdc, nameStr, nameColX + 20, currentItemY, textColor, L"Item: " + data.name);
                wchar_t buf[20]; swprintf_s(buf, L"%.1f", data.hardness);
                RENDER_TEXT_INSPECTABLE(hdc, buf, hardColX, currentItemY, textColor);
                swprintf_s(buf, L"%.1f", data.value);
                RENDER_TEXT_INSPECTABLE(hdc, buf, valColX, currentItemY, textColor);
                currentItemY += lineHeight;
            }
        }
        // Generic Scrollbar for items
        if (itemsToShow.size() > maxVisibleItems) {
            int scrollbarX = panelRect.right - 25; int scrollbarTop = itemListStartY; int scrollbarHeight = itemListEndY - itemListStartY;
            RECT trackRect = { scrollbarX, scrollbarTop, scrollbarX + 10, scrollbarTop + scrollbarHeight };
            HBRUSH trackBgBrush = CreateSolidBrush(RGB(20, 20, 20)); FillRect(hdc, &trackRect, trackBgBrush); DeleteObject(trackBgBrush);
            HBRUSH trackBorderBrush = CreateSolidBrush(RGB(50, 50, 50)); FrameRect(hdc, &trackRect, trackBorderBrush); DeleteObject(trackBorderBrush);
            float thumbRatio = (float)maxVisibleItems / itemsToShow.size(); int thumbHeight = max(10, (int)(scrollbarHeight * thumbRatio));
            float scrollRange = itemsToShow.size() - maxVisibleItems; float scrollPercent = (scrollRange > 0) ? (float)stuffsUI_scrollOffset / scrollRange : 0.0f;
            int thumbY = scrollbarTop + (int)((scrollbarHeight - thumbHeight) * scrollPercent);
            RECT thumbRect = { scrollbarX + 1, thumbY, scrollbarX + 9, thumbY + thumbHeight };
            HBRUSH thumbBgBrush = CreateSolidBrush(RGB(100, 100, 100)); FillRect(hdc, &thumbRect, thumbBgBrush); DeleteObject(thumbBgBrush);
        }
    }

    std::wstring sortOrderText = std::wstring(L"Sort Order: ") + (g_stuffsAlphabeticalSort ? L"Alphabetical" : L"Default") + L" [O]";
    RENDER_TEXT_INSPECTABLE(hdc, sortOrderText, categoryX, panelRect.bottom - 45, RGB(200, 200, 200), L"Sorting Order: Alphabetical/Default (Toggle with O)");

    std::wstring hintText = L"Use Arrows to navigate. [Left/Right] to change category. [O] to toggle sort. ESC to close.";
    RENDER_TEXT_INSPECTABLE(hdc, hintText, categoryX, panelRect.bottom - 25, RGB(150, 150, 150), L"Stuffs Panel Controls Hint");
}


void renderMenuPanel(HDC hdc, int width, int height) {
    int x = width - 250, y = height - 220;
    if (isInSettingsMenu) {
        renderSettingsPanel(hdc, width, height);
        return;
    }
    std::vector<std::wstring> options = { L"Resume", L"Settings", L"Back to Main Menu", L"Exit Game" };
    RENDER_TEXT_INSPECTABLE(hdc, L"Menu:", x, y, RGB(255, 255, 0), L"Pause Menu");
    y += 25;
    for (size_t i = 0; i < options.size(); ++i) {
        COLORREF color = (i == menuUI_selectedOption) ? RGB(255, 255, 255) : RGB(150, 150, 150);
        RENDER_TEXT_INSPECTABLE(hdc, options[i], x, y, color, L"Menu Option: " + options[i]);
        y += 20;
    }
}
void renderSettingsPanel(HDC hdc, int width, int height) {
    int x = width - 250, y = height - 220;
    RENDER_TEXT_INSPECTABLE(hdc, L"Settings (Enter/Left/Right to change):", x, y, RGB(255, 255, 0), L"Settings Menu Title and Controls"); y += 25;

    std::vector<std::wstring> options;
    options.push_back(L"Toggle Fullscreen");
    options.push_back(L"Resolution: < 1280x720 >");
    options.push_back(L"FPS Limit: < " + std::to_wstring(targetFPS) + L" >");
    options.push_back(L"Cursor Speed: < " + std::to_wstring(g_cursorSpeed) + L" >");
    options.push_back(isDebugMode ? L"Debug Mode: < ON >" : L"Debug Mode: < OFF >");

    for (size_t i = 0; i < options.size(); ++i) {
        COLORREF color = (i == settingsUI_selectedOption) ? RGB(255, 255, 255) : RGB(150, 150, 150);
        RENDER_TEXT_INSPECTABLE(hdc, options[i], x, y, color, L"Setting: " + options[i]);
        y += 20;
    }
    RENDER_TEXT_INSPECTABLE(hdc, L"Press ESC to go back.", x, y, RGB(150, 150, 150), L"Control Hint");
}
void renderDebugUI(HDC hdc, int width, int height) {
    if (!isDebugMode) return;
    int bottom_y = height - 40;

    if (currentDebugState == DebugMenuState::PLACING_TILE) {
        RENDER_CENTERED_TEXT_INSPECTABLE(hdc, L"PLACING: " + g_spawnableToPlace.name + L" (Z to place, Shift+Z to Brush, Esc to cancel)", bottom_y - 20, width, RGB(255, 100, 100), L"Debug: Placing Mode");
        return;
    }

    std::wstring debugText = L"[F10] DEBUG ON: [F5] Critter List | [F6] Spawn | [F7] Hour | [F8] Weather | [F9] Bright";
    if (isBrightModeActive) debugText += L" ON";
    RENDER_TEXT_INSPECTABLE(hdc, debugText, 20, 500, RGB(255, 100, 100), L"Debug Toolbar");

    if (currentDebugState == DebugMenuState::SPAWN) {
        RECT panelRect = { 150, 100, width - 150, height - 100 };
        RENDER_BOX_INSPECTABLE(hdc, panelRect, RGB(10, 10, 20), L"Debug: Spawn Menu Panel");

        HBRUSH panelBrush = CreateSolidBrush(RGB(10, 10, 20)); FillRect(hdc, &panelRect, panelBrush);
        DeleteObject(panelBrush); RENDER_BOX_INSPECTABLE(hdc, panelRect, RGB(100, 100, 120), L"Debug: Spawn Menu Panel Border");

        int x = panelRect.left + 15, y = panelRect.top + 15;
        RENDER_TEXT_INSPECTABLE(hdc, L"Spawn Menu: Arrows to select, Z to place, S to search, Esc to close.", x, y, RGB(255, 255, 0), L"Spawn Menu Hint");
        y += 20;

        std::wstring searchText = L"Search: " + spawnMenuSearch;
        if (spawnMenuIsSearching && (GetTickCount() / 500) % 2) searchText += L"_";
        RENDER_TEXT_INSPECTABLE(hdc, searchText, x, y, spawnMenuIsSearching ? RGB(255, 255, 0) : RGB(255, 255, 255), L"Spawn Menu Search Box");
        y += 25;

        // --- FIX 1: Filter the GLOBAL list, don't rebuild it ---
        std::vector<Spawnable> filteredList;
        for (const auto& spawnable : g_spawnMenuList) { // Iterate over the correct global list
            std::wstring lowerCaseName = spawnable.name;
            std::transform(lowerCaseName.begin(), lowerCaseName.end(), lowerCaseName.begin(), ::towlower);
            std::wstring lowerCaseSearch = spawnMenuSearch;
            std::transform(lowerCaseSearch.begin(), lowerCaseSearch.end(), lowerCaseSearch.begin(), ::towlower);
            if (spawnMenuSearch.empty() || lowerCaseName.find(lowerCaseSearch) != std::wstring::npos) {
                filteredList.push_back(spawnable);
            }
        }

        if (filteredList.empty()) {
            spawnMenuSelection = 0;
            spawnUI_scrollOffset = 0;
        }
        else {
            spawnMenuSelection = min(spawnMenuSelection, (int)filteredList.size() - 1);
            spawnMenuSelection = max(0, spawnMenuSelection);
        }

        int listStartY = y;
        int listHeight = panelRect.bottom - listStartY - 10;
        int lineHeight = 18;
        int maxVisibleItems = listHeight / lineHeight;
        if (maxVisibleItems < 1) maxVisibleItems = 1;

        if (spawnMenuSelection < spawnUI_scrollOffset) spawnUI_scrollOffset = spawnMenuSelection;
        else if (spawnMenuSelection >= spawnUI_scrollOffset + maxVisibleItems) spawnUI_scrollOffset = spawnMenuSelection - maxVisibleItems + 1;
        spawnUI_scrollOffset = max(0, spawnUI_scrollOffset);
        if ((int)filteredList.size() < maxVisibleItems) spawnUI_scrollOffset = 0;
        else spawnUI_scrollOffset = min(spawnUI_scrollOffset, (int)filteredList.size() - maxVisibleItems);

        int currentItemY = listStartY;
        for (int i = 0; i < maxVisibleItems; ++i) {
            int itemIndex = spawnUI_scrollOffset + i;
            if (itemIndex >= filteredList.size()) break;

            const Spawnable& spawnable = filteredList[itemIndex];

            // --- FIX 2: Get the correct character and color for rendering ---
            wchar_t charToDisplay = L'?';
            COLORREF colorToDisplay = RGB(255, 255, 255);

            if (spawnable.type == SpawnableType::TILE) {
                const auto& data = TILE_DATA.at(spawnable.tile_type);
                charToDisplay = data.character;
                colorToDisplay = data.color;
            }
            else if (spawnable.type == SpawnableType::CRITTER) {
                const auto& data = g_CritterData.at(spawnable.critter_type);
                charToDisplay = data.character;
                colorToDisplay = data.color;
            }

            COLORREF textColor = (itemIndex == spawnMenuSelection) ? RGB(255, 255, 0) : RGB(255, 255, 255);

            // Render character first, then the name
            RENDER_TEXT_INSPECTABLE(hdc, std::wstring(1, charToDisplay), x, currentItemY, colorToDisplay, L"Spawnable Character");
            RENDER_TEXT_INSPECTABLE(hdc, spawnable.name, x + 20, currentItemY, textColor, L"Spawnable: " + spawnable.name);

            currentItemY += lineHeight;
        }

        if (filteredList.size() > maxVisibleItems) {
            int scrollbarX = panelRect.right - 25;
            int scrollbarTop = listStartY;
            int scrollbarHeight = listHeight;

            RECT trackRect = { scrollbarX, scrollbarTop, scrollbarX + 10, scrollbarTop + scrollbarHeight };
            HBRUSH trackBgBrush = CreateSolidBrush(RGB(20, 20, 20));
            FillRect(hdc, &trackRect, trackBgBrush);
            DeleteObject(trackBgBrush);
            HBRUSH trackBorderBrush = CreateSolidBrush(RGB(50, 50, 50));
            FrameRect(hdc, &trackRect, trackBorderBrush);
            DeleteObject(trackBorderBrush);

            float thumbRatio = (float)maxVisibleItems / filteredList.size();
            int thumbHeight = max(10, (int)(scrollbarHeight * thumbRatio));
            float scrollRange = filteredList.size() - maxVisibleItems;
            float scrollPercent = (scrollRange > 0) ? (float)spawnUI_scrollOffset / scrollRange : 0.0f;
            int thumbY = scrollbarTop + (int)((scrollbarHeight - thumbHeight) * scrollPercent);

            RECT thumbRect = { scrollbarX + 1, thumbY, scrollbarX + 9, thumbY + thumbHeight };
            HBRUSH thumbBgBrush = CreateSolidBrush(RGB(100, 100, 100));
            FillRect(hdc, &thumbRect, thumbBgBrush);
            DeleteObject(thumbBgBrush);
            HBRUSH thumbBorderBrush = CreateSolidBrush(RGB(150, 150, 150));
            FrameRect(hdc, &thumbRect, thumbBorderBrush);
            DeleteObject(thumbBorderBrush);
        }
        return;
    }

    switch (currentDebugState) {
    case DebugMenuState::HOUR: {
        RENDER_CENTERED_TEXT_INSPECTABLE(hdc, L"Hour Control: Use Left/Right arrows to change hour. (ESC to close)", height - 40, width, RGB(255, 100, 100), L"Debug: Hour Control Hint");
        int barWidth = 24 * 15;
        int barX = (width - barWidth) / 2;
        int barY = height - 80;
        COLORREF barColor = RGB(100, 100, 100);
        COLORREF cursorColor = RGB(255, 255, 0);

        HPEN barPen = CreatePen(PS_SOLID, 2, barColor);
        HGDIOBJ oldPen = SelectObject(hdc, barPen);
        MoveToEx(hdc, barX, barY, NULL);
        LineTo(hdc, barX + barWidth, barY);

        for (int i = 0; i <= 24; ++i) {
            int tickX = barX + (i * (barWidth / 24));
            if (i % 6 == 0) {
                MoveToEx(hdc, tickX, barY - 5, NULL);
                LineTo(hdc, tickX, barY + 5);
                std::wstring hourText = std::to_wstring(i);
                SIZE textSize;
                GetTextExtentPoint32W(hdc, hourText.c_str(), (int)hourText.length(), &textSize);
                RENDER_TEXT_INSPECTABLE(hdc, hourText, tickX - (textSize.cx / 2), barY + 10, barColor, L"Time Marker: " + hourText);
            }
            else {
                MoveToEx(hdc, tickX, barY - 2, NULL);
                LineTo(hdc, tickX, barY + 2);
            }
        }
        SelectObject(hdc, oldPen);
        DeleteObject(barPen);

        long long ticksIntoDay = gameTicks % TICKS_PER_DAY;
        float day_percent = (float)ticksIntoDay / (float)TICKS_PER_DAY;
        int cursorXPosition = barX + (int)(day_percent * barWidth);

        HPEN cursorPen = CreatePen(PS_SOLID, 2, cursorColor);
        oldPen = SelectObject(hdc, cursorPen);
        MoveToEx(hdc, cursorXPosition, barY - 8, NULL);
        LineTo(hdc, cursorXPosition, barY + 8);
        SelectObject(hdc, oldPen);
        DeleteObject(cursorPen);

        wchar_t timeBuf[20];
        wsprintf(timeBuf, L"%02d:%02d", gameHour, gameMinute);
        RENDER_CENTERED_TEXT_INSPECTABLE(hdc, timeBuf, barY - 30, width, cursorColor, L"Debug: Current Time");
        break;
    }
    case DebugMenuState::WEATHER:
        RENDER_TEXT_INSPECTABLE(hdc, L"Weather Switched. Press F8 again to cycle.", 20, bottom_y - 20, RGB(255, 100, 100), L"Debug: Weather Control Hint");
        break;
    default: break;
    }
}


void renderGame(HDC hdc, int width, int height) {
    const int TOP_UI_HEIGHT = 80, BOTTOM_UI_HEIGHT = 220;
    int renderOffsetX = (width - VIEWPORT_WIDTH_TILES * charWidth) / 2;
    int renderOffsetY = TOP_UI_HEIGHT + (height - TOP_UI_HEIGHT - BOTTOM_UI_HEIGHT - VIEWPORT_HEIGHT_TILES * charHeight) / 2;

    if (currentState != GameState::REGION_SELECTION) {
        std::wstringstream pawnBarSS;
        for (size_t i = 0; i < colonists.size(); ++i) {
            std::wstring firstName = colonists[i].name.substr(0, colonists[i].name.find(L' '));
            pawnBarSS << L"[" << (i + 1) << L"] ";
            if (colonists[i].isDrafted) pawnBarSS << L"!";
            pawnBarSS << firstName << L"  ";
        }
        COLORREF pawnBarColor = RGB(255, 255, 255);
        if (!colonists.empty() && std::any_of(colonists.begin(), colonists.end(), [](const Pawn& p) { return p.isDrafted; })) {
            pawnBarColor = RGB(255, 100, 100);
        }
        RENDER_CENTERED_TEXT_INSPECTABLE(hdc, pawnBarSS.str(), 20, width, pawnBarColor, L"Colonist Bar (Select with 1-9)");

        std::vector<std::wstring> speedLabels = { L"||", L">", L">>", L">>>", L">>>>" };
        for (size_t i = 0; i < speedLabels.size(); ++i) {
            RENDER_TEXT_INSPECTABLE(hdc, L"F" + std::to_wstring(i + 1), width - 260 + (i * 40), 5, RGB(150, 150, 150), L"Hotkey");
            bool isSelected = (gameSpeed == 0 && i == 0) || (gameSpeed > 0 && gameSpeed == i);
            RENDER_TEXT_INSPECTABLE(hdc, speedLabels[i], width - 260 + (i * 40) + 4, 20, isSelected ? RGB(255, 255, 0) : RGB(255, 255, 255), L"Game Speed Control");
        }
    }
    StratumInfo sInfo = getStratumInfoForZ(currentZ);
    std::wstring zLayerText = L"Z-Level: " + sInfo.name + L" (" + std::to_wstring(currentZ - BIOSPHERE_Z_LEVEL) + L") (PgUp/Dn)";
    RENDER_TEXT_INSPECTABLE(hdc, zLayerText, 20, 20, RGB(255, 255, 0), L"Current Z-Level view. PgUp/PgDn to change.");

    // Check if we are in the main game world (not orbital views)
    if (sInfo.type < Stratum::OUTER_SPACE_PLANET_VIEW) {
        if (currentState != GameState::REGION_SELECTION) {
            int infoX = width - 250;
            int infoY = 50;
            RENDER_TEXT_INSPECTABLE(hdc, L"Speed: x" + std::to_wstring(gameSpeed) + L" (FPS:" + std::to_wstring(fps) + L")", infoX, infoY, RGB(255, 255, 255), L"Game Speed & Frames Per Second"); infoY += 20;

            std::wstring timeOfDayStr;
            switch (currentTimeOfDay) {
            case TimeOfDay::DAWN:      timeOfDayStr = L"Dawn"; break;
            case TimeOfDay::MORNING:   timeOfDayStr = L"Morning"; break;
            case TimeOfDay::MIDDAY:    timeOfDayStr = L"Midday"; break;
            case TimeOfDay::AFTERNOON: timeOfDayStr = L"Afternoon"; break;
            case TimeOfDay::EVENING:   timeOfDayStr = L"Evening"; break;
            case TimeOfDay::DUSK:      timeOfDayStr = L"Dusk"; break;
            case TimeOfDay::NIGHT:     timeOfDayStr = L"Night"; break;
            case TimeOfDay::MIDNIGHT:  timeOfDayStr = L"Midnight"; break;
            default:                   timeOfDayStr = L"Unknown"; break;
            }
            RENDER_TEXT_INSPECTABLE(hdc, timeOfDayStr, infoX, infoY, RGB(255, 255, 255), L"Current Time of Day");
            infoY += 20;

            wchar_t timeBuf[20]; wsprintf(timeBuf, L"%02d:%02d:%02d", gameHour, gameMinute, gameSecond);
            RENDER_TEXT_INSPECTABLE(hdc, timeBuf, infoX, infoY, RGB(255, 255, 255), L"Current In-Game Time"); infoY += 20;
            RENDER_TEXT_INSPECTABLE(hdc, std::to_wstring(gameDay) + getDaySuffix(gameDay) + L" of " + MonthNames[gameMonth] + L", Year " + std::to_wstring(gameYear), infoX, infoY, RGB(255, 255, 255), L"Current In-Game Date"); infoY += 20;
            RENDER_TEXT_INSPECTABLE(hdc, L"Temp: " + std::to_wstring(temperature) + L" C", infoX, infoY, RGB(255, 255, 255), L"Current Temperature"); infoY += 20;
            if (currentWeather == Weather::CLEAR) RENDER_TEXT_INSPECTABLE(hdc, L"Clear", infoX, infoY, RGB(255, 255, 255), L"Weather: Clear");
            if (currentWeather == Weather::RAINING) RENDER_TEXT_INSPECTABLE(hdc, L"Raining", infoX, infoY, RGB(0, 191, 255), L"Weather: Raining");
            if (currentWeather == Weather::SNOWING) RENDER_TEXT_INSPECTABLE(hdc, L"Snowing", infoX, infoY, RGB(173, 216, 230), L"Weather: Snowing");
            infoY += 20;
            if (!g_currentResearchProject.empty()) { const auto& project = g_allResearch.at(g_currentResearchProject); int progress_percent = (g_researchProgress * 100) / project.cost; RENDER_TEXT_INSPECTABLE(hdc, L"Research: " + project.name + L" (" + std::to_wstring(progress_percent) + L"%)", infoX, infoY, RGB(100, 200, 255), L"Current Research Project"); }
            else { RENDER_TEXT_INSPECTABLE(hdc, L"Research: Idle", infoX, infoY, RGB(128, 128, 128), L"Current Research Project"); } infoY += 20;
            RENDER_TEXT_INSPECTABLE(hdc, BIOME_DATA.at(landingBiome).name, infoX, infoY, RGB(255, 255, 255), L"Current Map Biome");
            renderMinimap(hdc, infoX, infoY + 40);
        }

        // Render game world tiles, items, and structures
        for (int y = 0; y < VIEWPORT_HEIGHT_TILES; ++y) {
            for (int x = 0; x < VIEWPORT_WIDTH_TILES; ++x) {
                int worldX = cameraX + x;
                int worldY = cameraY + y;

                if (worldX >= 0 && worldX < WORLD_WIDTH && worldY >= 0 && worldY < WORLD_HEIGHT) {
                    int drawX = x * charWidth + renderOffsetX;
                    int drawY = y * charHeight + renderOffsetY;
                    const MapCell& cell = Z_LEVELS[currentZ][worldY][worldX];

                    // Calculate final light level for this specific tile
                    float tileFinalLightLevel = currentLightLevel;
                    // If we are underground (and not in the core), give a base dim light level
                    if (currentZ < BIOSPHERE_Z_LEVEL && sInfo.type != Stratum::OUTER_CORE && sInfo.type != Stratum::INNER_CORE) {
                        tileFinalLightLevel = 0.2f; // Dim ambient light for caves
                    }

                    // Apply local light sources for this tile
                    for (const auto& light : g_lightSources) {
                        if (light.z == currentZ) {
                            float dist = sqrt(pow(worldX - light.x, 2) + pow(worldY - light.y, 2));
                            if (dist < light.radius) {
                                tileFinalLightLevel = max(tileFinalLightLevel, 1.0f * (1.0f - (dist / light.radius)));
                            }
                        }
                    }

                    if (isBrightModeActive) {
                        tileFinalLightLevel = 1.0f;
                    }

                    wchar_t charToDraw = L' ';
                    COLORREF colorToDraw = RGB(0, 0, 0);

                    if (cell.type == TileType::EMPTY) {
                        // Keep as empty character if it's genuinely empty
                    }
                    else if (!cell.itemsOnGround.empty() && currentZ == BIOSPHERE_Z_LEVEL) {
                        // If there are items on the ground, draw the first one
                        const TileData& itemData = TILE_DATA.at(cell.itemsOnGround.front());
                        charToDraw = itemData.character;
                        colorToDraw = applyLightLevel(itemData.color, tileFinalLightLevel);
                    }
                    else {
                        // Otherwise, draw the tile type itself
                        const TileData& data = TILE_DATA.at(cell.type);
                        charToDraw = data.character;
                        colorToDraw = applyLightLevel(data.color, tileFinalLightLevel);

                        const auto& tags = data.tags;
                        bool isFluid = std::find(tags.begin(), tags.end(), TileTag::FLUID) != tags.end();
                        if (isFluid && (GetTickCount64() / 250) % 2) {
                            charToDraw = L'≈';
                            if (cell.type == TileType::MOLTEN_CORE) colorToDraw = applyLightLevel(RGB(255, 140, 0), tileFinalLightLevel);
                            else colorToDraw = applyLightLevel(RGB(0, 0, 205), tileFinalLightLevel);
                        }
                    }

                    if (charToDraw != L' ') {
                        SetTextColor(hdc, colorToDraw);
                        TextOut(hdc, drawX, drawY, std::wstring(1, charToDraw).c_str(), 1);
                        // Draw stack count if more than one item is on the tile
                        if (cell.itemsOnGround.size() > 1) {
                            std::wstring stackCountText = std::to_wstring(cell.itemsOnGround.size());
                            COLORREF stackColor = RGB(255, 255, 255); // White for high contrast
                            SetTextColor(hdc, stackColor);
                            // Draw the text slightly below the main character
                            TextOut(hdc, drawX, drawY + 5, stackCountText.c_str(), (int)stackCountText.length());
                        }
                    }
                    // Designations for chop/mine/deconstruct characters (e.g., 'C', 'M', 'D')
                    wchar_t designationChar = designations[worldY][worldX];
                    if (designationChar != L' ') {
                        COLORREF designationColor = RGB(0, 255, 255); // Bright cyan for visibility
                        if (designationChar == L'D') {
                            designationColor = RGB(255, 100, 100); // Red for deconstruction
                        }
                        SetTextColor(hdc, designationColor);
                        TextOut(hdc, drawX, drawY, &designationChar, 1);

                        // Add designation to the inspector tool for debugging
                        std::wstring info_text;
                        if (designationChar == L'C') info_text = L"Designation: Chop";
                        else if (designationChar == L'M') info_text = L"Designation: Mine";
                        else if (designationChar == L'S') info_text = L"Designation: Stockpile";
                        else if (designationChar == L'D') info_text = L"Designation: Deconstruct";
                        g_inspectorElements.push_back({ { drawX, drawY, drawX + charWidth, drawY + charHeight }, info_text });
                    }
                }
            }
        }

        // Critter Rendering
        // Removed the outer if (currentZ == BIOSPHERE_Z_LEVEL) check
        for (const auto& critter : g_critters) {
            // Check if critter is on the current Z-level before checking viewport and drawing
            if (critter.z != currentZ) continue; // <-- MODIFIED: This line now filters by currentZ

            // Check if critter is in viewport
            int c_screen_x = (critter.x - cameraX) * charWidth + renderOffsetX;
            int c_screen_y = (critter.y - cameraY) * charHeight + renderOffsetY;

            if (c_screen_x >= renderOffsetX && c_screen_x < renderOffsetX + VIEWPORT_WIDTH_TILES * charWidth &&
                c_screen_y >= renderOffsetY && c_screen_y < renderOffsetY + VIEWPORT_HEIGHT_TILES * charHeight) {

                const auto& data = g_CritterData.at(critter.type);

                float critterLight = currentLightLevel; // Start with global ambient
                // Apply local light sources to the critter
                for (const auto& light : g_lightSources) {
                    if (light.z == critter.z) {
                        float dist = sqrt(pow(critter.x - light.x, 2) + pow(critter.y - light.y, 2));
                        if (dist < light.radius) {
                            critterLight = max(critterLight, 1.0f * (1.0f - (dist / light.radius)));
                        }
                    }
                }
                if (isBrightModeActive) critterLight = 1.0f;

                RENDER_TEXT_INSPECTABLE(hdc, std::wstring(1, data.character), c_screen_x, c_screen_y, applyLightLevel(data.color, critterLight), L"Critter: " + data.name);
            }
        }

        // Pawn Rendering
        if (currentZ == BIOSPHERE_Z_LEVEL) { // Pawns are only visible on the biosphere layer for now
            for (const auto& p : colonists) {
                // Ensure pawn is within viewport bounds
                int p_screen_x = (p.x - cameraX) * charWidth + renderOffsetX;
                int p_screen_y = (p.y - cameraY) * charHeight + renderOffsetY;

                if (p_screen_x >= renderOffsetX && p_screen_x < renderOffsetX + VIEWPORT_WIDTH_TILES * charWidth &&
                    p_screen_y >= renderOffsetY && p_screen_y < renderOffsetY + VIEWPORT_HEIGHT_TILES * charHeight) {

                    // Calculate light level for the pawn's specific position
                    float pawnLight = currentLightLevel; // Start with global ambient light

                    // Special color for fleeing pawns
                    COLORREF pawnColor = p.isDrafted ? RGB(255, 100, 100) : RGB(50, 255, 50);
                    if (p.currentTask == L"Fleeing") {
                        // Flashing yellow color
                        pawnColor = (GetTickCount() / 250) % 2 ? RGB(255, 255, 0) : RGB(200, 200, 0);
                    }
                    // If underground, apply the base dim light
                    if (p.z < BIOSPHERE_Z_LEVEL && getStratumInfoForZ(p.z).type != Stratum::OUTER_CORE && getStratumInfoForZ(p.z).type != Stratum::INNER_CORE) {
                        pawnLight = 0.2f;
                    }
                    // Apply local light sources to the pawn
                    for (const auto& light : g_lightSources) {
                        if (light.z == p.z) { // Check if light source is on the same Z-level as the pawn
                            float dist = sqrt(pow(p.x - light.x, 2) + pow(p.y - light.y, 2));
                            if (dist < light.radius) {
                                pawnLight = max(pawnLight, 1.0f * (1.0f - (dist / light.radius)));
                            }
                        }
                    }

                    if (isBrightModeActive) {
                        pawnLight = 1.0f;
                    }

                    RENDER_TEXT_INSPECTABLE(hdc, L"@", p_screen_x, p_screen_y, applyLightLevel(pawnColor, pawnLight), L"Colonist: " + p.name);
                }
            }
        }

        // Player Cursor Rendering (always on top of characters, before designation highlight)
        int c_screen_x = (cursorX - cameraX) * charWidth + renderOffsetX;
        int c_screen_y = (cursorY - cameraY) * charHeight + renderOffsetY;
        RENDER_TEXT_INSPECTABLE(hdc, L"X", c_screen_x, c_screen_y, RGB(255, 255, 0), L"Player Cursor");


        // Render stockpile outlines on the map (these should be above basic tiles but below cursor/rain)
        if (currentZ >= 0 && currentZ < TILE_WORLD_DEPTH) {
            HPEN hPen = CreatePen(PS_SOLID, 1, RGB(0, 200, 255)); // Cyan outline
            HGDIOBJ hOldPen = SelectObject(hdc, hPen);
            SelectObject(hdc, GetStockObject(NULL_BRUSH)); // No fill

            for (const auto& sp : g_stockpiles) {
                if (sp.z == currentZ) {
                    // Convert world coords to screen coords
                    int screenX1 = (sp.rect.left - cameraX) * charWidth + renderOffsetX;
                    int screenY1 = (sp.rect.top - cameraY) * charHeight + renderOffsetY;
                    int screenX2 = (sp.rect.right - cameraX) * charWidth + renderOffsetX + charWidth; // +charWidth for the right edge of the last tile
                    int screenY2 = (sp.rect.bottom - cameraY) * charHeight + renderOffsetY + charHeight; // +charHeight for the bottom edge of the last tile

                    // Only draw if at least partially visible
                    if (screenX2 > renderOffsetX && screenY2 > renderOffsetY &&
                        screenX1 < renderOffsetX + VIEWPORT_WIDTH_TILES * charWidth &&
                        screenY1 < renderOffsetY + VIEWPORT_HEIGHT_TILES * charHeight) {

                        RECT drawRect = { screenX1, screenY1, screenX2, screenY2 };
                        RENDER_BOX_INSPECTABLE(hdc, drawRect, RGB(0, 200, 255), L"Stockpile Zone " + std::to_wstring(sp.id) + L" Outline");
                    }
                }
            }
            SelectObject(hdc, hOldPen);
            DeleteObject(hPen);
        }

        // Designation Rectangle rendering (on top of everything else on the map, including cursor)
        // This is for the *selection box* (the yellow square/rectangle) when designating an area
        if (isDrawingDesignationRect) {
            if (currentArchitectMode == ArchitectMode::DESIGNATING_BUILD) {
                if (buildableToPlace == TileType::WOOD_FLOOR) {
                    int x1 = min(designationStartX, cursorX);
                    int y1 = min(designationStartY, cursorY);
                    int x2 = max(designationStartX, cursorX);
                    int y2 = max(designationStartY, cursorY);
                    for (int py = y1; py <= y2; ++py) {
                        for (int px = x1; px <= x2; ++px) {
                            // Check if the tile is buildable using our main logic
                            bool isGenerallyBuildable = CanBuildOn(px, py, currentZ, buildableToPlace);

                            // SPECIAL CASE CHECK FOR VISUALS:
                            // We explicitly check for the water tile case here to override the red 'X'
                            if (!isGenerallyBuildable && Z_LEVELS[currentZ][py][px].type == TileType::WATER) {
                                isGenerallyBuildable = true; // Visually, it's a valid spot
                            }

                            if (!isGenerallyBuildable) {
                                // If it's still not buildable after the special check, then it's truly blocked.
                                int screen_x = (px - cameraX) * charWidth + renderOffsetX;
                                int screen_y = (py - cameraY) * charHeight + renderOffsetY;
                                RENDER_TEXT_INSPECTABLE(hdc, L"x", screen_x, screen_y, RGB(255, 0, 0), L"Floor Placement Preview (Blocked)");
                                continue;
                            }

                            // Now, use the FAST pre-computed map to check if a colonist can reach an adjacent tile.
                            bool isReachableByPawn = false;
                            if (!g_isTileReachable.empty()) { // Safety check
                                // Increased search radius for pawn placement
                                for (int dy = -2; dy <= 2 && !isReachableByPawn; ++dy) {
                                    for (int dx = -2; dx <= 2 && !isReachableByPawn; ++dx) {
                                        // Skip the blueprint tile itself
                                        if (dx >= -1 && dx <= 1 && dy >= -1 && dy >= -1) continue; // Original bug: `dy >= -1` should be `dy <= 1` for the inner square
                                        // Corrected condition: skip the 3x3 square around the center if it's the placement tile itself.
                                        // For building, pawns need to be adjacent.
                                        if (dx == 0 && dy == 0) continue; // Don't check the tile itself

                                        int checkX = px + dx;
                                        int checkY = py + dy;
                                        // Bounds check for the adjacent tile
                                        if (checkX >= 0 && checkX < WORLD_WIDTH && checkY >= 0 && checkY < WORLD_HEIGHT) {
                                            if (g_isTileReachable[currentZ][checkY][checkX]) {
                                                isReachableByPawn = true;
                                            }
                                        }
                                    }
                                }
                            }
                            COLORREF previewColor = isReachableByPawn ? RGB(0, 255, 255) : RGB(255, 0, 0);
                            int screen_x = (px - cameraX) * charWidth + renderOffsetX;
                            int screen_y = (py - cameraY) * charHeight + renderOffsetY;
                            RENDER_TEXT_INSPECTABLE(hdc, L"x", screen_x, screen_y, previewColor, L"Floor Placement Preview");
                        }
                    }
                }
                else if (buildableToPlace == TileType::WALL) {
                    std::vector<POINT> line_points = BresenhamLine(designationStartX, designationStartY, cursorX, cursorY);
                    for (const auto& p : line_points) {
                        if (!CanBuildOn(p.x, p.y, currentZ, buildableToPlace)) {
                            int screen_x = (p.x - cameraX) * charWidth + renderOffsetX;
                            int screen_y = (p.y - cameraY) * charHeight + renderOffsetY;
                            RENDER_TEXT_INSPECTABLE(hdc, L"x", screen_x, screen_y, RGB(255, 0, 0), L"Wall Placement Preview (Blocked)");
                            continue;
                        }

                        bool isReachable = false;
                        if (!g_isTileReachable.empty()) { // Safety check
                            for (int dy = -1; dy <= 1 && !isReachable; ++dy) {
                                for (int dx = -1; dx <= 1 && !isReachable; ++dx) {
                                    if (dx == 0 && dy == 0) continue;
                                    int checkX = p.x + dx;
                                    int checkY = p.y + dy;
                                    if (checkX >= 0 && checkX < WORLD_WIDTH && checkY >= 0 && checkY < WORLD_HEIGHT) {
                                        if (g_isTileReachable[currentZ][checkY][checkX]) {
                                            isReachable = true;
                                        }
                                    }
                                }
                            }
                        }
                        COLORREF previewColor = isReachable ? RGB(0, 255, 255) : RGB(255, 0, 0);
                        int screen_x = (p.x - cameraX) * charWidth + renderOffsetX;
                        int screen_y = (p.y - cameraY) * charHeight + renderOffsetY;
                        RENDER_TEXT_INSPECTABLE(hdc, L"x", screen_x, screen_y, previewColor, L"Wall Placement Preview");
                    }
                }
            }
            else if (currentArchitectMode == ArchitectMode::DESIGNATING_DECONSTRUCT) {
                std::vector<POINT> line_points = BresenhamLine(designationStartX, designationStartY, cursorX, cursorY);
                for (const auto& p : line_points) {
                    COLORREF previewColor = RGB(255, 0, 0);
                    int screen_x = (p.x - cameraX) * charWidth + renderOffsetX;
                    int screen_y = (p.y - cameraY) * charHeight + renderOffsetY;
                    RENDER_TEXT_INSPECTABLE(hdc, L"x", screen_x, screen_y, previewColor, L"Deconstruction Preview");
                }
            }
            else { // Mine/Chop/Stockpile designation rectangle
                int r_x1 = (min(designationStartX, cursorX) - cameraX) * charWidth + renderOffsetX;
                int r_y1 = (min(designationStartY, cursorY) - cameraY) * charHeight + renderOffsetY;
                int r_x2 = (max(designationStartX, cursorX) - cameraX) * charWidth + renderOffsetX + charWidth;
                int r_y2 = (max(designationStartY, cursorY) - cameraY) * charHeight + renderOffsetY + charHeight;
                RECT rect = { r_x1, r_y1, r_x2, r_y2 };
                RENDER_BOX_INSPECTABLE(hdc, rect, RGB(255, 255, 0), L"Designation Area");
            }
        }


        // NEW: Rain particles rendering (NOW HERE, ON TOP OF ALL WORLD ELEMENTS)
        // Rain is drawn if weather is Raining and the current Z-level is within the atmosphere where rain occurs.
        // This means from the biosphere surface up to the top of the troposphere.
        if (currentWeather == Weather::RAINING && currentZ >= BIOSPHERE_Z_LEVEL && currentZ <= TROPOSPHERE_TOP_Z_LEVEL) {
            // Set the color for rain particles (a brighter light blue/grey)
            // MAKE IT EXTREMELY BRIGHT AND OBVIOUS FOR DEBUGGING
            SetTextColor(hdc, RGB(255, 0, 0)); // Pure RED for extreme visibility

            // Character for rain drops
            wchar_t rainChar = L'*'; // Changed to a very visible asterisk

            // Animate the rain by creating a vertical offset based on gameTicks.
            int animationOffset = (gameTicks % charHeight);

            // Loop through each visible tile in the viewport
            for (int y = 0; y < VIEWPORT_HEIGHT_TILES; ++y) {
                for (int x = 0; x < VIEWPORT_WIDTH_TILES; ++x) {
                    // Force drawing on ALMOST EVERY TILE for maximum density
                    // if (rand() % 100 < 25) { // Original random density
                    if (true) { // Force drawing on every tile in the viewport
                        int drawX = x * charWidth + renderOffsetX;
                        int drawY = y * charHeight + renderOffsetY;

                        // Apply the animation offset to the Y coordinate.
                        int animatedDrawY = drawY + animationOffset;

                        // Draw the rain character directly.
                        TextOut(hdc, drawX, animatedDrawY, &rainChar, 1);
                    }
                }
            }
        }

    }
    else { // Render orbital views
        switch (sInfo.type) {
        case Stratum::OUTER_SPACE_PLANET_VIEW: renderPlanetView(hdc, width, height); break;
        case Stratum::OUTER_SPACE_SYSTEM_VIEW: renderSystemView(hdc, width, height); break;
        case Stratum::OUTER_SPACE_BEYOND: renderBeyondView(hdc, width, height); break;
        default: break;
        }
    }

    int bottomY = height - 40;

    // Render the stockpile inventory readout if any stockpiles exist
    if (currentState == GameState::IN_GAME && !g_stockpiles.empty()) {
        renderStockpileReadout(hdc, width, height);
    }
    renderDebugCritterList(hdc, width, height);


    if (sInfo.type < Stratum::OUTER_SPACE_PLANET_VIEW) {
        int infoY = height - 80;
        bool pawnFound = false;
        if (currentZ == BIOSPHERE_Z_LEVEL) { // Only search for pawns on the biosphere surface
            for (const auto& p : colonists) {
                if (p.x == cursorX && p.y == cursorY) {
                    RENDER_TEXT_INSPECTABLE(hdc, p.name, 20, infoY, p.isDrafted ? RGB(255, 100, 100) : RGB(50, 255, 50), L"Inspected Pawn: " + p.name);
                    pawnFound = true;
                    break;
                }
            }
        }
        bool critterFound = false;
        if (currentZ == BIOSPHERE_Z_LEVEL) {
            for (const auto& c : g_critters) {
                if (c.x == cursorX && c.y == cursorY) {
                    const auto& data = g_CritterData.at(c.type);
                    RENDER_TEXT_INSPECTABLE(hdc, data.name, 20, infoY, data.color, L"Inspected Critter: " + data.name);
                    critterFound = true;
                    break;
                }
            }
        }
        if (pawnFound || critterFound) infoY += 20;
        std::wstringstream ss;
        const MapCell& currentCell = Z_LEVELS[currentZ][cursorY][cursorX];
        ss << TILE_DATA.at(currentCell.type).name << L" (" << cursorX << L", " << cursorY << L", " << (currentZ - BIOSPHERE_Z_LEVEL) << L")";
        if (currentCell.tree) ss << L" Part of " << TILE_DATA.at(currentCell.tree->type).name;
        // Adjust inspector info to clarify underlying vs current type for caves
        if (currentCell.type == TileType::EMPTY && currentCell.underlying_type != TileType::EMPTY) {
            ss.str(L""); // Clear previous info
            ss << TILE_DATA.at(currentCell.underlying_type).name << L" (Dug out) (" << cursorX << L", " << cursorY << L", " << (currentZ - BIOSPHERE_Z_LEVEL) << L")";
        }
        RENDER_TEXT_INSPECTABLE(hdc, ss.str(), 20, infoY, RGB(200, 200, 200), L"Inspected Tile Information");
    }

    if (currentArchitectMode != ArchitectMode::NONE) {
        std::wstring modeText;
        if (currentArchitectMode == ArchitectMode::DESIGNATING_MINE) modeText = L"Designating Mine...";
        else if (currentArchitectMode == ArchitectMode::DESIGNATING_CHOP) modeText = L"Designating Chop...";
        else if (currentArchitectMode == ArchitectMode::DESIGNATING_BUILD) modeText = L"Placing " + TILE_DATA.at(buildableToPlace).name + L"...";
        else if (currentArchitectMode == ArchitectMode::DESIGNATING_STOCKPILE) modeText = L"Designating Stockpile...";
        else if (currentArchitectMode == ArchitectMode::DESIGNATING_DECONSTRUCT) modeText = L"Designating Deconstruct...";

        if (currentArchitectMode == ArchitectMode::DESIGNATING_BUILD) {
            modeText += L" Press 'Z' to place, ESC to cancel.";
        }
        else {
            modeText += isDrawingDesignationRect ? L" Press 'Z' to confirm, ESC to cancel." : L" Press 'Z' to start selection, ESC to cancel.";
        }
        RENDER_CENTERED_TEXT_INSPECTABLE(hdc, modeText, height - BOTTOM_UI_HEIGHT, width, RGB(255, 255, 0), L"Architect Mode: " + modeText);
    }
    else if (currentState == GameState::IN_GAME) {
        if (currentTab != Tab::NONE) {
            switch (currentTab) {
            case Tab::ARCHITECT: {
                int menuY = height - BOTTOM_UI_HEIGHT;
                std::vector<std::wstring> categories = { L"Orders", L"Zones", L"Structure", L"Storage", L"Lights", L"Production", L"Furniture", L"Decoration" };
                RENDER_TEXT_INSPECTABLE(hdc, L"Architect:", 20, menuY, RGB(255, 255, 0), L"Architect Menu");
                for (size_t i = 0; i < categories.size(); ++i) {
                    menuY += 18; COLORREF color = (!isSelectingArchitectGizmo && static_cast<int>(i) == (int)currentArchitectCategory) ? RGB(255, 255, 0) : RGB(255, 255, 255);
                    RENDER_TEXT_INSPECTABLE(hdc, categories[i], 25, menuY, color, L"Architect Category: " + categories[i]);
                }
                if (isSelectingArchitectGizmo) {
                    int subMenuX = 200, subMenuY = height - BOTTOM_UI_HEIGHT + 18;
                    std::vector<std::wstring> gizmos;

                    std::vector<std::pair<std::wstring, TileType>> dynamicGizmos;
                    if (currentArchitectCategory == ArchitectCategory::ORDERS) gizmos = { L"Mine", L"Chop", L"Deconstruct" };
                    else if (currentArchitectCategory == ArchitectCategory::ZONES) gizmos = { L"Stockpile" };
                    else {
                        dynamicGizmos = getAvailableGizmos(currentArchitectCategory);
                        for (const auto& dg : dynamicGizmos) {
                            gizmos.push_back(dg.first);
                        }
                    }

                    if (!gizmos.empty()) {
                        for (size_t i = 0; i < gizmos.size(); ++i) {
                            COLORREF color = (i == architectGizmoSelection) ? RGB(255, 255, 0) : RGB(255, 255, 255);
                            RENDER_TEXT_INSPECTABLE(hdc, gizmos[i], subMenuX, subMenuY + (i * 18), color, L"Architect Gizmo: " + gizmos[i]);
                        }
                    }
                    else {
                        RENDER_TEXT_INSPECTABLE(hdc, L"[No items in this category]", subMenuX, subMenuY, RGB(128, 128, 128), L"Empty Architect Category");
                    }
                }
                break;
            }
            case Tab::WORK: renderWorkPanel(hdc, width, height); break;
            case Tab::RESEARCH: renderResearchPanel(hdc, width, height); break;
            case Tab::STUFFS: renderStuffsPanel(hdc, width, height); break;
            case Tab::MENU: renderMenuPanel(hdc, width, height); break;
            default: break;
            }
        }
        std::vector<std::pair<std::wstring, wchar_t>> tabs = { {L"Architect", L'A'}, {L"Work", L'W'}, {L"Research", L'R'}, {L"Stuffs", L'S'}, {L"Menu", L'E'} }; int currentTabX = 20;
        for (size_t i = 0; i < tabs.size(); ++i) {
            std::wstringstream tabSS; tabSS << L"[" << tabs[i].second << L"] " << tabs[i].first; COLORREF color = ((int)currentTab == static_cast<int>(i) + 1) ? RGB(255, 255, 0) : RGB(255, 255, 255);
            RENDER_TEXT_INSPECTABLE(hdc, tabSS.str(), currentTabX, bottomY, color, L"Tab Button: " + tabs[i].first);
            SIZE size; GetTextExtentPoint32(hdc, tabSS.str().c_str(), static_cast<int>(tabSS.str().length()), &size); currentTabX += size.cx + 30;
        }
    }
    // Consolidated UI panel rendering based on flags:
    if (inspectedStockpileIndex != -1) { renderStockpilePanel(hdc, width, height); }
    else if (inspectedPawnIndex != -1) { renderPawnInfoPanel(hdc, width, height); }

}

// NEW: Function to render the stockpile inventory readout on the left of the screen
void renderStockpileReadout(HDC hdc, int width, int height) {
    int panelWidth = 250;
    int panelX = 20;
        // Position it below the top UI elements, like the colonist bar and Z-level text
        int panelY = 80;
    
            // Create a vector from the map to sort it by item name for display
        std::vector<std::pair<TileType, int>> sorted_items;
    for (const auto& pair : g_stockpiledResources) {
        if (pair.second > 0) { // Only show items that are actually in stock
            sorted_items.push_back(pair);
            
        }
        
    }
    
            // Sort alphabetically by name
        std::sort(sorted_items.begin(), sorted_items.end(), [](const auto& a, const auto& b) {
        return TILE_DATA.at(a.first).name < TILE_DATA.at(b.first).name;
        });
    
            // Start drawing
        int currentY = panelY;
    int lineHeight = 16;
    
            // Optional: Draw a semi-transparent background for readability
            // For simplicity, we'll just draw the text directly.
        
        RENDER_TEXT_INSPECTABLE(hdc, L"Resources", panelX, currentY, RGB(255, 255, 255));
    currentY += lineHeight + 5;
    
        if (sorted_items.empty()) {
        RENDER_TEXT_INSPECTABLE(hdc, L"(Nothing in stockpiles)", panelX + 5, currentY, RGB(128, 128, 128));
        return;
        
    }
    
        for (const auto& item_pair : sorted_items) {
        const TileData & data = TILE_DATA.at(item_pair.first);
        int count = item_pair.second;
        
                    // Format the string for alignment: [Name]..........[Count]
            wchar_t buffer[100];
        swprintf_s(buffer, 100, L"%-25.25s %d", data.name.c_str(), count);
        
            RENDER_TEXT_INSPECTABLE(hdc, buffer, panelX + 5, currentY, RGB(220, 220, 220));
        
            currentY += lineHeight;
                // Stop if we would draw off the bottom of the screen
            if (currentY > height - 40) break;
        
    }
    
}

// NEW: renderStockpilePanel
void renderStockpilePanel(HDC hdc, int width, int height) {
    if (inspectedStockpileIndex == -1 || inspectedStockpileIndex >= g_stockpiles.size()) return;

    Stockpile& sp = g_stockpiles[inspectedStockpileIndex];

    // Panel dimensions and position
    int panelWidth = 450; // Slightly wider for new layout
    int panelHeight = 550; // Taller
    int panelX = (width - panelWidth) / 2;
    int panelY = (height - panelHeight) / 2;
    RECT panelRect = { panelX, panelY, panelX + panelWidth, panelY + panelHeight };

    // Draw background and border
    HBRUSH bgBrush = CreateSolidBrush(RGB(15, 15, 30));
    FillRect(hdc, &panelRect, bgBrush);
    DeleteObject(bgBrush);
    RENDER_BOX_INSPECTABLE(hdc, panelRect, RGB(100, 100, 120), L"Stockpile Configuration Panel");

    int textX = panelRect.left + 20;
    int currentY = panelRect.top + 20;

    RENDER_TEXT_INSPECTABLE(hdc, L"Stockpile Configuration (ID: " + std::to_wstring(sp.id) + L")", textX, currentY, RGB(255, 255, 0));
    currentY += 30;

    RENDER_TEXT_INSPECTABLE(hdc, L"Accepted Resources:", textX, currentY, RGB(255, 255, 255));
    currentY += 10; // Extra spacing before list

    // Special selection: Accept All / Decline All buttons
    int lineHeight = 18; // Standard line height
    int buttonY = panelRect.bottom - 60; // Position for buttons

    COLORREF acceptAllColor = RGB(0, 255, 128);
    if (stockpilePanel_selectedLineIndex == -1) { // -1 means "Accept All" is selected
        acceptAllColor = RGB(255, 255, 0); // Highlight when selected
    }
    RENDER_TEXT_INSPECTABLE(hdc, L"[A]ccept All", textX, buttonY, acceptAllColor, L"Accept All Button");

    COLORREF declineAllColor = RGB(255, 100, 100);
    if (stockpilePanel_selectedLineIndex == -2) { // -2 means "Decline All" is selected
        declineAllColor = RGB(255, 255, 0); // Highlight when selected
    }
    RENDER_TEXT_INSPECTABLE(hdc, L"[D]ecline All", textX + 120, buttonY, declineAllColor, L"Decline All Button");

    // Dynamic list for categories and items
    std::vector<std::pair<TileTag, int>> displayItemsFlat; // {category_tag, item_index_in_group} or {category_tag, -1} for header

    for (TileTag categoryTag : stockpilePanel_displayCategoriesOrder) {
        if (g_haulableItemsGrouped.count(categoryTag) == 0 || g_haulableItemsGrouped.at(categoryTag).empty()) {
            continue; // Skip categories with no items
        }
        displayItemsFlat.push_back({ categoryTag, -1 }); // Add category header
        if (stockpilePanel_categoryExpanded[categoryTag]) {
            for (int i = 0; i < g_haulableItemsGrouped.at(categoryTag).size(); ++i) {
                displayItemsFlat.push_back({ categoryTag, i }); // Add item
            }
        }
    }

    // Adjust selectedLineIndex
    if (!displayItemsFlat.empty()) {
        stockpilePanel_selectedLineIndex = min(stockpilePanel_selectedLineIndex, (int)displayItemsFlat.size() - 1);
        stockpilePanel_selectedLineIndex = max(-2, stockpilePanel_selectedLineIndex); // Allow -1 for Accept All, -2 for Decline All
    }
    else {
        stockpilePanel_selectedLineIndex = -1; // If no items, default to Accept All
    }

    // Adjust scroll offset
    int maxVisibleLines = (panelRect.bottom - (panelRect.top + 70) - 80) / lineHeight; // Remaining space minus controls/footer
    if (maxVisibleLines < 0) maxVisibleLines = 0; // Prevent negative lines

    // Only adjust scroll if an item/category is selected (not Accept/Decline All buttons)
    if (stockpilePanel_selectedLineIndex >= 0) {
        if (stockpilePanel_selectedLineIndex < stockpilePanel_scrollOffset) {
            stockpilePanel_scrollOffset = stockpilePanel_selectedLineIndex;
        }
        else if (stockpilePanel_selectedLineIndex >= stockpilePanel_scrollOffset + maxVisibleLines) {
            stockpilePanel_scrollOffset = stockpilePanel_selectedLineIndex - maxVisibleLines + 1;
        }
    }
    // Ensure scrollOffset doesn't go below 0 or beyond available content
    stockpilePanel_scrollOffset = max(0, stockpilePanel_scrollOffset);
    if (displayItemsFlat.size() < maxVisibleLines) {
        stockpilePanel_scrollOffset = 0; // No need to scroll if all fit
    }
    else {
        stockpilePanel_scrollOffset = min(stockpilePanel_scrollOffset, (int)displayItemsFlat.size() - maxVisibleLines);
    }


    currentY = panelRect.top + 70; // Reset Y for drawing the main list

    for (int i = 0; i < maxVisibleLines; ++i) {
        int lineIndex = stockpilePanel_scrollOffset + i;
        if (lineIndex >= displayItemsFlat.size()) break;

        TileTag currentTag = displayItemsFlat[lineIndex].first;
        int itemInGroupIndex = displayItemsFlat[lineIndex].second;

        COLORREF lineColor = RGB(255, 255, 255); // Default white
        if (lineIndex == stockpilePanel_selectedLineIndex) {
            lineColor = RGB(255, 255, 0); // Highlight selected line in yellow
        }

        if (itemInGroupIndex == -1) { // Category header
            std::wstring headerText = stockpilePanel_categoryExpanded[currentTag] ? L"[-] " : L"[+] ";
            headerText += g_tagNames.count(currentTag) ? g_tagNames.at(currentTag) : L"UNKNOWN CATEGORY";
            RENDER_TEXT_INSPECTABLE(hdc, headerText, textX, currentY, lineColor, L"Stockpile Category Header: " + headerText);
        }
        else { // Item within a category
            TileType itemType = g_haulableItemsGrouped.at(currentTag)[itemInGroupIndex];
            bool isAccepted = sp.acceptedResources.count(itemType);

            COLORREF itemColor = isAccepted ? RGB(0, 200, 0) : RGB(200, 0, 0); // Green for accepted, Red for rejected
            if (lineIndex == stockpilePanel_selectedLineIndex) { // Selected color
                itemColor = isAccepted ? RGB(0, 255, 255) : RGB(255, 100, 100);
            }

            std::wstring statusChar = isAccepted ? L"X" : L" ";
            std::wstring itemText = L"[" + statusChar + L"] " + TILE_DATA.at(itemType).name;
            RENDER_TEXT_INSPECTABLE(hdc, itemText, textX + 20, currentY, itemColor, L"Stockpile Item: " + TILE_DATA.at(itemType).name + L" (Accepted: " + (isAccepted ? L"Yes" : L"No") + L")");
        }
        currentY += lineHeight;
    }

    RENDER_CENTERED_TEXT_INSPECTABLE(hdc, L"Use Arrows to navigate. [Left/Right] to toggle category. [Z]/[Space]/[Enter] to toggle item. ESC to close.", panelRect.bottom - 25, panelWidth, RGB(150, 150, 150), L"Stockpile Panel Controls Hint");
}


void renderInspectorOverlay(HDC hdc, HWND hwnd) {
    if (!isInspectorModeActive) return;

    POINT cursorPos;
    GetCursorPos(&cursorPos);
    ScreenToClient(hwnd, &cursorPos);

    InspectorInfo* hoveredInfo = nullptr;
    for (int i = g_inspectorElements.size() - 1; i >= 0; --i) {
        if (PtInRect(&g_inspectorElements[i].rect, cursorPos)) {
            hoveredInfo = &g_inspectorElements[i];
            break;
        }
    }

    if (hoveredInfo && !hoveredInfo->info.empty()) {
        // Draw highlight box
        HPEN hPenHighlight = CreatePen(PS_SOLID, 2, RGB(0, 255, 255));
        HGDIOBJ hOldPenHighlight = SelectObject(hdc, hPenHighlight);
        SelectObject(hdc, GetStockObject(NULL_BRUSH));
        Rectangle(hdc, hoveredInfo->rect.left, hoveredInfo->rect.top, hoveredInfo->rect.right, hoveredInfo->rect.bottom);
        SelectObject(hdc, hOldPenHighlight); DeleteObject(hPenHighlight);

        // --- Draw Tooltip ---
        const int tooltipMaxWidth = 450;
        RECT clientRect; GetClientRect(hwnd, &clientRect);

        // Calculate text rect
        RECT textRect = { 0, 0, tooltipMaxWidth, 0 };
        DrawText(hdc, hoveredInfo->info.c_str(), -1, &textRect, DT_CALCRECT | DT_WORDBREAK);

        // Position tooltip background
        int tx = cursorPos.x + 20;
        int ty = cursorPos.y + 20;
        RECT bgRect = { tx, ty, tx + textRect.right + 12, ty + textRect.bottom + 8 };

        // Adjust if off-screen
        if (bgRect.right > clientRect.right) {
            tx = cursorPos.x - (bgRect.right - bgRect.left) - 20;
            bgRect.left = tx;
            bgRect.right = tx + textRect.right + 12;
        }
        if (bgRect.bottom > clientRect.bottom) {
            ty = cursorPos.y - (bgRect.bottom - bgRect.top) - 20;
            bgRect.top = ty;
            bgRect.bottom = ty + textRect.bottom + 8;
        }

        // Final text position inside the background
        textRect = { bgRect.left + 6, bgRect.top + 4, bgRect.right - 6, bgRect.bottom - 4 };

        // Draw background and border
        HBRUSH bgBrush = CreateSolidBrush(RGB(15, 15, 45));
        FillRect(hdc, &bgRect, bgBrush);
        DeleteObject(bgBrush);

        HPEN borderPen = CreatePen(PS_SOLID, 1, RGB(100, 100, 200));
        HGDIOBJ hOldPenBorder = SelectObject(hdc, borderPen);
        SelectObject(hdc, GetStockObject(NULL_BRUSH));
        Rectangle(hdc, bgRect.left, bgRect.top, bgRect.right, bgRect.bottom);
        SelectObject(hdc, hOldPenBorder); DeleteObject(borderPen);

        // Draw the text
        SetTextColor(hdc, RGB(220, 220, 255));
        DrawText(hdc, hoveredInfo->info.c_str(), -1, &textRect, DT_LEFT | DT_WORDBREAK);
    }
}


// --- Game Logic ---
void updateTime() {
    gameTicks += gameSpeed;

    const int DAYS_PER_MONTH = 28;
    const int MONTHS_PER_YEAR = 12;

    long long currentTotalDays = gameTicks / TICKS_PER_DAY;
    gameYear = 1 + (currentTotalDays / (DAYS_PER_MONTH * MONTHS_PER_YEAR));
    gameMonth = (currentTotalDays / DAYS_PER_MONTH) % MONTHS_PER_YEAR;
    gameDay = 1 + (currentTotalDays % DAYS_PER_MONTH);

    long long ticksIntoDay = gameTicks % TICKS_PER_DAY;
    gameHour = (ticksIntoDay * 24) / TICKS_PER_DAY;
    gameMinute = (ticksIntoDay * 1440 / TICKS_PER_DAY) % 60;
    gameSecond = (ticksIntoDay * 86400 / TICKS_PER_DAY) % 60;

    // Update Time of Day
    if (gameHour >= 5 && gameHour < 7) currentTimeOfDay = TimeOfDay::DAWN;
    else if (gameHour >= 7 && gameHour < 12) currentTimeOfDay = TimeOfDay::MORNING;
    else if (gameHour >= 12 && gameHour < 16) currentTimeOfDay = TimeOfDay::MIDDAY;
    else if (gameHour >= 16 && gameHour < 18) currentTimeOfDay = TimeOfDay::AFTERNOON;
    else if (gameHour >= 18 && gameHour < 20) currentTimeOfDay = TimeOfDay::EVENING;
    else if (gameHour >= 20 && gameHour < 22) currentTimeOfDay = TimeOfDay::DUSK;
    else if (gameHour >= 22 || gameHour < 5) currentTimeOfDay = TimeOfDay::NIGHT;

    // Update Lighting
    float targetLightLevel = 1.0f;
    switch (currentTimeOfDay) {
    case TimeOfDay::DAWN:      targetLightLevel = 0.6f; break;
    case TimeOfDay::MORNING:   targetLightLevel = 0.9f; break;
    case TimeOfDay::MIDDAY:    targetLightLevel = 1.0f; break;
    case TimeOfDay::AFTERNOON: targetLightLevel = 0.9f; break;
    case TimeOfDay::EVENING:   targetLightLevel = 0.7f; break;
    case TimeOfDay::DUSK:      targetLightLevel = 0.4f; break;
    case TimeOfDay::NIGHT:     targetLightLevel = isFullMoon ? 0.25f : 0.15f; break;
    }
    // Smoothly interpolate to the target light level
    if (gameSpeed > 0) {
        currentLightLevel += (targetLightLevel - currentLightLevel) * 0.005f * gameSpeed;
    }

    if (gameMonth >= 0 && gameMonth <= 2) currentSeason = Season::SPRING;
    else if (gameMonth >= 3 && gameMonth <= 5) currentSeason = Season::SUMMER;
    else if (gameMonth >= 6 && gameMonth <= 8) currentSeason = Season::AUTUMN;
    else currentSeason = Season::WINTER;

    const std::vector<int> fullMoonDays = { 2, 4, 6, 8, 10, 13, 15, 17, 19, 21, 23, 25, 28 };
    isFullMoon = std::find(fullMoonDays.begin(), fullMoonDays.end(), gameDay) != fullMoonDays.end();

    weatherChangeCooldown -= gameSpeed;
    if (weatherChangeCooldown <= 0) {
        int baseTemp = 15;
        switch (currentSeason) {
        case Season::SPRING: baseTemp = 15; if (rand() % 100 < 10) currentWeather = Weather::RAINING; else currentWeather = Weather::CLEAR; break;
        case Season::SUMMER: baseTemp = 25; currentWeather = Weather::CLEAR; break;
        case Season::AUTUMN: baseTemp = 10; if (rand() % 100 < 15) currentWeather = Weather::RAINING; else currentWeather = Weather::CLEAR; break;
        case Season::WINTER: baseTemp = -5; if (rand() % 100 < 20) currentWeather = Weather::SNOWING; else currentWeather = Weather::CLEAR; break;
        }
        if (landingBiome == Biome::DESERT) baseTemp += 15;
        if (landingBiome == Biome::TUNDRA) baseTemp -= 20;
        if (landingBiome == Biome::JUNGLE) baseTemp += 10;
        temperature = baseTemp;
        weatherChangeCooldown = 20000 + (rand() % 40000);
    }
}
void updateSolarSystem() {
    for (auto& planet : solarSystem) { planet.currentAngle += planet.orbitalSpeed * gameSpeed * 0.1; if (planet.currentAngle > 2 * 3.14159) planet.currentAngle -= 2 * 3.14159; }
    homeMoon.currentAngle += homeMoon.orbitalSpeed * gameSpeed * 0.1; if (homeMoon.currentAngle > 2 * 3.14159) homeMoon.currentAngle -= 2 * 3.14159;

    // This logic now creates a seamless carousel effect
    for (auto& star : distantStars) {
        star.x += star.dx * gameSpeed * 0.05f;
        // If star goes off the left edge...
        if (star.x < 0.0f) {
            // ...wrap it to the right edge with a little extra to prevent pop-in
            star.x += 1.0f;
        }
    }
}

// NEW: Helper function to get the total number of items a pawn is carrying.
int getTotalItemCount(const Pawn& pawn) {
    int total = 0;
    for (const auto& pair : pawn.inventory) {
        total += pair.second; // Add the count of each stack
    }
    return total;
}

void updateGame() {
    if (currentState != GameState::IN_GAME) return;

    if (gameSpeed > 0) {
        updateTime();
        updateSolarSystem();
        updateFallingTrees();

        // --- CRITTER SPAWNING AND UPDATING ---
        const int MAX_CRITTERS = 20;
        static long long lastCritterSpawnCheck = 0;
        // Corrected interval: check every 30 in-game minutes
        const long long CRITTER_SPAWN_INTERVAL = TICKS_PER_DAY / 48;

        if (g_critters.size() < MAX_CRITTERS && (gameTicks - lastCritterSpawnCheck > CRITTER_SPAWN_INTERVAL)) {
            lastCritterSpawnCheck = gameTicks;

            // Spawn Land Critter (5% chance per check)
            if (rand() % 100 < 5) {
                if (g_BiomeCritters.count(landingBiome) && !g_BiomeCritters.at(landingBiome).empty()) {
                    const auto& possible_critters = g_BiomeCritters.at(landingBiome);
                    CritterType type_to_spawn = possible_critters[rand() % possible_critters.size()];

                    // Find a valid spawn location anywhere on the map, not just the edge
                    int spawn_x = -1, spawn_y = -1;
                    int attempts = 50;
                    while (attempts > 0) {
                        int try_x = rand() % WORLD_WIDTH;
                        int try_y = rand() % WORLD_HEIGHT;
                        if (isCritterWalkable(try_x, try_y, BIOSPHERE_Z_LEVEL)) {
                            spawn_x = try_x;
                            spawn_y = try_y;
                            break;
                        }
                        attempts--;
                    }

                    if (spawn_x != -1) {
                        Critter new_critter;
                        new_critter.type = type_to_spawn;
                        new_critter.x = spawn_x;
                        new_critter.y = spawn_y;
                        new_critter.z = BIOSPHERE_Z_LEVEL;
                        new_critter.wanderCooldown = g_CritterData.at(type_to_spawn).wander_speed + (rand() % 20 - 10);
                        g_critters.push_back(new_critter);
                    }
                }
            }

            // Spawn Aquatic Critter (2% chance per check, independent of land spawns)
            if (rand() % 100 < 2) {
                const auto& aquatic_critters = g_BiomeCritters.at(Biome::OCEAN);
                if (!aquatic_critters.empty()) {
                    CritterType type_to_spawn = aquatic_critters[rand() % aquatic_critters.size()];

                    int spawn_x = -1, spawn_y = -1;
                    int attempts = 50;
                    while (attempts > 0) {
                        int try_x = rand() % WORLD_WIDTH;
                        int try_y = rand() % WORLD_HEIGHT;

                        if (Z_LEVELS[BIOSPHERE_Z_LEVEL][try_y][try_x].type == TileType::WATER) {
                            spawn_x = try_x;
                            spawn_y = try_y;
                            break;
                        }
                        attempts--;
                    }

                    if (spawn_x != -1) {
                        Critter new_critter;
                        new_critter.type = type_to_spawn;
                        new_critter.x = spawn_x;
                        new_critter.y = spawn_y;
                        new_critter.z = BIOSPHERE_Z_LEVEL;
                        new_critter.wanderCooldown = g_CritterData.at(type_to_spawn).wander_speed + (rand() % 20 - 10);
                        g_critters.push_back(new_critter);
                    }
                }
            }
        }

        // --- UNDEAD INVASION SPAWNING ---
        if (gameTicks - lastUndeadSpawnCheck > UNDEAD_SPAWN_INTERVAL) {
            lastUndeadSpawnCheck = gameTicks;
            if ((rand() % 1000) < UNDEAD_SPAWN_CHANCE_PER_1000) {
                int numUndead = 1 + (rand() % 3); // Spawn 1 to 3 undead
                for (int i = 0; i < numUndead; ++i) {
                    int edge = rand() % 4; // 0: top, 1: bottom, 2: left, 3: right
                    int spawnX = 0, spawnY = 0;

                    if (edge == 0) { spawnX = rand() % WORLD_WIDTH; spawnY = 0; }
                    else if (edge == 1) { spawnX = rand() % WORLD_WIDTH; spawnY = WORLD_HEIGHT - 1; }
                    else if (edge == 2) { spawnX = 0; spawnY = rand() % WORLD_HEIGHT; }
                    else { spawnX = WORLD_WIDTH - 1; spawnY = rand() % WORLD_HEIGHT; }

                    if (isCritterWalkable(spawnX, spawnY, BIOSPHERE_Z_LEVEL)) {
                        Critter new_undead;
                        new_undead.type = (rand() % 2 == 0) ? CritterType::ZOMBIE : CritterType::SKELETON;
                        new_undead.x = spawnX;
                        new_undead.y = spawnY;
                        new_undead.z = BIOSPHERE_Z_LEVEL;
                        new_undead.wanderCooldown = g_CritterData.at(new_undead.type).wander_speed + (rand() % 50);
                        g_critters.push_back(new_undead);
                    }
                }
            }
        }

        // Update existing critters (with Zombie AI) ---
        for (auto& critter : g_critters) {
            critter.wanderCooldown -= gameSpeed;
            if (critter.wanderCooldown <= 0) {
                const auto& data = g_CritterData.at(critter.type);
                critter.wanderCooldown = data.wander_speed + (rand() % 20 - 10);

                // --- MODIFIED: Declaration of is_aquatic moved here for wider scope ---
                bool is_aquatic = std::find(data.tags.begin(), data.tags.end(), CritterTag::AQUATIC) != data.tags.end();

                bool moved = false;
                // --- NEW: ZOMBIE AI ---
                if (critter.type == CritterType::ZOMBIE) { // Check for zombie-like critter behavior
                    const int ZOMBIE_SENSE_RADIUS = 25;

                    // 1. Check if current target is still valid
                    if (critter.targetPawnIndex != -1) {
                        if (critter.targetPawnIndex >= colonists.size()) {
                            critter.targetPawnIndex = -1; // Target is gone
                        }
                    }

                    // 2. If no target, try to find one
                    if (critter.targetPawnIndex == -1) {
                        for (int i = 0; i < colonists.size(); ++i) {
                            const auto& pawn = colonists[i];
                            int distSq = (pawn.x - critter.x) * (pawn.x - critter.x) + (pawn.y - critter.y) * (pawn.y - critter.y);
                            if (distSq < ZOMBIE_SENSE_RADIUS * ZOMBIE_SENSE_RADIUS) {
                                critter.targetPawnIndex = i;
                                break;
                            }
                        }
                    }

                    // 3. Move towards target if one exists
                    if (critter.targetPawnIndex != -1) {
                        const auto& targetPawn = colonists[critter.targetPawnIndex];
                        int dx = targetPawn.x - critter.x;
                        int dy = targetPawn.y - critter.y;

                        // Simple step-wise movement
                        int moveX = (dx > 0) ? 1 : ((dx < 0) ? -1 : 0);
                        int moveY = (dy > 0) ? 1 : ((dy < 0) ? -1 : 0);

                        int newX = critter.x + moveX;
                        int newY = critter.y + moveY;

                        // Zombies only move on their current Z-level
                        if (isCritterWalkable(newX, newY, critter.z)) {
                            critter.x = newX;
                            critter.y = newY;
                            moved = true;
                        }
                    }
                }
                // --- END: ZOMBIE AI ---

                // If not moved by special AI, do normal wandering
                if (!moved) {
                    int dx = (rand() % 3) - 1;
                    int dy = (rand() % 3) - 1;

                    if (is_aquatic) {
                        std::vector<Point3D> validNextPositions;

                        // Option 1: Try a purely vertical move (up or down) at current (x,y)
                        // Only try to change Z if currently at a water tile and there's a 20% chance
                        if (Z_LEVELS[critter.z][critter.y][critter.x].type == TileType::WATER && (rand() % 100 < 20)) {
                            int dz_try = (rand() % 2 == 0) ? -1 : 1; // Try to go up (-1) or down (+1)
                            int proposedZ_vertical = critter.z + dz_try;

                            // Check if purely vertical move is valid (within bounds and target tile is water)
                            if (proposedZ_vertical >= 0 && proposedZ_vertical < TILE_WORLD_DEPTH &&
                                Z_LEVELS[proposedZ_vertical][critter.y][critter.x].type == TileType::WATER) {
                                validNextPositions.push_back({ critter.x, critter.y, proposedZ_vertical });
                            }
                        }

                        // Option 2: Try a 2D horizontal move on the current Z-level
                        int newX_horiz = critter.x + dx;
                        int newY_horiz = critter.y + dy;

                        // Check if horizontal move is valid (within bounds and target tile is water)
                        if (newX_horiz >= 0 && newX_horiz < WORLD_WIDTH &&
                            newY_horiz >= 0 && newY_horiz < WORLD_HEIGHT &&
                            Z_LEVELS[critter.z][newY_horiz][newX_horiz].type == TileType::WATER) {
                            validNextPositions.push_back({ newX_horiz, newY_horiz, critter.z });
                        }

                        // If there are valid moves, pick one randomly and move there
                        if (!validNextPositions.empty()) {
                            Point3D chosenMove = validNextPositions[rand() % validNextPositions.size()];
                            critter.x = chosenMove.x;
                            critter.y = chosenMove.y;
                            critter.z = chosenMove.z;
                            moved = true;
                        }
                    }
                    else { // Non-aquatic critter: use isCritterWalkable
                        int newX = critter.x + dx;
                        int newY = critter.y + dy;

                        if (newX >= 0 && newX < WORLD_WIDTH && newY >= 0 && newY < WORLD_HEIGHT) {
                            if (isCritterWalkable(newX, newY, critter.z)) {
                                critter.x = newX;
                                critter.y = newY;
                            }
                        }
                    }
                }
            }
        }

        // New: Track which pawns are currently researching.
        int researchers = 0;

        // Hauling Job
        static long long lastHaulJobScanTick = 0;
        const long long HAUL_SCAN_INTERVAL = 100;
        if (gameTicks - lastHaulJobScanTick >= HAUL_SCAN_INTERVAL) {
            lastHaulJobScanTick = gameTicks;

            // Tick down cooldowns for unreachable stockpiles in the cache.
            for (auto it = g_unreachableStockpileCache.begin(); it != g_unreachableStockpileCache.end(); ) {
                it->second--; // Decrement cooldown timer
                if (it->second <= 0) {
                    it = g_unreachableStockpileCache.erase(it); // Remove from cache if cooldown expires
                }
                else {
                    ++it;
                }
            }

            // Sort stockpiles by ID to prioritize earlier ones
            std::sort(g_stockpiles.begin(), g_stockpiles.end(), [](const Stockpile& a, const Stockpile& b) {
                return a.id < b.id;
                });

            for (int y = 0; y < WORLD_HEIGHT; ++y) {
                for (int x = 0; x < WORLD_WIDTH; ++x) {
                    MapCell& cell = Z_LEVELS[BIOSPHERE_Z_LEVEL][y][x];

                    if (!cell.itemsOnGround.empty()) {
                        bool itemNeedsHauling = true;
                        if (cell.stockpileId != -1) {
                            for (const auto& sp : g_stockpiles) {
                                if (sp.id == cell.stockpileId && sp.z == BIOSPHERE_Z_LEVEL) {
                                    if (sp.acceptedResources.count(cell.itemsOnGround.front())) {
                                        itemNeedsHauling = false;
                                    }
                                    break;
                                }
                            }
                        }

                        for (const auto& job : jobQueue) {
                            if (job.type == JobType::Haul && job.itemSourceX == x && job.itemSourceY == y && job.itemSourceZ == BIOSPHERE_Z_LEVEL) {
                                itemNeedsHauling = false;
                                break;
                            }
                        }

                        if (itemNeedsHauling) {
                            TileType itemToHaul = cell.itemsOnGround.front();
                            int destX = -1, destY = -1, destZ = -1;
                            bool foundReachableDestination = false;

                            Point3D sourcePoint = { x, y, BIOSPHERE_Z_LEVEL };

                            for (const auto& sp : g_stockpiles) {
                                // If this stockpile is in the unreachable cache, skip it entirely.
                                if (g_unreachableStockpileCache.count(sp.id)) {
                                    continue;
                                }

                                // Only consider stockpiles that accept the item on the correct Z-level
                                if (sp.z == BIOSPHERE_Z_LEVEL && sp.acceptedResources.count(itemToHaul)) {
                                    bool stockpileHasReachableSpot = false; // Flag to check if this SP is reachable at all

                                    // Find a potential destination within this stockpile
                                    Point3D potentialDest = { -1, -1, -1 };
                                    bool foundSpotInThisSP = false;

                                    // Pass 1: Look for existing stacks
                                    for (long sy = sp.rect.top; sy <= sp.rect.bottom && !foundSpotInThisSP; ++sy) {
                                        for (long sx = sp.rect.left; sx <= sp.rect.right && !foundSpotInThisSP; ++sx) {
                                            if (sx >= 0 && sx < WORLD_WIDTH && sy >= 0 && sy < WORLD_HEIGHT) {
                                                MapCell& destCell = Z_LEVELS[sp.z][sy][sx];
                                                if (!destCell.itemsOnGround.empty() && destCell.itemsOnGround.front() == itemToHaul && destCell.itemsOnGround.size() < MAX_STACK_SIZE) {
                                                    potentialDest = { (int)sx, (int)sy, sp.z };
                                                    foundSpotInThisSP = true;
                                                }
                                            }
                                        }
                                    }
                                    // Pass 2: Look for empty spots
                                    if (!foundSpotInThisSP) {
                                        for (long sy = sp.rect.top; sy <= sp.rect.bottom && !foundSpotInThisSP; ++sy) {
                                            for (long sx = sp.rect.left; sx <= sp.rect.right && !foundSpotInThisSP; ++sx) {
                                                if (sx >= 0 && sx < WORLD_WIDTH && sy >= 0 && sy < WORLD_HEIGHT) {
                                                    if (Z_LEVELS[sp.z][sy][sx].itemsOnGround.empty() && isWalkable((int)sx, (int)sy, sp.z)) {
                                                        potentialDest = { (int)sx, (int)sy, sp.z };
                                                        foundSpotInThisSP = true;
                                                    }
                                                }
                                            }
                                        }
                                    }

                                    // If we found any potential spot, now do the expensive path check ONCE.
                                    if (foundSpotInThisSP) {
                                        // Check if the source item can reach this stockpile.
                                        if (isReachable(sourcePoint, potentialDest)) { // isReachable is still used here for path *existence*
                                            stockpileHasReachableSpot = true;

                                            // Check if this spot is already targeted by another haul job
                                            bool isTargeted = false;
                                            for (const auto& job : jobQueue) {
                                                if (job.type == JobType::Haul && job.x == potentialDest.x && job.y == potentialDest.y && job.z == potentialDest.z) {
                                                    isTargeted = true;
                                                    break;
                                                }
                                            }

                                            if (!isTargeted) {
                                                destX = potentialDest.x;
                                                destY = potentialDest.y;
                                                destZ = potentialDest.z;
                                                foundReachableDestination = true;
                                                break; // Found a valid, reachable, untargeted spot. Stop searching.
                                            }
                                        }
                                    }

                                    // If we checked the whole stockpile and found no reachable spots, cache it as unreachable.
                                    if (!stockpileHasReachableSpot) {
                                        g_unreachableStockpileCache[sp.id] = 20; // Cooldown for 20 scan cycles (~2000 ticks)
                                    }
                                }
                            }

                            if (foundReachableDestination) {
                                jobQueue.push_back({ JobType::Haul, destX, destY, destZ, -1, itemToHaul, x, y, BIOSPHERE_Z_LEVEL });
                            }
                        }
                    }
                }
            }
        }

        for (auto& pawn : colonists) {
            if (pawn.haulCooldown > 0) pawn.haulCooldown -= gameSpeed; // NEW: Tick down the haul cooldown.
            bool isFleeing = (pawn.currentTask == L"Fleeing");
            const int PAWN_SIGHT_RADIUS = 10;
            Critter* closestThreat = nullptr;
            int closestThreatDistSq = PAWN_SIGHT_RADIUS * PAWN_SIGHT_RADIUS + 1;

            for (auto& critter : g_critters) {
                if (critter.type == CritterType::ZOMBIE || critter.type == CritterType::SKELETON) {
                    int distSq = (pawn.x - critter.x) * (pawn.x - critter.x) + (pawn.y - critter.y) * (pawn.y - critter.y);
                    if (distSq < closestThreatDistSq) {
                        closestThreatDistSq = distSq;
                        closestThreat = &critter;
                    }
                }
            }

            if (closestThreat) {
                // If a threat is found, interrupt everything and flee.
                if (!isFleeing) {
                    pawn.currentTask = L"Fleeing";
                    // Clear any current path, as fleeing takes priority
                    pawn.currentPath.clear();
                    pawn.currentPathIndex = 0;
                    pawn.ticksStuck = 0;

                    // Drop everything on the current tile
                    if (!pawn.inventory.empty()) {
                        for (const auto& item_pair : pawn.inventory) {
                            for (int i = 0; i < item_pair.second; ++i) {
                                Z_LEVELS[pawn.z][pawn.y][pawn.x].itemsOnGround.push_back(item_pair.first);
                            }
                        }
                        pawn.inventory.clear();
                    }
                }

                // Calculate a flee destination directly away from the threat
                int fleeVecX = pawn.x - closestThreat->x;
                int fleeVecY = pawn.y - closestThreat->y;

                // Set a temporary target far away in the flee direction (used for pathfinding)
                pawn.targetX = pawn.x + fleeVecX * 5;
                pawn.targetY = pawn.y + fleeVecY * 5;
                pawn.targetZ = pawn.z; // Stay on current Z-level when fleeing for simplicity

                // Clamp to world bounds
                pawn.targetX = max(0, min(WORLD_WIDTH - 1, pawn.targetX));
                pawn.targetY = max(0, min(WORLD_HEIGHT - 1, pawn.targetY));

                // Pathfind to the flee target
                pawn.currentPath = findPath({ pawn.x, pawn.y, pawn.z }, { pawn.targetX, pawn.targetY, pawn.targetZ });
                pawn.currentPathIndex = 0;

            }
            else if (isFleeing) {
                // No more threats nearby, but we were fleeing. Stop fleeing.
                pawn.currentTask = L"Idle";
                pawn.currentPath.clear();
                pawn.currentPathIndex = 0;
            }


            if (pawn.currentTask == L"Research") {
                researchers++;
            }

            if (pawn.isDrafted) {
                // Drafted pawns follow a direct target set by the player, not pathfinding through jobs.
                // Their movement is still simple step-by-step.
                if (pawn.targetX != -1 && (pawn.x != pawn.targetX || pawn.y != pawn.targetY || pawn.z != pawn.targetZ)) {
                    // Try to move closer (greedy approach for player control)
                    int moveX = 0, moveY = 0, moveZ = 0;
                    if (pawn.x < pawn.targetX) moveX = 1; else if (pawn.x > pawn.targetX) moveX = -1;
                    if (pawn.y < pawn.targetY) moveY = 1; else if (pawn.y > pawn.targetY) moveY = -1;
                    if (pawn.z < pawn.targetZ) moveZ = 1; else if (pawn.z > pawn.targetZ) moveZ = -1;

                    // Prioritize Z movement if possible through stairs, then XY.
                    if (moveZ != 0) {
                        // Check if current tile has a stair to target Z level.
                        TileType currentTile = Z_LEVELS[pawn.z][pawn.y][pawn.x].type;
                        if ((moveZ == 1 && currentTile == TileType::STAIR_UP && pawn.z < TILE_WORLD_DEPTH - 1 && Z_LEVELS[pawn.z + 1][pawn.y][pawn.x].type == TileType::STAIR_DOWN) ||
                            (moveZ == -1 && currentTile == TileType::STAIR_DOWN && pawn.z > 0 && Z_LEVELS[pawn.z - 1][pawn.y][pawn.x].type == TileType::STAIR_UP)) {
                            pawn.z += moveZ;
                        }
                        else { // Cannot move in Z, try XY
                            int newX = pawn.x + moveX;
                            int newY = pawn.y + moveY;
                            if (isWalkable(newX, newY, pawn.z)) {
                                pawn.x = newX;
                                pawn.y = newY;
                            }
                        }
                    }
                    else { // No Z-move needed/possible, just move XY
                        int newX = pawn.x + moveX;
                        int newY = pawn.y + moveY;
                        if (isWalkable(newX, newY, pawn.z)) {
                            pawn.x = newX;
                            pawn.y = newY;
                        }
                    }
                }
                else if (pawn.targetX != -1 && pawn.x == pawn.targetX && pawn.y == pawn.targetY && pawn.z == pawn.targetZ) {
                    pawn.targetX = -1; pawn.targetY = -1; pawn.targetZ = -1; // Arrived
                }
                continue; // Skip job logic for drafted pawns
            }

            if (pawn.currentTask == L"Idle") {
                if (pawn.jobSearchCooldown > 0) {
                    pawn.jobSearchCooldown -= gameSpeed;
                }

                if (pawn.jobSearchCooldown <= 0) {
                    pawn.jobSearchCooldown = 15 + (rand() % 10);

                    // --- START OF NEW, OPTIMIZED JOB SEARCH LOGIC ---

                    Job bestJob = {};
                    int bestPriority = -1;
                    int bestJobIndex = -1;
                    Point3D finalDestinationForJob = { -1, -1, -1 }; // The actual tile to path to

                    // 1. First, check the central job queue for non-chopping jobs.
                    for (size_t i = 0; i < jobQueue.size(); ++i) {
                        const Job& currentJob = jobQueue[i];
                        if (currentJob.type == JobType::Chop) continue; // Pawns find chop jobs themselves now.
                        if (currentJob.type == JobType::Haul && pawn.haulCooldown > 0) continue;

                        int currentPawnSkill = 0;
                        switch (currentJob.type) {
                        case JobType::Build: currentPawnSkill = pawn.skills[L"Construction"]; break;
                        case JobType::Research: currentPawnSkill = pawn.skills[L"Research"]; break;
                        case JobType::Mine: currentPawnSkill = pawn.skills[L"Mining"]; break;
                        case JobType::Haul: currentPawnSkill = pawn.skills[L"Hauling"]; break; // Hauling now uses skill
                        case JobType::Deconstruct: currentPawnSkill = pawn.skills[L"Construction"]; break; // Deconstruct uses Construction
                        default: currentPawnSkill = 1; // Default minimum skill for other jobs
                        }
                        if (currentPawnSkill == 0) continue; // Pawn cannot perform this job

                        Point3D jobCoreLocation;
                        if (currentJob.type == JobType::Haul) {
                            jobCoreLocation = { currentJob.itemSourceX, currentJob.itemSourceY, currentJob.itemSourceZ };
                        }
                        else {
                            jobCoreLocation = { currentJob.x, currentJob.y, currentJob.z };
                        }

                        // Determine the actual tile the pawn needs to path to (adjacent or direct)
                        Point3D tempTargetDestination;
                        bool targetRequiresAdjacent = (currentJob.type != JobType::Haul); // Mining, Build, Research, Deconstruct require adjacent

                        if (targetRequiresAdjacent) {
                            // Find an adjacent walkable tile that can be reached
                            bool foundAdjacentReachable = false;
                            for (int dz_adj = -1; dz_adj <= 1; ++dz_adj) {
                                for (int dy_adj = -1; dy_adj <= 1; ++dy_adj) {
                                    for (int dx_adj = -1; dx_adj <= 1; ++dx_adj) {
                                        if (dx_adj == 0 && dy_adj == 0 && dz_adj == 0) continue;
                                        tempTargetDestination = { jobCoreLocation.x + dx_adj, jobCoreLocation.y + dy_adj, jobCoreLocation.z + dz_adj };
                                        if (isWalkable(tempTargetDestination.x, tempTargetDestination.y, tempTargetDestination.z)) {
                                            // Only check path existence, not generate it yet
                                            if (isReachable({ pawn.x, pawn.y, pawn.z }, tempTargetDestination)) {
                                                foundAdjacentReachable = true;
                                                goto foundAdjacentForJob; // Exit nested loops
                                            }
                                        }
                                    }
                                }
                            }
                        foundAdjacentForJob:;
                            if (!foundAdjacentReachable) continue; // Skip job if no reachable adjacent tile found
                        }
                        else { // Hauling, path directly to the item
                            tempTargetDestination = jobCoreLocation;
                            if (!isReachable({ pawn.x, pawn.y, pawn.z }, tempTargetDestination)) { // Check path existence
                                continue; // Skip job if source is unreachable
                            }
                        }

                        int priority = pawn.priorities.at(currentJob.type);
                        if (priority > bestPriority) {
                            bestPriority = priority;
                            bestJob = currentJob;
                            bestJobIndex = static_cast<int>(i);
                            finalDestinationForJob = tempTargetDestination; // Store the actual point to path to
                        }
                    }

                    // 2. If no better non-chop job was found, check for nearby designated trees.
                    int chopPriority = pawn.priorities.at(JobType::Chop);
                    if (chopPriority > bestPriority) {
                        const int searchRadius = 30; // Pawns will only look for trees within this radius.
                        int bestTreeId = -1;
                        int bestTreeDistSq = searchRadius * searchRadius + 1;
                        Point3D treeJobTarget = { -1, -1, -1 }; // Target adjacent to tree root

                        for (int dy = -searchRadius; dy <= searchRadius; ++dy) {
                            for (int dx = -searchRadius; dx <= searchRadius; ++dx) {
                                int checkX = pawn.x + dx;
                                int checkY = pawn.y + dy;

                                if (checkX >= 0 && checkX < WORLD_WIDTH && checkY >= 0 && checkY < WORLD_HEIGHT) {
                                    if (designations[checkY][checkX] == L'C') { // Check for chop designation
                                        MapCell& cell = Z_LEVELS[BIOSPHERE_Z_LEVEL][checkY][checkX];
                                        if (cell.tree != nullptr) {
                                            int distSq = dx * dx + dy * dy; // Distance to designated *part*
                                            if (distSq < bestTreeDistSq) {
                                                // Find a walkable spot adjacent to the *tree root*
                                                int treeRootX = cell.tree->rootX;
                                                int treeRootY = cell.tree->rootY;
                                                bool foundSpot = false;
                                                for (int sdy = -1; sdy <= 1 && !foundSpot; ++sdy) {
                                                    for (int sdx = -1; sdx <= 1 && !foundSpot; ++sdx) {
                                                        if (sdx == 0 && sdy == 0) continue;
                                                        Point3D standSpot = { treeRootX + sdx, treeRootY + sdy, BIOSPHERE_Z_LEVEL };
                                                        if (isWalkable(standSpot.x, standSpot.y, standSpot.z)) {
                                                            // Check if pawn can path to this adjacent spot
                                                            if (isReachable({ pawn.x, pawn.y, pawn.z }, standSpot)) {
                                                                bestTreeId = cell.tree->id;
                                                                bestTreeDistSq = distSq; // Update with dist to designated part, not root
                                                                treeJobTarget = standSpot;
                                                                foundSpot = true;
                                                            }
                                                        }
                                                    }
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }

                        // If a reachable tree was found, it's the best job.
                        if (bestTreeId != -1) {
                            bestPriority = chopPriority;
                            bestJob.type = JobType::Chop;
                            bestJob.treeId = bestTreeId;
                            bestJob.x = treeJobTarget.x; // Store the adjacent spot as job target
                            bestJob.y = treeJobTarget.y;
                            bestJob.z = treeJobTarget.z;
                            finalDestinationForJob = treeJobTarget;
                            bestJobIndex = -1; // Indicate it's not from jobQueue
                        }
                    }

                    // 3. If a valid job was found, assign it and calculate the path.
                    if (bestPriority >= 0) {
                        pawn.currentPath = findPath({ pawn.x, pawn.y, pawn.z }, finalDestinationForJob);

                        if (!pawn.currentPath.empty()) { // Only take job if a path was successfully found
                            pawn.currentPathIndex = 0;
                            pawn.ticksStuck = 0; // Reset stuck counter for new path

                            if (bestJob.type == JobType::Haul) {
                                pawn.currentTask = L"Gathering Items"; // Hauling has two phases
                                pawn.haulSourceX = bestJob.itemSourceX; pawn.haulSourceY = bestJob.itemSourceY; pawn.haulSourceZ = bestJob.itemSourceZ;
                                pawn.haulDestX = bestJob.x; pawn.haulDestY = bestJob.y; pawn.haulDestZ = bestJob.z;
                            }
                            else {
                                pawn.currentTask = JobTypeNames[static_cast<int>(bestJob.type)];
                                pawn.jobTreeId = bestJob.treeId; // Only relevant for chop jobs
                            }

                            // If job was from queue, remove it. Chop jobs are marked via designation.
                            if (bestJobIndex != -1) {
                                jobQueue.erase(jobQueue.begin() + bestJobIndex);
                            }
                            else if (bestJob.type == JobType::Chop) {
                                // Mark this specific tree's root as "in progress" by changing the designation.
                                designations[a_trees[bestJob.treeId].rootY][a_trees[bestJob.treeId].rootX] = L'c';
                            }

                        }
                        else {
                            // Path not found, mark job as unassignable for a bit or just let pawn remain idle.
                            // For simplicity, pawn remains idle, can retry next search cycle.
                        }
                    }
                } // End of jobSearchCooldown check

                // If still idle after all checks, wander.
                if (pawn.currentTask == L"Idle") {
                    pawn.wanderCooldown--;
                    if (pawn.wanderCooldown <= 0) {
                        pawn.wanderCooldown = rand() % 60 + 40;
                        int dx = (rand() % 3) - 1, dy = (rand() % 3) - 1;
                        int newX = pawn.x + dx, newY = pawn.y + dy;
                        if (isWalkable(newX, newY, pawn.z)) {
                            pawn.x = newX;
                            pawn.y = newY;
                        }
                    }
                }
            } // End of pawn is Idle block
            else { // Pawn has an active task and should be following its path or performing its action
                // Check if the pawn has a path to follow
                if (!pawn.currentPath.empty() && pawn.currentPathIndex < pawn.currentPath.size()) {
                    Point3D nextStep = pawn.currentPath[pawn.currentPathIndex];

                    // Check if the next step in the path is still walkable
                    if (isWalkable(nextStep.x, nextStep.y, nextStep.z)) {
                        pawn.x = nextStep.x;
                        pawn.y = nextStep.y;
                        pawn.z = nextStep.z;
                        pawn.currentPathIndex++;
                        pawn.ticksStuck = 0; // Reset stuck counter
                    }
                    else {
                        // Path is blocked, increment stuck counter
                        pawn.ticksStuck += gameSpeed;
                        if (pawn.ticksStuck > 100) { // If stuck for too long, re-plan or go idle
                            pawn.currentTask = L"Idle"; // Abandon current task
                            pawn.currentPath.clear();
                            pawn.currentPathIndex = 0;
                            pawn.ticksStuck = 0;
                            // Designations are cleared when job is taken, so nothing to clear here usually
                        }
                    }
                }
                // If pawn arrived at the end of its path (or didn't have one, meaning it's already at the job site)
                if (pawn.currentPath.empty() || pawn.currentPathIndex >= pawn.currentPath.size()) {
                    // Reset path state (should be empty already, but for safety)
                    pawn.currentPath.clear();
                    pawn.currentPathIndex = 0;
                    pawn.ticksStuck = 0;

                    // Perform the job action based on pawn's current task
                    MapCell& cell = Z_LEVELS[pawn.z][pawn.y][pawn.x]; // The cell the pawn is currently on

                    if (pawn.currentTask == JobTypeNames[static_cast<int>(JobType::Deconstruct)]) {
                        // For deconstruct, the target is the structure itself. Pawn moves ADJACENT, not onto it.
                        // So, the check `cell.type` needs to be `pawn.targetX/Y/Z` where the structure is.
                        // However, since we path to an ADJACENT tile, `cell` is the walkable tile the pawn is on.
                        // We need to find the structure that was designated near this pawn.
                        TileType deconstructedType = TileType::EMPTY;
                        // Find designated structure at original job target (pawn.x,y,z is adjacent to it)
                        // A more robust system might store job location in pawn struct
                        // For now, let's assume if pawn is on "Deconstruct", it's near a designated structure.
                        // This simple version assumes the structure is at pawn.targetX,Y,Z from original job assignment
                        // Since job target is now where pawn paths *to*, this needs reconsideration for adjacency.
                        // For simplicity, if pawn has a deconstruct task and is AT its path destination,
                        // we can iterate neighbors to find what to deconstruct.
                        int deconstructTargetX = -1, deconstructTargetY = -1, deconstructTargetZ = -1;
                        for (int dz = -1; dz <= 1; ++dz) {
                            for (int dy = -1; dy <= 1; ++dy) {
                                for (int dx = -1; dx <= 1; ++dx) {
                                    if (dx == 0 && dy == 0 && dz == 0) continue;
                                    int chkX = pawn.x + dx;
                                    int chkY = pawn.y + dy;
                                    int chkZ = pawn.z + dz;
                                    if (chkX >= 0 && chkX < WORLD_WIDTH && chkY >= 0 && chkY < WORLD_HEIGHT && chkZ >= 0 && chkZ < TILE_WORLD_DEPTH) {
                                        if (isDeconstructable(Z_LEVELS[chkZ][chkY][chkX].type) && designations[chkY][chkX] == L'D') {
                                            deconstructTargetX = chkX; deconstructTargetY = chkY; deconstructTargetZ = chkZ;
                                            deconstructedType = Z_LEVELS[chkZ][chkY][chkX].type;
                                            goto foundDeconstructTarget;
                                        }
                                    }
                                }
                            }
                        }
                    foundDeconstructTarget:;

                        if (deconstructTargetX != -1) {
                            MapCell& targetCell = Z_LEVELS[deconstructTargetZ][deconstructTargetY][deconstructTargetX];

                            // Give back some resources (simple version)
                            if (TILE_DATA.count(deconstructedType)) {
                                const auto& tags = TILE_DATA.at(deconstructedType).tags;
                                if (std::find(tags.begin(), tags.end(), TileTag::STRUCTURE) != tags.end()) {
                                    if (deconstructedType == TileType::WOOD_FLOOR) {
                                        targetCell.itemsOnGround.push_back(TileType::OAK_WOOD);
                                    }
                                    else {
                                        targetCell.itemsOnGround.push_back(TileType::STONE_CHUNK);
                                    }
                                }
                                else if (std::find(tags.begin(), tags.end(), TileTag::FURNITURE) != tags.end()) {
                                    targetCell.itemsOnGround.push_back(TileType::OAK_WOOD);
                                }
                            }

                            // Handle special cases
                            if (deconstructedType == TileType::TORCH) {
                                g_lightSources.erase(std::remove_if(g_lightSources.begin(), g_lightSources.end(),
                                    [&](const LightSource& ls) { return ls.x == deconstructTargetX && ls.y == deconstructTargetY && ls.z == deconstructTargetZ; }),
                                    g_lightSources.end());
                            }
                            if (deconstructedType == TileType::STAIR_DOWN && deconstructTargetZ > 0) {
                                Z_LEVELS[deconstructTargetZ - 1][deconstructTargetY][deconstructTargetX].type = Z_LEVELS[deconstructTargetZ - 1][deconstructTargetY][deconstructTargetX].underlying_type;
                            }
                            if (deconstructedType == TileType::STAIR_UP && deconstructTargetZ < TILE_WORLD_DEPTH - 1) {
                                Z_LEVELS[deconstructTargetZ + 1][deconstructTargetY][deconstructTargetX].type = Z_LEVELS[deconstructTargetZ + 1][deconstructTargetY][deconstructTargetX].underlying_type;
                            }
                            if (deconstructedType == TileType::BLUEPRINT) {
                                // Cancel any build jobs for this blueprint if it was a blueprint that was deconstructed
                                jobQueue.erase(std::remove_if(jobQueue.begin(), jobQueue.end(),
                                    [&](const Job& job) { return job.type == JobType::Build && job.x == deconstructTargetX && job.y == deconstructTargetY && job.z == deconstructTargetZ; }),
                                    jobQueue.end());
                            }

                            // Reset the tile
                            targetCell.type = targetCell.underlying_type;
                            targetCell.target_type = TileType::EMPTY;
                            targetCell.construction_progress = 0;
                            designations[deconstructTargetY][deconstructTargetX] = L' '; // Clear designation
                            pawn.currentTask = L"Idle"; // Job complete
                        }
                        else {
                            // No valid target found near pawn, or it was already deconstructed/invalidated
                            pawn.currentTask = L"Idle"; // Job effectively cancelled
                        }

                    }
                    else if (pawn.currentTask == L"Research") {
                        // Pawn is at research bench location. It stays in this task until research is complete globally.
                        if (g_currentResearchProject.empty()) {
                            pawn.currentTask = L"Idle";
                        } // Else: pawn continues to 'work' on research in global research progress
                    }
                    else if (pawn.currentTask == L"Construction") {
                        // Pawn is at adjacent tile to blueprint. Find the blueprint near it.
                        TileType blueprintTargetType = TileType::EMPTY;
                        int blueprintX = -1, blueprintY = -1, blueprintZ = -1;
                        for (int dz = -1; dz <= 1; ++dz) {
                            for (int dy = -1; dy <= 1; ++dy) {
                                for (int dx = -1; dx <= 1; ++dx) {
                                    if (dx == 0 && dy == 0 && dz == 0) continue;
                                    int chkX = pawn.x + dx; int chkY = pawn.y + dy; int chkZ = pawn.z + dz;
                                    if (chkX >= 0 && chkX < WORLD_WIDTH && chkY >= 0 && chkY < WORLD_HEIGHT && chkZ >= 0 && chkZ < TILE_WORLD_DEPTH) {
                                        if (Z_LEVELS[chkZ][chkY][chkX].type == TileType::BLUEPRINT) {
                                            blueprintX = chkX; blueprintY = chkY; blueprintZ = chkZ;
                                            blueprintTargetType = Z_LEVELS[chkZ][chkY][chkX].target_type;
                                            goto foundBlueprint;
                                        }
                                    }
                                }
                            }
                        }
                    foundBlueprint:;

                        if (blueprintX != -1) {
                            MapCell& blueprintCell = Z_LEVELS[blueprintZ][blueprintY][blueprintX];
                            blueprintCell.construction_progress += gameSpeed * (1 + pawn.skills[L"Construction"] / 5);
                            if (blueprintCell.construction_progress >= BUILD_WORK_REQUIRED) {
                                TileType finalType = blueprintCell.target_type;
                                blueprintCell.type = finalType;
                                blueprintCell.target_type = TileType::EMPTY;
                                blueprintCell.construction_progress = 0;

                                if (finalType == TileType::STAIR_DOWN && blueprintZ > 0) Z_LEVELS[blueprintZ - 1][blueprintY][blueprintX].type = TileType::STAIR_UP;
                                if (finalType == TileType::STAIR_UP && blueprintZ < TILE_WORLD_DEPTH - 1) Z_LEVELS[blueprintZ + 1][blueprintY][blueprintX].type = TileType::STAIR_DOWN;
                                if (finalType == TileType::TORCH) g_lightSources.push_back({ blueprintX, blueprintY, blueprintZ, 30 });

                                pawn.currentTask = L"Idle"; // Job complete
                            }
                        }
                        else {
                            pawn.currentTask = L"Idle"; // Blueprint disappeared or invalid
                        }

                    }
                    else if (pawn.currentTask == L"Mining") {
                        // Pawn is at adjacent tile to mine designation. Find designation near it.
                        int mineTargetX = -1, mineTargetY = -1, mineTargetZ = -1;
                        for (int dz = -1; dz <= 1; ++dz) {
                            for (int dy = -1; dy <= 1; ++dy) {
                                for (int dx = -1; dx <= 1; ++dx) {
                                    if (dx == 0 && dy == 0 && dz == 0) continue;
                                    int chkX = pawn.x + dx; int chkY = pawn.y + dy; int chkZ = pawn.z + dz;
                                    if (chkX >= 0 && chkX < WORLD_WIDTH && chkY >= 0 && chkY < WORLD_HEIGHT && chkZ >= 0 && chkZ < TILE_WORLD_DEPTH) {
                                        if (designations[chkY][chkX] == L'M' && TILE_DATA.at(Z_LEVELS[chkZ][chkY][chkX].type).drops != TileType::EMPTY) {
                                            mineTargetX = chkX; mineTargetY = chkY; mineTargetZ = chkZ;
                                            goto foundMineTarget;
                                        }
                                    }
                                }
                            }
                        }
                    foundMineTarget:;

                        if (mineTargetX != -1) {
                            MapCell& targetCell = Z_LEVELS[mineTargetZ][mineTargetY][mineTargetX];
                            targetCell.itemsOnGround.push_back(TILE_DATA.at(targetCell.type).drops);
                            targetCell.type = targetCell.underlying_type; // Revert to underlying type after mining
                            designations[mineTargetY][mineTargetX] = L' '; // Clear designation
                            pawn.currentTask = L"Idle"; // Job complete
                        }
                        else {
                            pawn.currentTask = L"Idle"; // Target disappeared or invalid
                        }

                    }
                    else if (pawn.currentTask == JobTypeNames[static_cast<int>(JobType::Chop)]) {
                        // Pawn has arrived at the spot next to the tree root.
                        if (pawn.jobTreeId != -1 && a_trees.count(pawn.jobTreeId)) {
                            fellTree(pawn.jobTreeId, pawn); // This clears all tree parts and designations
                        }
                        pawn.currentTask = L"Idle"; // Job complete or tree disappeared
                        pawn.jobTreeId = -1;
                    }
                    else if (pawn.currentTask == L"Gathering Items") {
                        // Pawn arrived at the source tile (pawn.x,y,z should be pawn.haulSourceX,Y,Z)
                        MapCell& sourceCell = Z_LEVELS[pawn.haulSourceZ][pawn.haulSourceY][pawn.haulSourceX];

                        bool isSourceStockpile = (sourceCell.stockpileId != -1);
                        TileType gatheringType = TileType::EMPTY;
                        if (!pawn.inventory.empty()) {
                            gatheringType = pawn.inventory.begin()->first;
                        }
                        else if (!sourceCell.itemsOnGround.empty()) {
                            gatheringType = sourceCell.itemsOnGround.front();
                        }

                        // Pick up all matching items from the current tile.
                        if (gatheringType != TileType::EMPTY) {
                            for (int i = sourceCell.itemsOnGround.size() - 1; i >= 0; --i) {
                                if (getTotalItemCount(pawn) < PAWN_INVENTORY_CAPACITY && sourceCell.itemsOnGround[i] == gatheringType) {
                                    pawn.inventory[gatheringType]++;
                                    if (isSourceStockpile) {
                                        g_stockpiledResources[gatheringType]--;
                                    }
                                    sourceCell.itemsOnGround.erase(sourceCell.itemsOnGround.begin() + i);
                                }
                            }
                        }

                        // Now, decide the next action.
                        if (getTotalItemCount(pawn) >= PAWN_INVENTORY_CAPACITY) {
                            // We're full, so now we switch to the "Hauling" task to go to the stockpile.
                            pawn.currentTask = L"Hauling";
                            // Calculate path to haul destination
                            pawn.currentPath = findPath({ pawn.x, pawn.y, pawn.z }, { pawn.haulDestX, pawn.haulDestY, pawn.haulDestZ });
                            pawn.currentPathIndex = 0;
                        }
                        else {
                            // Not full. Look for more of the same item type nearby.
                            bool foundMore = false;
                            int bestDist = 1000;
                            Point3D nextSourceTarget = { -1,-1,-1 };
                            for (int dz = -1; dz <= 1; ++dz) { // Check adjacent Z-levels for more items
                                for (int dy = -5; dy <= 5; ++dy) {
                                    for (int dx = -5; dx <= 5; ++dx) {
                                        if (dx == 0 && dy == 0 && dz == 0) continue; // Don't check current tile
                                        int checkX = pawn.haulSourceX + dx; // Search from the original source tile (relative)
                                        int checkY = pawn.haulSourceY + dy;
                                        int checkZ = pawn.haulSourceZ + dz;
                                        if (checkX >= 0 && checkX < WORLD_WIDTH && checkY >= 0 && checkY < WORLD_HEIGHT && checkZ >= 0 && checkZ < TILE_WORLD_DEPTH) {
                                            MapCell& scanCell = Z_LEVELS[checkZ][checkY][checkX];
                                            if (!scanCell.itemsOnGround.empty() && scanCell.itemsOnGround.front() == gatheringType) {
                                                int dist = abs(dx) + abs(dy) + abs(dz);
                                                if (dist < bestDist) {
                                                    bestDist = dist;
                                                    nextSourceTarget = { checkX, checkY, checkZ };
                                                    foundMore = true;
                                                }
                                            }
                                        }
                                    }
                                }
                            }

                            if (foundMore) {
                                // Found another pile. Set it as the next target to continue gathering.
                                pawn.haulSourceX = nextSourceTarget.x;
                                pawn.haulSourceY = nextSourceTarget.y;
                                pawn.haulSourceZ = nextSourceTarget.z;
                                // Pathfind to next source tile
                                pawn.currentPath = findPath({ pawn.x, pawn.y, pawn.z }, nextSourceTarget);
                                pawn.currentPathIndex = 0;
                            }
                            else {
                                // No more found nearby. Haul what we have.
                                if (!pawn.inventory.empty()) {
                                    pawn.currentTask = L"Hauling";
                                    // Pathfind to haul destination
                                    pawn.currentPath = findPath({ pawn.x, pawn.y, pawn.z }, { pawn.haulDestX, pawn.haulDestY, pawn.haulDestZ });
                                    pawn.currentPathIndex = 0;
                                }
                                else {
                                    // We have nothing and found nothing. Job is done, go idle.
                                    pawn.currentTask = L"Idle";
                                }
                            }
                        }
                    }
                    else if (pawn.currentTask == L"Hauling") {
                        // Pawn arrived at the destination (pawn.x,y,z should be pawn.haulDestX,Y,Z)
                        MapCell& destCell = Z_LEVELS[pawn.haulDestZ][pawn.haulDestY][pawn.haulDestX]; // Corrected to use haulDest
                        int destStockpileId = destCell.stockpileId;
                        TileType itemTypeToDrop = pawn.inventory.empty() ? TileType::EMPTY : pawn.inventory.begin()->first;

                        bool isDestinationValid = false;
                        if (itemTypeToDrop != TileType::EMPTY && destStockpileId != -1) {
                            for (const auto& sp : g_stockpiles) {
                                if (sp.id == destStockpileId) {
                                    if (sp.acceptedResources.count(itemTypeToDrop)) {
                                        isDestinationValid = true;
                                    }
                                    break;
                                }
                            }
                        }

                        if (isDestinationValid) {
                            for (auto it = pawn.inventory.begin(); it != pawn.inventory.end();) {
                                TileType itemType = it->first;
                                int& count = it->second;
                                while (count > 0 && destCell.itemsOnGround.size() < MAX_STACK_SIZE) {
                                    destCell.itemsOnGround.push_back(itemType);
                                    g_stockpiledResources[itemType]++;
                                    count--;
                                }
                                if (count <= 0) it = pawn.inventory.erase(it);
                                else ++it;
                            }
                            pawn.currentTask = L"Idle";
                        }
                        else { // Destination no longer valid, find a new one or drop items
                            int newDestX = -1, newDestY = -1, newDestZ = -1;
                            bool foundNewDest = false;

                            // Re-run the destination search logic for the item the pawn is holding.
                            if (itemTypeToDrop != TileType::EMPTY) {
                                for (const auto& sp : g_stockpiles) {
                                    if (sp.z == pawn.z && sp.acceptedResources.count(itemTypeToDrop)) {
                                        for (long sy = sp.rect.top; sy <= sp.rect.bottom && !foundNewDest; ++sy) {
                                            for (long sx = sp.rect.left; sx <= sp.rect.right && !foundNewDest; ++sx) {
                                                if (sx >= 0 && sx < WORLD_WIDTH && sy >= 0 && sy < WORLD_HEIGHT) {
                                                    // Ensure the new destination cell is walkable and not already full or targeted
                                                    if (isWalkable((int)sx, (int)sy, sp.z) &&
                                                        Z_LEVELS[sp.z][sy][sx].itemsOnGround.empty() || (Z_LEVELS[sp.z][sy][sx].itemsOnGround.size() < MAX_STACK_SIZE && Z_LEVELS[sp.z][sy][sx].itemsOnGround.front() == itemTypeToDrop)) {
                                                        newDestX = (int)sx; newDestY = (int)sy; newDestZ = sp.z; foundNewDest = true;
                                                    }
                                                }
                                            }
                                        }
                                    }
                                }
                            }

                            // If a new destination was found, update the pawn's target and continue hauling.
                            if (foundNewDest) {
                                pawn.haulDestX = newDestX;
                                pawn.haulDestY = newDestY;
                                pawn.haulDestZ = newDestZ;
                                // Re-path to the new valid destination
                                pawn.currentPath = findPath({ pawn.x, pawn.y, pawn.z }, { pawn.haulDestX, pawn.haulDestY, pawn.haulDestZ });
                                pawn.currentPathIndex = 0;
                            }
                            else { // If NO valid destination exists anywhere, drop the items on the ground as a last resort.
                                for (auto it = pawn.inventory.begin(); it != pawn.inventory.end();) {
                                    int& count = it->second;
                                    while (count > 0) { // No stack limit, just dump it all
                                        destCell.itemsOnGround.push_back(it->first);
                                        // No need to adjust g_stockpiledResources here, as it was decremented on pickup.
                                        count--;
                                    }
                                    it = pawn.inventory.erase(it);
                                }
                                pawn.currentTask = L"Idle";
                            }
                        }
                    }
                    else { // Any other task ends by simply going idle if path is complete
                        // For non-hauling, non-building jobs, if we arrived and didn't find a target (e.g. mine was removed)
                        pawn.currentTask = L"Idle";
                        // Designations are cleared by the job-specific logic above (mine/chop/deconstruct)
                    }
                } // End of arrival at path end
            } // End of pawn has active task
        } // End of for each pawn

        // Global research progress update based on number of active researchers.
        if (!g_currentResearchProject.empty() && researchers > 0) {
            float research_speed_bonus = 1.0f;
            if (g_completedResearch.count(L"WRT")) research_speed_bonus += 0.10f;
            if (g_completedResearch.count(L"REN_PRP")) research_speed_bonus += 0.25f;

            int total_points_added = 0;
            for (auto& pawn : colonists) {
                if (pawn.currentTask == L"Research") {
                    // Ensure pawn is at the research bench location.
                    // This is assumed by the pathfinding, but a check might be good.
                    // For now, any pawn with "Research" task contributes.
                    total_points_added += (1 + pawn.skills[L"Research"]);
                }
            }
            g_researchProgress += static_cast<int>(total_points_added * gameSpeed * research_speed_bonus);

            const auto& project = g_allResearch.at(g_currentResearchProject);
            if (g_researchProgress >= project.cost) {
                updateUnlockedContent(project);

                g_completedResearch.insert(g_currentResearchProject);
                g_currentResearchProject = L"";
                g_researchProgress = 0;
                // Any pawns set to "Research" task will become "Idle" in the next tick
                // as their job target (the research project) is now "complete".
            }
        }
    }

    if (followedPawnIndex != -1 && followedPawnIndex < colonists.size()) {
        cursorX = colonists[followedPawnIndex].x;
        cursorY = colonists[followedPawnIndex].y;
        currentZ = colonists[followedPawnIndex].z;
        cameraX = colonists[followedPawnIndex].x - VIEWPORT_WIDTH_TILES / 2;
        cameraY = colonists[followedPawnIndex].y - VIEWPORT_HEIGHT_TILES / 2;
        cameraX = max(0, min(WORLD_WIDTH - VIEWPORT_WIDTH_TILES, cameraX));
        cameraY = max(0, min(WORLD_HEIGHT - VIEWPORT_HEIGHT_TILES, cameraY));
    }
}

void fellTree(int treeId, const Pawn& chopper) {
    if (a_trees.find(treeId) == a_trees.end()) return;
    Tree& tree = a_trees.at(treeId);
    FallenTree ftree;
    ftree.treeId = tree.id;
    ftree.baseType = tree.type;
    ftree.initialParts = tree.parts;
    ftree.fallStep = 0;

    // Determine fall direction (opposite of chopper)
    int dx = tree.rootX - chopper.x;
    int dy = tree.rootY - chopper.y;
    if (dx == 0 && dy == 0) { dx = (rand() % 3) - 1; dy = (rand() % 3) - 1; if (dx == 0 && dy == 0) dx = 1; } // Fall in random direction if chopper is on the stump
    ftree.fallDirectionX = (dx > 0) ? 1 : ((dx < 0) ? -1 : 0);
    ftree.fallDirectionY = (dy > 0) ? 1 : ((dy < 0) ? -1 : 0);

    a_fallingTrees.push_back(ftree);

    // Remove tree from world grid AND CLEAR ALL DESIGNATIONS
    for (const auto& part : tree.parts) {
        // This part clears the 2D designation grid
        if (part.x >= 0 && part.x < WORLD_WIDTH && part.y >= 0 && part.y < WORLD_HEIGHT) {
            designations[part.y][part.x] = L' ';
        }

        // This part clears the 3D map cell data
        if (part.x >= 0 && part.x < WORLD_WIDTH && part.y >= 0 && part.y < WORLD_HEIGHT && part.z >= 0 && part.z < TILE_WORLD_DEPTH) {
            MapCell& cell = Z_LEVELS[part.z][part.y][part.x];
            if (cell.tree && cell.tree->id == treeId) {
                cell.type = cell.underlying_type;
                cell.tree = nullptr;
            }
        }
    }

    // The single line that was here before is now handled by the loop above.
    // designations[tree.rootY][tree.rootX] = L' ';
    a_trees.erase(treeId);
}
void updateFallingTrees() {
    for (int i = a_fallingTrees.size() - 1; i >= 0; --i) {
        FallenTree& ftree = a_fallingTrees[i];
        ftree.fallStep++;

        bool hasLanded = false;
        for (const auto& part : ftree.initialParts) {
            int currentX = part.x + ftree.fallStep * ftree.fallDirectionX;
            int currentY = part.y + ftree.fallStep * ftree.fallDirectionY;
            int currentZ = part.z - ftree.fallStep;

            if (currentZ < BIOSPHERE_Z_LEVEL) {
                hasLanded = true; break;
            }
        }

        if (hasLanded || ftree.fallStep > 15) {
            for (const auto& part : ftree.initialParts) {
                const auto& partData = TILE_DATA.at(part.type);
                if (partData.drops != TileType::EMPTY && (std::find(partData.tags.begin(), partData.tags.end(), TileTag::TREE_TRUNK) != partData.tags.end())) {
                    int finalX = part.x + (ftree.fallStep - 1) * ftree.fallDirectionX;
                    int finalY = part.y + (ftree.fallStep - 1) * ftree.fallDirectionY;
                    if (finalX >= 0 && finalX < WORLD_WIDTH && finalY >= 0 && finalY < WORLD_HEIGHT) {
                        MapCell& b_cell = Z_LEVELS[BIOSPHERE_Z_LEVEL][finalY][finalX];
                        b_cell.itemsOnGround.push_back(partData.drops);
                    }
                }
            }
            a_fallingTrees.erase(a_fallingTrees.begin() + i);
        }
    }
}

bool isDeconstructable(TileType type) {
    if (TILE_DATA.count(type) == 0) return false;
    const auto& tags = TILE_DATA.at(type).tags;
    return (type == TileType::BLUEPRINT || // Allow deconstructing (canceling) blueprints
        std::find(tags.begin(), tags.end(), TileTag::STRUCTURE) != tags.end() ||
        std::find(tags.begin(), tags.end(), TileTag::FURNITURE) != tags.end() ||
        std::find(tags.begin(), tags.end(), TileTag::LIGHTS) != tags.end() ||
        std::find(tags.begin(), tags.end(), TileTag::PRODUCTION) != tags.end());
}

void handleInput(HWND hwnd) {
    RECT clientRect;
    GetClientRect(hwnd, &clientRect);
    int windowWidth = clientRect.right;
    int windowHeight = clientRect.bottom;

    // This block handles continuous brush placement with left-click
    if (isDebugMode && currentDebugState == DebugMenuState::PLACING_TILE && isPlacingWithBrush) {
        if (GetAsyncKeyState(VK_LBUTTON) & 0x8000) {
            if (cursorX >= 0 && cursorX < WORLD_WIDTH && cursorY >= 0 && cursorY < WORLD_HEIGHT) {
                // MODIFIED: Only place tiles with the brush
                if (g_spawnableToPlace.type == SpawnableType::TILE) {
                    Z_LEVELS[currentZ][cursorY][cursorX].type = g_spawnableToPlace.tile_type;
                }
            }
        }
    }

    // This block handles cursor movement when exploring the map or designating
    if (currentState == GameState::IN_GAME &&
        currentTab == Tab::NONE &&
        inspectedPawnIndex == -1 &&
        inspectedStockpileIndex == -1 &&
        (currentDebugState == DebugMenuState::PLACING_TILE ||
            currentArchitectMode != ArchitectMode::NONE ||
            (currentDebugState == DebugMenuState::NONE && currentArchitectMode == ArchitectMode::NONE)))
    {
        if (getStratumInfoForZ(currentZ).type < Stratum::OUTER_SPACE_PLANET_VIEW) {
            bool moved = false;
            if (GetAsyncKeyState(VK_UP) & 0x8000) { cursorY = max(0, cursorY - g_cursorSpeed); moved = true; }
            if (GetAsyncKeyState(VK_DOWN) & 0x8000) { cursorY = min(WORLD_HEIGHT - 1, cursorY + g_cursorSpeed); moved = true; }
            if (GetAsyncKeyState(VK_LEFT) & 0x8000) { cursorX = max(0, cursorX - g_cursorSpeed); moved = true; }
            if (GetAsyncKeyState(VK_RIGHT) & 0x8000) { cursorX = min(WORLD_WIDTH - 1, cursorX + g_cursorSpeed); moved = true; }
            if (moved) followedPawnIndex = -1;
            int margin = 5;
            if (cursorX < cameraX + margin) cameraX = max(0, cameraX - 1);
            if (cursorX >= cameraX + VIEWPORT_WIDTH_TILES - margin) cameraX = min(WORLD_WIDTH - VIEWPORT_WIDTH_TILES, cameraX + 1);
            if (cursorY < cameraY + margin) cameraY = max(0, cameraY - 1);
            if (cursorY >= cameraY + VIEWPORT_HEIGHT_TILES - margin) cameraY = min(WORLD_HEIGHT - VIEWPORT_HEIGHT_TILES, cameraY + 1);


        }
    }
}

// In renderResearchPanel(), the state modification logic has been removed.
// The function now only renders the state that handleInput() has already prepared.
void renderResearchPanel(HDC hdc, int width, int height) {
    // 1. Define UI areas and colors
    RECT panelRect = { 50, 30, width - 50, height - 70 };
    const COLORREF yellow = RGB(255, 255, 0);
    const COLORREF white = RGB(255, 255, 255);
    const COLORREF gray = RGB(128, 128, 128);
    const COLORREF green = RGB(0, 255, 128);

    // 2. Draw panel background and border
    HBRUSH bgBrush = CreateSolidBrush(RGB(0, 0, 0));
    FillRect(hdc, &panelRect, bgBrush);
    DeleteObject(bgBrush);
    RENDER_BOX_INSPECTABLE(hdc, panelRect, RGB(255, 255, 255), L"Research Panel Border");

    // 3. Rebuild the project list *first* based on current filters.
    researchUI_projectList.clear();
    for (const auto& pair : g_allResearch) {
        if (pair.second.era == researchUI_selectedEra) {
            if (researchUI_selectedCategory == ResearchCategory::ALL || pair.second.category == researchUI_selectedCategory) {
                researchUI_projectList.push_back(pair.first);
            }
        }
    }
    std::sort(researchUI_projectList.begin(), researchUI_projectList.end(),
        [](const std::wstring& id1, const std::wstring& id2) {
            return g_allResearch.at(id1).name < g_allResearch.at(id2).name;
        });

    // 4. Safely clamp the selected index to ensure it is always valid for the newly built list.
    if (researchUI_projectList.empty()) {
        researchUI_selectedProjectIndex = 0;
    }
    else {
        // This is the key fix: it prevents the index from ever being out of bounds.
        if (researchUI_selectedProjectIndex >= (int)researchUI_projectList.size()) {
            researchUI_selectedProjectIndex = (int)researchUI_projectList.size() - 1;
        }
    }
    researchUI_selectedProjectIndex = max(0, researchUI_selectedProjectIndex);

    // 5. Render Era tabs
    int topBarY = panelRect.top + 20;
    int currentX = panelRect.left + 20;
    for (int i = 0; i < static_cast<int>(ResearchEraNames.size()); ++i) {
        std::wstring text = L" " + ResearchEraNames[i] + L" ";
        COLORREF fg = (i == static_cast<int>(researchUI_selectedEra)) ? yellow : white;
        SIZE size; GetTextExtentPoint32(hdc, text.c_str(), static_cast<int>(text.length()), &size);
        RECT r = { currentX, topBarY, currentX + size.cx, topBarY + size.cy };
        if (i == static_cast<int>(researchUI_selectedEra)) {
            RECT highlightRect = { r.left - 3, r.top - 3, r.right + 3, r.bottom + 3 };
            RENDER_BOX_INSPECTABLE(hdc, highlightRect, yellow, L"Selected Era Box");
        }
        RENDER_TEXT_INSPECTABLE(hdc, text, r.left, r.top, fg);
        if (i < static_cast<int>(ResearchEraNames.size()) - 1) {
            currentX += size.cx + 5;
            HPEN sepPen = CreatePen(PS_SOLID, 1, white); HGDIOBJ oldPen = SelectObject(hdc, sepPen);
            MoveToEx(hdc, currentX, topBarY - 2, NULL); LineTo(hdc, currentX, topBarY + size.cy + 2);
            SelectObject(hdc, oldPen); DeleteObject(sepPen); currentX += 5;
        }
    }

    // 6. Render the rest of the UI (Categories, Separator line)
    int mainPanelY = topBarY + 30;
    int categoryPanelX = panelRect.left + 20;
    int categoryPanelWidth = 150;
    int researchListX = categoryPanelX + categoryPanelWidth + 20;
    int detailPanelX = researchListX + 300 + 20;
    HPEN linePen = CreatePen(PS_SOLID, 1, white);
    HGDIOBJ oldPen = SelectObject(hdc, linePen);
    MoveToEx(hdc, categoryPanelX + categoryPanelWidth + 5, mainPanelY, NULL);
    LineTo(hdc, categoryPanelX + categoryPanelWidth + 5, panelRect.bottom - 40);
    SelectObject(hdc, oldPen); DeleteObject(linePen);
    int currentY = mainPanelY + 10;
    std::vector<std::wstring> sortedCategories = ResearchCategoryNames;
    std::sort(sortedCategories.begin() + 1, sortedCategories.end());
    std::wstring selectedCategoryName = ResearchCategoryNames[static_cast<int>(researchUI_selectedCategory)];
    for (const auto& categoryName : sortedCategories) {
        COLORREF color = (categoryName == selectedCategoryName) ? yellow : white;
        RENDER_TEXT_INSPECTABLE(hdc, categoryName, categoryPanelX, currentY, color, L"Research Category: " + categoryName);
        currentY += 20;
    }

    // 7. Calculate scroll offset and render the project list
    int listStartY = mainPanelY + 10;
    int listHeight = panelRect.bottom - 40 - listStartY;
    int lineHeight = 20;
    int maxVisibleItems = max(1, listHeight / lineHeight);
    if (!researchUI_projectList.empty()) {
        // Ensure selected project index is within bounds
        researchUI_selectedProjectIndex = min(researchUI_selectedProjectIndex, (int)researchUI_projectList.size() - 1);

        if (researchUI_selectedProjectIndex < researchUI_scrollOffset) {
            researchUI_scrollOffset = researchUI_selectedProjectIndex;
        }
        else if (researchUI_selectedProjectIndex >= researchUI_scrollOffset + maxVisibleItems) {
            researchUI_scrollOffset = researchUI_selectedProjectIndex - maxVisibleItems + 1;
        }

        researchUI_scrollOffset = max(0, min(researchUI_scrollOffset, (int)researchUI_projectList.size() - maxVisibleItems));
    }
    else {
        // Reset scroll offset if the project list is empty
        researchUI_scrollOffset = 0;
        researchUI_selectedProjectIndex = 0; // Or set to a safe default
    }
    currentY = listStartY;
    for (int i = researchUI_scrollOffset; i < (int)researchUI_projectList.size() && i < researchUI_scrollOffset + maxVisibleItems; ++i) {
        const auto& projectID = researchUI_projectList[i];
        const auto& project = g_allResearch.at(projectID);
        bool canResearch = true;
        for (const auto& prereq : project.prerequisites) if (g_completedResearch.find(prereq) == g_completedResearch.end()) canResearch = false;
        COLORREF color = (i == researchUI_selectedProjectIndex) ? yellow : (g_completedResearch.count(projectID) ? green : (!canResearch ? gray : white));
        RENDER_TEXT_INSPECTABLE(hdc, project.name, researchListX, currentY, color, L"Research Project: " + project.name);
        currentY += lineHeight;
    }

    // Render Scrollbar
    if (researchUI_projectList.size() > maxVisibleItems) {
        int scrollbarX = researchListX + 300 - 15;
        int scrollbarTop = listStartY;
        int scrollbarHeight = listHeight;
        RECT trackRect = { scrollbarX, scrollbarTop, scrollbarX + 10, scrollbarTop + scrollbarHeight };
        HBRUSH trackBgBrush = CreateSolidBrush(RGB(20, 20, 20)); FillRect(hdc, &trackRect, trackBgBrush); DeleteObject(trackBgBrush);
        HBRUSH trackBorderBrush = CreateSolidBrush(RGB(50, 50, 50)); FrameRect(hdc, &trackRect, trackBorderBrush); DeleteObject(trackBorderBrush);
        float thumbRatio = (float)maxVisibleItems / researchUI_projectList.size();
        int thumbHeight = max(10, (int)(scrollbarHeight * thumbRatio));
        float scrollRange = researchUI_projectList.size() - maxVisibleItems;
        float scrollPercent = (scrollRange > 0) ? (float)researchUI_scrollOffset / scrollRange : 0.0f;
        int thumbY = scrollbarTop + (int)((scrollbarHeight - thumbHeight) * scrollPercent);
        RECT thumbRect = { scrollbarX + 1, thumbY, scrollbarX + 9, thumbY + thumbHeight };
        HBRUSH thumbBgBrush = CreateSolidBrush(RGB(100, 100, 100)); FillRect(hdc, &thumbRect, thumbBgBrush); DeleteObject(thumbBgBrush);
    }

    // 8. Render detail view (this is now safe because the index was clamped)
    if (!researchUI_projectList.empty()) {
        const auto& project = g_allResearch.at(researchUI_projectList[researchUI_selectedProjectIndex]);
        int detailY = mainPanelY + 10;
        RENDER_TEXT_INSPECTABLE(hdc, project.name, detailPanelX, detailY, yellow); detailY += 40;
        RENDER_TEXT_INSPECTABLE(hdc, L"Cost: " + std::to_wstring(project.cost), detailPanelX, detailY, white); detailY += 20;
        RENDER_TEXT_INSPECTABLE(hdc, L"Era: " + ResearchEraNames[static_cast<int>(project.era)], detailPanelX, detailY, white); detailY += 20;
        RENDER_TEXT_INSPECTABLE(hdc, L"Category: " + ResearchCategoryNames[static_cast<int>(project.category)], detailPanelX, detailY, white); detailY += 20;
        if (!project.prerequisites.empty()) {
            detailY += 20; RENDER_TEXT_INSPECTABLE(hdc, L"Requires:", detailPanelX, detailY, white); detailY += 20;
            for (const auto& prereqID : project.prerequisites) {
                const auto& prereqProject = g_allResearch.at(prereqID);
                RENDER_TEXT_INSPECTABLE(hdc, L"  - " + prereqProject.name, detailPanelX, detailY, g_completedResearch.count(prereqID) > 0 ? green : RGB(255, 100, 100)); detailY += 20;
            }
        }
        if (!project.unlocks.empty()) {
            RENDER_TEXT_INSPECTABLE(hdc, L"Unlocks:", detailPanelX, detailY, white); detailY += 20;
            for (const auto& unlock : project.unlocks) {
                RENDER_TEXT_INSPECTABLE(hdc, L"  - " + unlock, detailPanelX, detailY, green); detailY += 20;
            }
        }
        RECT descCalcRect = { 0, 0, (panelRect.right - 20) - detailPanelX, 0 };
        DrawText(hdc, project.description.c_str(), -1, &descCalcRect, DT_WORDBREAK | DT_CALCRECT);
        int descHeight = descCalcRect.bottom;
        int descBottomY = panelRect.bottom - 40;
        int descTopY = descBottomY - descHeight;
        descTopY = max(descTopY, detailY + 10);
        RECT descRect = { detailPanelX, descTopY, panelRect.right - 20, descBottomY };
        renderWrappedText(hdc, project.description, descRect, white);
    }
    else {
        RENDER_TEXT_INSPECTABLE(hdc, L"[No research available for this filter]", researchListX, mainPanelY + 10, gray);
    }

    // 9. Render hint text
    std::wstring hint_text = L"ESC: Close | Arrows: Navigate | L/R: Change Era | Tab: Change Category | Enter: Research | G: Graph";
    RENDER_CENTERED_TEXT_INSPECTABLE(hdc, hint_text, panelRect.bottom - 25, width, green, L"Action Prompt");
}

void renderMinimap(HDC hdc, int startX, int startY) {
    if (currentZ >= TILE_WORLD_DEPTH) return;
    const int pixelSize = 2; int mapW = WORLD_WIDTH * pixelSize, mapH = WORLD_HEIGHT * pixelSize;
    RECT minimapRect = { startX, startY, startX + mapW, startY + mapH };
    RENDER_BOX_INSPECTABLE(hdc, minimapRect, RGB(0, 0, 0), L"Minimap");
    COLORREF pawnColor = RGB(0, 255, 255), borderColor = RGB(255, 255, 255);
    COLORREF cursorColor = RGB(255, 0, 255); // Magenta for the cursor

    // --- NEW: Define colors for critter types on the minimap ---
    COLORREF neutralCritterColor = RGB(0, 255, 0);   // Green for neutral animals
    COLORREF hostileCritterColor = RGB(255, 0, 0);   // Red for hostile undead

    // Draw minimap tiles
    for (int y = 0; y < WORLD_HEIGHT; ++y) {
        for (int x = 0; x < WORLD_WIDTH; ++x) {
            const MapCell& cell = Z_LEVELS[currentZ][y][x];
            COLORREF tileColor;
            if (cell.stockpileId != -1) {
                tileColor = RGB(0, 0, 100);
            }
            else {
                if (cell.type == TileType::EMPTY) continue;
                const TileData& data = TILE_DATA.at(cell.type);
                tileColor = applyLightLevel(data.color, currentLightLevel);
            }
            RECT r = { startX + x * pixelSize, startY + y * pixelSize, startX + (x + 1) * pixelSize, startY + (y + 1) * pixelSize };
            HBRUSH brush = CreateSolidBrush(tileColor);
            FillRect(hdc, &r, brush);
            DeleteObject(brush);

            if (!cell.itemsOnGround.empty()) {
                const TileData& itemData = TILE_DATA.at(cell.itemsOnGround.front());
                HBRUSH itemBrush = CreateSolidBrush(applyLightLevel(itemData.color, currentLightLevel));
                FillRect(hdc, &r, itemBrush);
                DeleteObject(itemBrush);
            }
        }
    }

    // Draw pawns on minimap
    if (currentZ == BIOSPHERE_Z_LEVEL) {
        for (const auto& p : colonists) {
            if (p.x < 0 || p.y < 0) continue;
            RECT r = { startX + p.x * pixelSize, startY + p.y * pixelSize, startX + (p.x + 1) * pixelSize, startY + (p.y + 1) * pixelSize };
            HBRUSH brush = CreateSolidBrush(pawnColor); FillRect(hdc, &r, brush); DeleteObject(brush);
        }
    }

    // Draw critters on minimap
    for (const auto& critter : g_critters) {
        if (critter.z != currentZ) continue; // Only draw critters if they are on the currently displayed Z-level

        if (critter.x < 0 || critter.y < 0) continue;

        COLORREF critterColor;
        // Differentiate between hostile and neutral critters
        if (critter.type == CritterType::ZOMBIE || critter.type == CritterType::SKELETON) {
            critterColor = hostileCritterColor;
        }
        else {
            critterColor = neutralCritterColor;
        }

        RECT r = { startX + critter.x * pixelSize, startY + critter.y * pixelSize, startX + (critter.x + 1) * pixelSize, startY + (critter.y + 1) * pixelSize };
        HBRUSH brush = CreateSolidBrush(critterColor);
        FillRect(hdc, &r, brush);
        DeleteObject(brush);
    }
    // --- END OF NEW CODE ---

    // Draw the cursor
    if (cursorX >= 0 && cursorY >= 0) {
        RECT r = { startX + cursorX * pixelSize, startY + cursorY * pixelSize, startX + (cursorX + 1) * pixelSize, startY + (cursorY + 1) * pixelSize };
        HBRUSH brush = CreateSolidBrush(cursorColor);
        FillRect(hdc, &r, brush);
        DeleteObject(brush);
    }

    // Draw the camera viewport rectangle
    HPEN viewPen = CreatePen(PS_SOLID, 1, RGB(255, 255, 0));
    HGDIOBJ oldViewPen = SelectObject(hdc, viewPen);
    SelectObject(hdc, GetStockObject(NULL_BRUSH));

    int viewRectX1 = startX + cameraX * pixelSize;
    int viewRectY1 = startY + cameraY * pixelSize;
    int viewRectX2 = startX + (cameraX + VIEWPORT_WIDTH_TILES) * pixelSize;
    int viewRectY2 = startY + (cameraY + VIEWPORT_HEIGHT_TILES) * pixelSize;

    Rectangle(hdc, viewRectX1, viewRectY1, viewRectX2, viewRectY2);

    SelectObject(hdc, oldViewPen);
    DeleteObject(viewPen);

    // Draw the outer border of the minimap
    HPEN bPen = CreatePen(PS_SOLID, 1, borderColor);
    HGDIOBJ oldPen = SelectObject(hdc, bPen);
    SelectObject(hdc, GetStockObject(NULL_BRUSH));
    Rectangle(hdc, startX - 1, startY - 1, startX + mapW + 1, startY + mapH + 1);
    SelectObject(hdc, oldPen); DeleteObject(bPen);
}


void renderPlanetView(HDC hdc, int width, int height) {
    RENDER_CENTERED_TEXT_INSPECTABLE(hdc, L"PLANET VIEW: " + solarSystem[0].name, 50, width, RGB(255, 255, 255), L"Menu Title: Planet View");
    const int pixelSize = 4; int mapW = PLANET_MAP_WIDTH * pixelSize, mapH = PLANET_MAP_HEIGHT * pixelSize;
    int ox = (width - mapW) / 2, oy = (height - mapH) / 2;
    for (int y = 0; y < PLANET_MAP_HEIGHT; y++) {
        for (int x = 0; x < PLANET_MAP_WIDTH; x++) {
            RECT r = { ox + x * pixelSize, oy + y * pixelSize, ox + (x + 1) * pixelSize, oy + (y + 1) * pixelSize };
            HBRUSH brush = CreateSolidBrush(BIOME_DATA.at(solarSystem[0].biomeMap[y][x]).mapColor);
            FillRect(hdc, &r, brush); DeleteObject(brush);
        }
    }
    if (landingSiteX != -1 && (GetTickCount() / 400) % 2) {
        RECT r = { ox + landingSiteX * pixelSize, oy + landingSiteY * pixelSize, ox + (landingSiteX + 1) * pixelSize, oy + (landingSiteY + 1) * pixelSize };
        RENDER_BOX_INSPECTABLE(hdc, r, RGB(255, 255, 0), L"Final Landing Site");
    }
}
void renderSystemView(HDC hdc, int width, int height) {
    int sunX = width / 2, sunY = height / 2;
    HBRUSH sunBrush = CreateSolidBrush(RGB(255, 204, 0)); SelectObject(hdc, sunBrush);
    Ellipse(hdc, sunX - 20, sunY - 20, sunX + 20, sunY + 20); DeleteObject(sunBrush);
    for (const auto& planet : solarSystem) {
        HPEN orbitPen = CreatePen(PS_DOT, 1, RGB(50, 50, 50)); HGDIOBJ oldPen = SelectObject(hdc, orbitPen);
        SelectObject(hdc, GetStockObject(NULL_BRUSH)); Ellipse(hdc, sunX - (int)planet.orbitalRadius, sunY - (int)planet.orbitalRadius, sunX + (int)planet.orbitalRadius, sunY + (int)planet.orbitalRadius);
        SelectObject(hdc, oldPen); DeleteObject(orbitPen);
        int planetX = sunX + static_cast<int>(planet.orbitalRadius * cos(planet.currentAngle));
        int planetY = sunY + static_cast<int>(planet.orbitalRadius * sin(planet.currentAngle));
        HBRUSH planetBrush = CreateSolidBrush(planet.color); SelectObject(hdc, planetBrush);
        Ellipse(hdc, planetX - planet.size, planetY - planet.size, planetX + planet.size, planetY + planet.size);
        DeleteObject(planetBrush);
        RENDER_TEXT_INSPECTABLE(hdc, planet.name, planetX - (planet.name.length() * 8) / 2, planetY + planet.size + 5, RGB(200, 200, 200), L"Planet: " + planet.name);
        if (&planet == &solarSystem[0]) {
            HPEN moonOrbitPen = CreatePen(PS_DOT, 1, RGB(80, 80, 80)); oldPen = SelectObject(hdc, moonOrbitPen);
            SelectObject(hdc, GetStockObject(NULL_BRUSH)); Ellipse(hdc, planetX - (int)homeMoon.orbitalRadius, planetY - (int)homeMoon.orbitalRadius, planetX + (int)homeMoon.orbitalRadius, planetY + (int)homeMoon.orbitalRadius);
            SelectObject(hdc, oldPen); DeleteObject(moonOrbitPen);
            int moonX = planetX + static_cast<int>(homeMoon.orbitalRadius * cos(homeMoon.currentAngle));
            int moonY = planetY + static_cast<int>(homeMoon.orbitalRadius * sin(homeMoon.currentAngle));
            HBRUSH moonBrush = CreateSolidBrush(homeMoon.color); SelectObject(hdc, moonBrush);
            Ellipse(hdc, moonX - homeMoon.size, moonY - homeMoon.size, moonX + homeMoon.size, moonY + homeMoon.size);
            DeleteObject(moonBrush);
        }
    }
}
void renderBeyondView(HDC hdc, int width, int height) {
    const int TOP_MARGIN = 40;
    const int BOTTOM_MARGIN = 40;
    RECT viewport = { 0, TOP_MARGIN, width, height - BOTTOM_MARGIN };
    int vp_width = viewport.right - viewport.left;
    int vp_height = viewport.bottom - viewport.top;

    if (vp_width <= 0 || vp_height <= 0) return;

    // --- Render all the stars first ---
    for (const auto& star : distantStars) {
        int screenX = viewport.left + static_cast<int>(fmod(star.x, 1.0f) * vp_width);
        int screenY = viewport.top + static_cast<int>(star.y * vp_height);

        // We only need to check Y bounds, as X will wrap
        if (screenY >= viewport.top && screenY < viewport.bottom) {
            if (star.size > 1) {
                RECT r = { screenX, screenY, screenX + star.size, screenY + star.size };
                HBRUSH starBrush = CreateSolidBrush(star.color);
                FillRect(hdc, &r, starBrush);
                DeleteObject(starBrush);
            }
            else {
                SetPixelV(hdc, screenX, screenY, star.color);
            }
        }
    }

    // --- Now, find and draw the indicator for the home system ---
    if (g_homeSystemStarIndex != -1 && g_homeSystemStarIndex < distantStars.size()) {
        const auto& homeStar = distantStars[g_homeSystemStarIndex];

        // Calculate the home star's absolute screen position
        int homeStarX = viewport.left + static_cast<int>(fmod(homeStar.x, 1.0f) * vp_width);
        int homeStarY = viewport.top + static_cast<int>(homeStar.y * vp_height);

        // Define the indicator's geometry relative to the home star's position
        int box_size = 20;
        int box_x = homeStarX - (box_size / 2);
        int box_y = homeStarY - (box_size / 2);

        POINT box_tr = { box_x + box_size, box_y }; // Top-right of the box
        POINT line_elbow = { box_tr.x + 20, box_tr.y - 20 };
        POINT line_end = { line_elbow.x + 150, line_elbow.y };

        // Create a thick yellow pen
        HPEN hPen = CreatePen(PS_SOLID, 2, RGB(255, 255, 0));
        HGDIOBJ hOldPen = SelectObject(hdc, hPen);

        // Make sure the brush is NULL so rectangles are hollow (transparent)
        SelectObject(hdc, GetStockObject(NULL_BRUSH));

        // Draw the box around the star
        Rectangle(hdc, box_x, box_y, box_x + box_size, box_y + box_size);

        // Draw the lines
        MoveToEx(hdc, box_tr.x, box_tr.y, NULL);
        LineTo(hdc, line_elbow.x, line_elbow.y);
        MoveToEx(hdc, line_elbow.x, line_elbow.y, NULL);
        LineTo(hdc, line_end.x, line_end.y);

        SelectObject(hdc, hOldPen);
        DeleteObject(hPen);

        // Draw the text for the solar system name. NO background.
        RENDER_TEXT_INSPECTABLE(hdc, solarSystemName, line_elbow.x + 5, line_elbow.y - 20, RGB(255, 255, 0));
    }
}

LRESULT CALLBACK window_callback(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
    case WM_CLOSE:
    case WM_DESTROY:
        if (g_hDisplayFont) {
            DeleteObject(g_hDisplayFont);
            g_hDisplayFont = NULL;
        }
        running = false;
        PostQuitMessage(0);
        return 0;

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT clientRect;
        GetClientRect(hwnd, &clientRect);
        int width = clientRect.right;
        int height = clientRect.bottom;

        HDC memDC = CreateCompatibleDC(hdc);
        HBITMAP memBitmap = CreateCompatibleBitmap(hdc, width, height);
        HGDIOBJ oldBitmap = SelectObject(memDC, memBitmap);

        g_inspectorElements.clear(); // Clear inspector data at the start of the frame

        // Apply font selection
        HFONT hFont = NULL;
        if (!g_currentFontFile.empty()) {
            // Try to load the custom font if available
            hFont = CreateFont(16, 9, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, VARIABLE_PITCH | FF_DONTCARE, g_currentFontName.c_str());
        }
        if (hFont == NULL) { // Fallback to default or if custom font failed to load
            hFont = CreateFont(16, 9, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, FIXED_PITCH | FF_MODERN, L"Consolas");
        }
        HFONT hOldFont = (HFONT)SelectObject(memDC, g_hDisplayFont);
        TEXTMETRIC tm;
        GetTextMetrics(memDC, &tm);
        charWidth = tm.tmAveCharWidth;
        charHeight = tm.tmHeight + tm.tmExternalLeading;

        // You can see these in a debugger output window, or a message box if needed.
        OutputDebugStringW((L"Font selected: " + g_currentFontName + L"\n").c_str());
        OutputDebugStringW((L"Calculated charWidth: " + std::to_wstring(charWidth) + L"\n").c_str());
        OutputDebugStringW((L"Calculated charHeight: " + std::to_wstring(charHeight) + L"\n").c_str());

        FillRect(memDC, &clientRect, (HBRUSH)GetStockObject(BLACK_BRUSH));
        SetBkMode(memDC, TRANSPARENT);
        SetTextColor(memDC, RGB(255, 255, 255));

        // Render the base game state first
        switch (currentState) {
        case GameState::MAIN_MENU:
            renderMainMenu(memDC, width, height);
            break;
        case GameState::WORLD_GENERATION_MENU:
            renderWorldGenerationMenu(memDC, width, height);
            break;
        case GameState::PLANET_CUSTOMIZATION_MENU:
            renderPlanetCustomizationMenu(memDC, width, height);
            break;
        case GameState::LANDING_SITE_SELECTION:
            renderLandingSiteSelection(memDC, width, height);
            break;
        case GameState::REGION_SELECTION:
            renderRegionSelection(memDC, width, height);
            break;
        case GameState::PAWN_SELECTION:
            renderColonistSelection(memDC, width, height);
            break;
        case GameState::IN_GAME:
            renderGame(memDC, width, height);
            break;
        }

        // Render overlay UIs (modal or tabbed UIs that appear on top of the game world)
        // Order of rendering here determines Z-order: later ones are on top.

        // 1. Debug UI (highest priority for debugging)
        renderDebugUI(memDC, width, height);

        // 2. Modal panels (Stockpile, Pawn Info)
        if (inspectedStockpileIndex != -1) {
            renderStockpilePanel(memDC, width, height);
        }
        else if (inspectedPawnIndex != -1) {
            renderPawnInfoPanel(memDC, width, height);
        }
        // 3. Tabbed UI panels (only if no modal panels are open and in-game state)
        else if (currentState == GameState::IN_GAME) {
            switch (currentTab) {
            case Tab::RESEARCH:
                // MODIFICATION HERE: Check isInResearchGraphView
                if (isInResearchGraphView) {
                    renderResearchGraph(memDC, width, height);
                }
                else {
                    renderResearchPanel(memDC, width, height);
                }
                break;
            case Tab::STUFFS:
                renderStuffsPanel(memDC, width, height);
                break;
            case Tab::MENU: // The Menu tab also acts as an overlay
                renderMenuPanel(memDC, width, height);
                break;
            default:
                // No specific tab panel to render, game world is visible
                break;
            }
        }

        // 4. Inspector Overlay (always on top when active)
        renderInspectorOverlay(memDC, hwnd);

        BitBlt(hdc, 0, 0, width, height, memDC, 0, 0, SRCCOPY);
        SelectObject(memDC, hOldFont); // Select the old font back to memDC
        // DO NOT DeleteObject(hFont) here, as g_hDisplayFont is managed globally.
        SelectObject(memDC, oldBitmap);
        DeleteObject(memBitmap);
        DeleteDC(memDC);
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_CHAR: {
        if (worldGen_isNaming) {
            if (wParam == VK_BACK) {
                if (worldGen_selectedOption == 0 && !worldName.empty()) worldName.pop_back();
                else if (worldGen_selectedOption == 1 && !solarSystemName.empty()) solarSystemName.pop_back();
            }
            else if (iswprint(static_cast<wint_t>(wParam))) {
                if (worldGen_selectedOption == 0 && worldName.length() < 20) worldName += static_cast<wchar_t>(wParam);
                else if (worldGen_selectedOption == 1 && solarSystemName.length() < 20) solarSystemName += static_cast<wchar_t>(wParam);
            }
        }
        // --- START OF FIX: Spawn Menu Search Logic ---
        else if (isDebugMode && currentDebugState == DebugMenuState::SPAWN) {
            // This block handles all character input for the spawn menu.
            if (spawnMenuIsSearching) {
                // If we are ALREADY searching, handle typing.
                if (wParam == VK_BACK) {
                    if (!spawnMenuSearch.empty()) spawnMenuSearch.pop_back();
                }
                else if (wParam == VK_RETURN) { // Allow Enter to exit search mode
                    spawnMenuIsSearching = false;
                }
                else if (iswprint(static_cast<wint_t>(wParam))) {
                    spawnMenuSearch += static_cast<wchar_t>(wParam);
                }
                spawnMenuSelection = 0; // Reset selection to top of the filtered list on any change
            }
            else {
                // If we are NOT searching, check for the 'S' key to activate search mode.
                if (wParam == 's' || wParam == 'S') {
                    spawnMenuIsSearching = true;
                    spawnMenuSearch = L""; // Clear any previous search
                    // By handling it here and not typing the character, we solve the problem.
                }
            }
        }
        // --- END OF FIX ---
        else if (currentState == GameState::PLANET_CUSTOMIZATION_MENU && planetCustomization_isEditing) {
            if (wParam == VK_BACK && !solarSystem[planetCustomization_selected].name.empty()) solarSystem[planetCustomization_selected].name.pop_back();
            else if (iswprint(static_cast<wint_t>(wParam)) && solarSystem[planetCustomization_selected].name.length() < 20) solarSystem[planetCustomization_selected].name += static_cast<wchar_t>(wParam);
        }
        return 0;
    }

    case WM_KEYDOWN: {
        RECT clientRect;
        GetClientRect(hwnd, &clientRect);
        int windowWidth = clientRect.right;
        int windowHeight = clientRect.bottom;

        bool needsRedraw = true;
        if (wParam == VK_F12) {
            isInspectorModeActive = !isInspectorModeActive;
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        if (wParam == VK_F10) {
            isDebugMode = !isDebugMode;
            currentDebugState = DebugMenuState::NONE;
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        if (wParam == VK_ESCAPE) {
            // Universal ESC handler with priority order
            if (isInResearchGraphView) {
                isInResearchGraphView = false;
            }
            else if (inspectedStockpileIndex != -1) {
                inspectedStockpileIndex = -1;
            }
            else if (currentArchitectMode != ArchitectMode::NONE) {
                currentArchitectMode = ArchitectMode::NONE;
                isDrawingDesignationRect = false;
            }
            else if (inspectedPawnIndex != -1) {
                inspectedPawnIndex = -1;
                followedPawnIndex = -1;
            }
            else if (currentState == GameState::MAIN_MENU && isInFontMenu) {
                isInFontMenu = false;
            }
            else if (currentState == GameState::PLANET_CUSTOMIZATION_MENU && planetCustomization_isEditing) {
                planetCustomization_isEditing = false;
            }
            else if (currentState == GameState::IN_GAME && currentTab == Tab::MENU) {
                if (isInSettingsMenu) {
                    isInSettingsMenu = false;
                }
                else {
                    currentTab = Tab::NONE;
                    if (lastGameSpeed > 0) gameSpeed = lastGameSpeed;
                }
            }
            else if (currentState == GameState::IN_GAME && currentTab != Tab::NONE) {
                currentTab = Tab::NONE;
                isSelectingArchitectGizmo = false;
            }
            else if (isDebugMode && currentDebugState != DebugMenuState::NONE) {
                currentDebugState = DebugMenuState::NONE;
                spawnMenuIsSearching = false;
            }
            else if (currentState == GameState::WORLD_GENERATION_MENU) {
                worldGen_isNaming = false;
                currentState = GameState::MAIN_MENU;
            }
            else if (currentState == GameState::PLANET_CUSTOMIZATION_MENU) {
                currentState = GameState::WORLD_GENERATION_MENU;
            }
            else if (currentState == GameState::LANDING_SITE_SELECTION) {
                currentState = GameState::WORLD_GENERATION_MENU;
            }
            else if (currentState == GameState::REGION_SELECTION) {
                currentState = GameState::LANDING_SITE_SELECTION;
            }
            else if (currentState == GameState::PAWN_SELECTION) {
                currentState = GameState::REGION_SELECTION;
            }
            else if (currentState == GameState::IN_GAME) {
                currentTab = Tab::MENU;
                if (gameSpeed > 0) lastGameSpeed = gameSpeed;
                gameSpeed = 0;
            }
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }

        if (isDebugMode && wParam != VK_F12 && wParam != VK_F10) {
            if (currentDebugState == DebugMenuState::SPAWN) {
                if (spawnMenuIsSearching) {
                    if (wParam == VK_RETURN) spawnMenuIsSearching = false;
                }
                else {
                    std::vector<Spawnable> filteredList;
                    for (const auto& spawnable : g_spawnMenuList) {
                        std::wstring lowerCaseName = spawnable.name; std::transform(lowerCaseName.begin(), lowerCaseName.end(), lowerCaseName.begin(), ::towlower);
                        std::wstring lowerCaseSearch = spawnMenuSearch; std::transform(lowerCaseSearch.begin(), lowerCaseSearch.end(), lowerCaseSearch.begin(), ::towlower);
                        if (spawnMenuSearch.empty() || lowerCaseName.find(lowerCaseSearch) != std::wstring::npos) {
                            filteredList.push_back(spawnable);
                        }
                    }
                    switch (wParam) {
                    case VK_UP: spawnMenuSelection = max(0, spawnMenuSelection - 1); break;
                    case VK_DOWN: if (!filteredList.empty()) spawnMenuSelection = min((int)filteredList.size() - 1, spawnMenuSelection + 1); break;
                    case 'Z':
                        if (!filteredList.empty()) {
                            g_spawnableToPlace = filteredList[spawnMenuSelection];
                            currentDebugState = DebugMenuState::PLACING_TILE;
                            isPlacingWithBrush = (g_spawnableToPlace.type == SpawnableType::TILE) && (GetAsyncKeyState(VK_SHIFT) & 0x8000);
                        }
                        break;
                    default: needsRedraw = false; break;
                    }
                }
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
            else if (currentDebugState == DebugMenuState::PLACING_TILE) {
                if (wParam == 'Z') {
                    if (g_spawnableToPlace.type == SpawnableType::TILE) {
                        Z_LEVELS[currentZ][cursorY][cursorX].type = g_spawnableToPlace.tile_type;
                        Z_LEVELS[currentZ][cursorY][cursorX].underlying_type = g_spawnableToPlace.tile_type;
                    }
                    else if (g_spawnableToPlace.type == SpawnableType::CRITTER) {
                        Critter new_critter;
                        new_critter.type = g_spawnableToPlace.critter_type;
                        new_critter.x = cursorX; new_critter.y = cursorY; new_critter.z = currentZ;
                        new_critter.wanderCooldown = g_CritterData.at(new_critter.type).wander_speed;
                        g_critters.push_back(new_critter);
                    }
                }
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
            else {
                switch (wParam) {
                case VK_F5: isDebugCritterListVisible = !isDebugCritterListVisible; break;
                case VK_F6: currentDebugState = (currentDebugState == DebugMenuState::SPAWN) ? DebugMenuState::NONE : DebugMenuState::SPAWN; spawnMenuSearch = L""; spawnMenuSelection = 0; spawnMenuIsSearching = false; break;
                case VK_F7: currentDebugState = (currentDebugState == DebugMenuState::HOUR) ? DebugMenuState::NONE : DebugMenuState::HOUR; break;
                case VK_F8: currentDebugState = DebugMenuState::WEATHER; currentWeather = (Weather)(((int)currentWeather + 1) % 3); break;
                case VK_F9: isBrightModeActive = !isBrightModeActive; break;
                default: needsRedraw = false; break;
                }
                if (needsRedraw) InvalidateRect(hwnd, nullptr, FALSE);
            }
        }

        if (isInspectorModeActive) { InvalidateRect(hwnd, nullptr, FALSE); return 0; }

        switch (currentState) {
        case GameState::MAIN_MENU: {
            if (isInFontMenu) {
                switch (wParam) {
                case VK_UP: fontMenu_selectedOption = max(0, fontMenu_selectedOption - 1); break;
                case VK_DOWN: if (!g_availableFonts.empty()) { fontMenu_selectedOption = min((int)g_availableFonts.size() - 1, fontMenu_selectedOption + 1); } break;
                case 'Z': case VK_SPACE: case VK_RETURN: {
                    if (!g_currentFontFile.empty()) { RemoveFontResourceExW(g_currentFontFile.c_str(), FR_PRIVATE, NULL); g_currentFontFile = L""; }
                    std::wstring selectedName = g_availableFonts[fontMenu_selectedOption];
                    if (selectedName == L"(Default)") { g_currentFontName = L"Consolas"; }
                    else { g_currentFontName = selectedName; g_currentFontFile = L"Fonts\\" + selectedName + L".ttf"; AddFontResourceExW(g_currentFontFile.c_str(), FR_PRIVATE, NULL); }

                    // After setting g_currentFontName and g_currentFontFile, update the global font.
                    HDC tempHdc = GetDC(hwnd); // Get a temporary HDC for updating
                    UpdateDisplayFont(tempHdc);
                    ReleaseDC(hwnd, tempHdc); // Release the HDC

                    saveFontSelection();
                    isInFontMenu = false;
                } break;
                default: needsRedraw = false; break;
                }
            }
            else {
                switch (wParam) {
                case VK_UP: menuUI_selectedOption = max(0, menuUI_selectedOption - 1); break;
                case VK_DOWN: menuUI_selectedOption = min(2, menuUI_selectedOption + 1); break;
                case 'Z': case VK_SPACE: case VK_RETURN:
                    if (menuUI_selectedOption == 0) { resetGame(); currentState = GameState::WORLD_GENERATION_MENU; }
                    else if (menuUI_selectedOption == 1) { isInFontMenu = true; fontMenu_selectedOption = 0; }
                    else if (menuUI_selectedOption == 2) { running = false; }
                    break;
                default: needsRedraw = false; break;
                }
            }
            break;
        }
        case GameState::WORLD_GENERATION_MENU: {
            if (worldGen_isNaming) {
                if (wParam == VK_RETURN) worldGen_isNaming = false;
            }
            else {
                switch (wParam) {
                case VK_UP: worldGen_selectedOption = max(0, worldGen_selectedOption - 1); break;
                case VK_DOWN: worldGen_selectedOption = min(5, worldGen_selectedOption + 1); break;
                case VK_LEFT:
                    if (worldGen_selectedOption == 2) { numberOfPlanets = max(3, numberOfPlanets - 1); generateSolarSystem(numberOfPlanets, true); }
                    else if (worldGen_selectedOption == 3) { int type = static_cast<int>(selectedWorldType) - 1; if (type < 0) type = 2; selectedWorldType = static_cast<WorldType>(type); }
                    break;
                case VK_RIGHT:
                    if (worldGen_selectedOption == 2) { numberOfPlanets = min(8, numberOfPlanets + 1); generateSolarSystem(numberOfPlanets, true); }
                    else if (worldGen_selectedOption == 3) { int type = (static_cast<int>(selectedWorldType) + 1) % 3; selectedWorldType = static_cast<WorldType>(type); }
                    break;
                case 'Z': case VK_SPACE: case VK_RETURN:
                    if (worldGen_selectedOption == 0 || worldGen_selectedOption == 1) worldGen_isNaming = true;
                    else if (worldGen_selectedOption == 4) { if (solarSystem.empty()) generateSolarSystem(numberOfPlanets, false); currentState = GameState::PLANET_CUSTOMIZATION_MENU; planetCustomization_selected = 0; planetCustomization_isEditing = false; }
                    else if (worldGen_selectedOption == 5) { if (worldName.empty()) worldName = L"Nameless World"; if (solarSystemName.empty()) solarSystemName = L"Nameless System"; if (solarSystem.empty()) generateSolarSystem(numberOfPlanets, false); solarSystem[0].type = selectedWorldType; solarSystem[0].name = L"Homeworld"; generatePlanetMap(solarSystem[0]); generateDistantStars(); currentState = GameState::LANDING_SITE_SELECTION; cursorX = PLANET_MAP_WIDTH / 2; cursorY = PLANET_MAP_HEIGHT / 2; }
                    break;
                default: needsRedraw = false; break;
                }
            }
            break;
        }
        case GameState::PLANET_CUSTOMIZATION_MENU: {
            if (planetCustomization_isEditing) {
                if (wParam == VK_RETURN) planetCustomization_isEditing = false;
            }
            else {
                switch (wParam) {
                case VK_UP: planetCustomization_selected = max(0, planetCustomization_selected - 1); break;
                case VK_DOWN: planetCustomization_selected = min((int)solarSystem.size() - 1, planetCustomization_selected + 1); break;
                case 'Z': case VK_SPACE: case VK_RETURN: planetCustomization_isEditing = true; break;
                default: needsRedraw = false; break;
                }
            }
            break;
        }
        case GameState::LANDING_SITE_SELECTION: {
            if (wParam == VK_UP) cursorY = max(0, cursorY - 1);
            else if (wParam == VK_DOWN) cursorY = min(PLANET_MAP_HEIGHT - 1, cursorY + 1);
            else if (wParam == VK_LEFT) cursorX = max(0, cursorX - 1);
            else if (wParam == VK_RIGHT) cursorX = min(PLANET_MAP_WIDTH - 1, cursorX + 1);
            else if ((wParam == 'Z' || wParam == VK_SPACE || wParam == VK_RETURN) && solarSystem[0].biomeMap[cursorY][cursorX] != Biome::OCEAN) {
                ContinentInfo continent = findContinentInfo(cursorX, cursorY);
                if (continent.found) g_startingTimezoneOffset = continent.timezoneOffset;
                landingSiteX = cursorX; landingSiteY = cursorY; landingBiome = solarSystem[0].biomeMap[cursorY][cursorX];
                generateFullWorld(landingBiome); spawnInitialCritters();
                cursorX = WORLD_WIDTH / 2; cursorY = WORLD_HEIGHT / 2; currentZ = BIOSPHERE_Z_LEVEL;
                cameraX = (WORLD_WIDTH - VIEWPORT_WIDTH_TILES) / 2; cameraY = (WORLD_HEIGHT - VIEWPORT_HEIGHT_TILES) / 2;
                currentState = GameState::REGION_SELECTION;
            }
            else needsRedraw = false;
            break;
        }
        case GameState::REGION_SELECTION: {
            if (wParam == VK_UP) cursorY = max(0, cursorY - 1); else if (wParam == VK_DOWN) cursorY = min(WORLD_HEIGHT - 1, cursorY + 1);
            else if (wParam == VK_LEFT) cursorX = max(0, cursorX - 1); else if (wParam == VK_RIGHT) cursorX = min(WORLD_WIDTH - 1, cursorX + 1);
            else if (wParam == 'Z' || wParam == VK_SPACE || wParam == VK_RETURN) { finalStartX = cursorX; finalStartY = cursorY; preparePawnSelection(); currentState = GameState::PAWN_SELECTION; }
            else needsRedraw = false;
            break;
        }
        case GameState::PAWN_SELECTION: {
            if (wParam == 'R') preparePawnSelection();
            else if (wParam == 'Z' || wParam == VK_SPACE || wParam == VK_RETURN) {
                colonists = rerollablePawns;
                for (auto& p : colonists) {
                    bool placed = false;
                    for (int radius = 0; radius < 20 && !placed; ++radius) {
                        for (int dy = -radius; dy <= radius && !placed; ++dy) {
                            for (int dx = -radius; dx <= radius && !placed; ++dx) {
                                if (abs(dx) != radius && abs(dy) != radius) continue;
                                int checkX = finalStartX + dx, checkY = finalStartY + dy;
                                if (checkX >= 0 && checkX < WORLD_WIDTH && checkY >= 0 && checkY < WORLD_HEIGHT) {
                                    if (isWalkable(checkX, checkY, BIOSPHERE_Z_LEVEL)) {
                                        p.x = checkX; p.y = checkY; p.z = BIOSPHERE_Z_LEVEL; placed = true;
                                    }
                                }
                            }
                        }
                    }
                    if (!placed) { p.x = finalStartX; p.y = finalStartY; p.z = BIOSPHERE_Z_LEVEL; }
                }
                long long baseTicks = 3600 * 12; long long offsetTicks = static_cast<long long>(g_startingTimezoneOffset * 3600);
                gameTicks = baseTicks + offsetTicks;
                updateTime(); currentState = GameState::IN_GAME;
            }
            else if (wParam == 'B') { currentState = GameState::REGION_SELECTION; }
            else needsRedraw = false;
            break;
        }
        case GameState::IN_GAME: {
            if (currentArchitectMode != ArchitectMode::NONE) {
                if (wParam == 'Z') {
                    if (currentArchitectMode == ArchitectMode::DESIGNATING_DECONSTRUCT) {
                        if (!isDrawingDesignationRect) {
                            designationStartX = cursorX; designationStartY = cursorY; isDrawingDesignationRect = true;
                        }
                        else {
                            std::vector<POINT> linePoints = BresenhamLine(designationStartX, designationStartY, cursorX, cursorY);
                            std::set<int> removedStockpileIDs;
                            for (const auto& p : linePoints) {
                                if (p.x < 0 || p.x >= WORLD_WIDTH || p.y < 0 || p.y >= WORLD_HEIGHT) continue;
                                MapCell& cell = Z_LEVELS[currentZ][p.y][p.x];
                                if (cell.stockpileId != -1 && removedStockpileIDs.find(cell.stockpileId) == removedStockpileIDs.end()) {
                                    int id_to_remove = cell.stockpileId; removedStockpileIDs.insert(id_to_remove);
                                    g_stockpiles.erase(std::remove_if(g_stockpiles.begin(), g_stockpiles.end(), [id_to_remove](const Stockpile& sp) { return sp.id == id_to_remove; }), g_stockpiles.end());
                                    for (int z = 0; z < TILE_WORLD_DEPTH; ++z) for (int y = 0; y < WORLD_HEIGHT; ++y) for (int x = 0; x < WORLD_WIDTH; ++x) if (Z_LEVELS[z][y][x].stockpileId == id_to_remove) Z_LEVELS[z][y][x].stockpileId = -1;
                                }
                                else if (isDeconstructable(cell.type) && designations[p.y][p.x] == L' ') {
                                    jobQueue.push_back({ JobType::Deconstruct, (int)p.x, (int)p.y, currentZ });
                                    designations[p.y][p.x] = L'D';
                                }
                            }
                            isDrawingDesignationRect = false; currentArchitectMode = ArchitectMode::NONE;
                        }
                    }
                    else if (currentArchitectMode == ArchitectMode::DESIGNATING_BUILD) {
                        if (buildableToPlace == TileType::WOOD_FLOOR || buildableToPlace == TileType::WALL) {
                            if (!isDrawingDesignationRect) {
                                designationStartX = cursorX; designationStartY = cursorY; isDrawingDesignationRect = true;
                            }
                            else {
                                if (buildableToPlace == TileType::WOOD_FLOOR) {
                                    int x1 = min(designationStartX, cursorX), y1 = min(designationStartY, cursorY), x2 = max(designationStartX, cursorX), y2 = max(designationStartY, cursorY);
                                    for (int py = y1; py <= y2; ++py) for (int px = x1; px <= x2; ++px) if (CanBuildOn(px, py, currentZ, buildableToPlace)) {
                                        bool isReachable = false;
                                        if (!g_isTileReachable.empty()) for (int dy = -2; dy <= 2 && !isReachable; ++dy) for (int dx = -2; dx <= 2 && !isReachable; ++dx) {
                                            if (dx >= -1 && dx <= 1 && dy >= -1 && dy <= 1) continue;
                                            int checkX = px + dx, checkY = py + dy; if (checkX >= 0 && checkX < WORLD_WIDTH && checkY >= 0 && checkY < WORLD_HEIGHT) if (g_isTileReachable[currentZ][checkY][checkX]) isReachable = true;
                                        }
                                        if (isReachable) { MapCell& cell = Z_LEVELS[currentZ][py][px]; cell.type = TileType::BLUEPRINT; cell.target_type = buildableToPlace; jobQueue.push_back({ JobType::Build, px, py, currentZ }); }
                                    }
                                }
                                else {
                                    std::vector<POINT> linePoints = BresenhamLine(designationStartX, designationStartY, cursorX, cursorY);
                                    for (const auto& p : linePoints) if (CanBuildOn(p.x, p.y, currentZ, buildableToPlace)) {
                                        bool isReachable = false;
                                        if (!g_isTileReachable.empty()) for (int dy = -1; dy <= 1 && !isReachable; ++dy) for (int dx = -1; dx <= 1 && !isReachable; ++dx) {
                                            if (dx == 0 && dy == 0) continue;
                                            int checkX = p.x + dx, checkY = p.y + dy; if (checkX >= 0 && checkX < WORLD_WIDTH && checkY >= 0 && checkY < WORLD_HEIGHT) if (g_isTileReachable[currentZ][checkY][checkX]) isReachable = true;
                                        }
                                        if (isReachable) { MapCell& cell = Z_LEVELS[currentZ][p.y][p.x]; cell.type = TileType::BLUEPRINT; cell.target_type = buildableToPlace; jobQueue.push_back({ JobType::Build, (int)p.x, (int)p.y, currentZ }); }
                                    }
                                }
                                isDrawingDesignationRect = false; g_isTileReachable.clear();
                            }
                        }
                        else {
                            if (CanBuildOn(cursorX, cursorY, currentZ, buildableToPlace)) {
                                bool isReachable = false;
                                if (!g_isTileReachable.empty()) for (int dy = -1; dy <= 1 && !isReachable; ++dy) for (int dx = -1; dx <= 1 && !isReachable; ++dx) {
                                    if (dx == 0 && dy == 0) continue;
                                    int checkX = cursorX + dx, checkY = cursorY + dy; if (checkX >= 0 && checkX < WORLD_WIDTH && checkY >= 0 && checkY < WORLD_HEIGHT) if (g_isTileReachable[currentZ][checkY][checkX]) isReachable = true;
                                }
                                if (isReachable) { MapCell& cell = Z_LEVELS[currentZ][cursorY][cursorX]; cell.type = TileType::BLUEPRINT; cell.target_type = buildableToPlace; jobQueue.push_back({ JobType::Build, cursorX, cursorY, currentZ }); }
                            }
                        }
                    }
                    else if (currentArchitectMode == ArchitectMode::DESIGNATING_MINE || currentArchitectMode == ArchitectMode::DESIGNATING_CHOP || currentArchitectMode == ArchitectMode::DESIGNATING_STOCKPILE) {
                        if (!isDrawingDesignationRect) { designationStartX = cursorX; designationStartY = cursorY; isDrawingDesignationRect = true; }
                        else {
                            int x1 = min(designationStartX, cursorX), y1 = min(designationStartY, cursorY), x2 = max(designationStartX, cursorX), y2 = max(designationStartY, cursorY);
                            if (currentArchitectMode == ArchitectMode::DESIGNATING_CHOP) {
                                std::set<int> processedTreeIDs;
                                for (int dy = y1; dy <= y2; ++dy) for (int dx = x1; dx <= x2; ++dx) {
                                    if (dx < 0 || dx >= WORLD_WIDTH || dy < 0 || dy >= WORLD_HEIGHT) continue;
                                    MapCell& cell = Z_LEVELS[currentZ][dy][dx];
                                    if (cell.tree != nullptr && processedTreeIDs.find(cell.tree->id) == processedTreeIDs.end()) {
                                        processedTreeIDs.insert(cell.tree->id);
                                        if (a_trees.count(cell.tree->id)) for (const auto& part : a_trees.at(cell.tree->id).parts) if (part.x >= 0 && part.x < WORLD_WIDTH && part.y >= 0 && part.y < WORLD_HEIGHT) {
                                            const auto& tags = TILE_DATA.at(part.type).tags;
                                            if (std::find(tags.begin(), tags.end(), TileTag::TREE_TRUNK) != tags.end() || std::find(tags.begin(), tags.end(), TileTag::TREE_BRANCH) != tags.end()) designations[part.y][part.x] = L'C';
                                        }
                                    }
                                }
                            }
                            else {
                                if (currentArchitectMode == ArchitectMode::DESIGNATING_STOCKPILE) {
                                    Stockpile sp; sp.id = nextStockpileId++; sp.rect = { (long)x1, (long)y1, (long)x2, (long)y2 }; sp.z = currentZ;
                                    for (const auto& group : g_haulableItemsGrouped) for (TileType item : group.second) sp.acceptedResources.insert(item);
                                    g_stockpiles.push_back(sp);
                                }
                                for (int dy = y1; dy <= y2; ++dy) for (int dx = x1; dx <= x2; ++dx) {
                                    if (dx < 0 || dx >= WORLD_WIDTH || dy < 0 || dy >= WORLD_HEIGHT) continue;
                                    MapCell& cell = Z_LEVELS[currentZ][dy][dx];
                                    if (currentArchitectMode == ArchitectMode::DESIGNATING_MINE) {
                                        const auto& tags = TILE_DATA.at(cell.type).tags;
                                        if ((std::find(tags.begin(), tags.end(), TileTag::STONE) != tags.end() || std::find(tags.begin(), tags.end(), TileTag::ORE) != tags.end()) && cell.type != TileType::EMPTY && designations[dy][dx] == L' ') {
                                            jobQueue.push_back({ JobType::Mine, dx, dy, currentZ }); designations[dy][dx] = L'M';
                                        }
                                    }
                                    else if (currentArchitectMode == ArchitectMode::DESIGNATING_STOCKPILE) {
                                        const auto& tags = TILE_DATA.at(cell.type).tags;
                                        if (!(cell.tree != nullptr || std::find(tags.begin(), tags.end(), TileTag::STRUCTURE) != tags.end() || std::find(tags.begin(), tags.end(), TileTag::FURNITURE) != tags.end())) cell.stockpileId = g_stockpiles.back().id;
                                    }
                                }
                            }
                            isDrawingDesignationRect = false; currentArchitectMode = ArchitectMode::NONE;
                        }
                    }
                }
                else { needsRedraw = false; }
            }
            else if (isSelectingArchitectGizmo) {
                std::vector<std::pair<std::wstring, TileType>> dynamicGizmos; int gizmoCount = 0;
                if (currentArchitectCategory == ArchitectCategory::ORDERS) gizmoCount = 3;
                else if (currentArchitectCategory == ArchitectCategory::ZONES) gizmoCount = 1;
                else { dynamicGizmos = getAvailableGizmos(currentArchitectCategory); gizmoCount = dynamicGizmos.size(); }
                switch (wParam) {
                case VK_UP: architectGizmoSelection = max(0, architectGizmoSelection - 1); break;
                case VK_DOWN: if (gizmoCount > 0) architectGizmoSelection = min(gizmoCount - 1, architectGizmoSelection + 1); break;
                case VK_LEFT: isSelectingArchitectGizmo = false; break;
                case 'Z': case VK_SPACE: case VK_RETURN:
                    if (currentArchitectCategory == ArchitectCategory::ORDERS) {
                        if (architectGizmoSelection == 0) currentArchitectMode = ArchitectMode::DESIGNATING_MINE;
                        else if (architectGizmoSelection == 1) currentArchitectMode = ArchitectMode::DESIGNATING_CHOP;
                        else if (architectGizmoSelection == 2) currentArchitectMode = ArchitectMode::DESIGNATING_DECONSTRUCT;
                    }
                    else if (currentArchitectCategory == ArchitectCategory::ZONES) {
                        if (architectGizmoSelection == 0) currentArchitectMode = ArchitectMode::DESIGNATING_STOCKPILE;
                    }
                    else if (!dynamicGizmos.empty() && architectGizmoSelection < dynamicGizmos.size()) {
                        currentArchitectMode = ArchitectMode::DESIGNATING_BUILD;
                        computeGlobalReachability();
                        buildableToPlace = dynamicGizmos[architectGizmoSelection].second;
                    }
                    isSelectingArchitectGizmo = false; currentTab = Tab::NONE; break;
                default: needsRedraw = false; break;
                }
            }
            else if (inspectedPawnIndex != -1) {
                switch (wParam) {
                case VK_UP:
                    pawnInfo_selectedLine = max(0, pawnInfo_selectedLine - 1);
                    break;
                case VK_DOWN: {
                    std::vector<std::pair<std::wstring, std::wstring>> selectableContent;
                    Pawn& pawn = colonists[inspectedPawnIndex];
                    switch (currentPawnInfoTab) {
                    case PawnInfoTab::ITEMS:
                        if (pawn.inventory.empty()) selectableContent.push_back({ L"", L"" });
                        else for (const auto& stack : pawn.inventory) selectableContent.push_back({ L"", L"" });
                        break;
                    case PawnInfoTab::SKILLS:
                        for (const auto& skill : pawn.skills) selectableContent.push_back({ L"", L"" });
                        break;
                    default:
                        selectableContent.push_back({ L"", L"" }); selectableContent.push_back({ L"", L"" }); selectableContent.push_back({ L"", L"" }); break;
                    }
                    if (!selectableContent.empty()) pawnInfo_selectedLine = min((int)selectableContent.size() - 1, pawnInfo_selectedLine + 1);
                    break;
                }
                case VK_LEFT: {
                    int currentTabIdx = static_cast<int>(currentPawnInfoTab); currentTabIdx = max(0, currentTabIdx - 1);
                    currentPawnInfoTab = static_cast<PawnInfoTab>(currentTabIdx);
                    pawnInfo_selectedLine = 0; pawnInfo_scrollOffset = 0; pawnItems_scrollOffset = 0;
                    break;
                }
                case VK_RIGHT: {
                    int currentTabIdx = static_cast<int>(currentPawnInfoTab); currentTabIdx = min(static_cast<int>(PawnInfoTab::PERSONALITY), currentTabIdx + 1);
                    currentPawnInfoTab = static_cast<PawnInfoTab>(currentTabIdx);
                    pawnInfo_selectedLine = 0; pawnInfo_scrollOffset = 0; pawnItems_scrollOffset = 0;
                    break;
                }
                }
                InvalidateRect(hwnd, nullptr, FALSE); return 0;
            }
            else if (inspectedStockpileIndex != -1) {
                if (inspectedStockpileIndex >= g_stockpiles.size()) { inspectedStockpileIndex = -1; return 0; }
                Stockpile& sp = g_stockpiles[inspectedStockpileIndex];
                std::vector<std::pair<TileTag, int>> displayItemsFlat;
                for (TileTag categoryTag : stockpilePanel_displayCategoriesOrder) {
                    if (g_haulableItemsGrouped.count(categoryTag) == 0 || g_haulableItemsGrouped.at(categoryTag).empty()) continue;
                    displayItemsFlat.push_back({ categoryTag, -1 });
                    if (stockpilePanel_categoryExpanded[categoryTag]) for (size_t i = 0; i < g_haulableItemsGrouped.at(categoryTag).size(); ++i) displayItemsFlat.push_back({ categoryTag, (int)i });
                }
                int totalListItems = displayItemsFlat.size();
                switch (wParam) {
                case VK_UP:
                    if (stockpilePanel_selectedLineIndex > 0) stockpilePanel_selectedLineIndex--;
                    else if (stockpilePanel_selectedLineIndex == 0) stockpilePanel_selectedLineIndex = -2;
                    else if (stockpilePanel_selectedLineIndex == -1) stockpilePanel_selectedLineIndex = totalListItems > 0 ? totalListItems - 1 : -2;
                    else if (stockpilePanel_selectedLineIndex == -2) stockpilePanel_selectedLineIndex = -1;
                    break;
                case VK_DOWN:
                    if (stockpilePanel_selectedLineIndex >= 0 && stockpilePanel_selectedLineIndex < totalListItems - 1) stockpilePanel_selectedLineIndex++;
                    else if (stockpilePanel_selectedLineIndex == totalListItems - 1) stockpilePanel_selectedLineIndex = -1;
                    else if (stockpilePanel_selectedLineIndex == -1) stockpilePanel_selectedLineIndex = -2;
                    else if (stockpilePanel_selectedLineIndex == -2) stockpilePanel_selectedLineIndex = totalListItems > 0 ? 0 : -1;
                    break;
                case 'A': for (const auto& group : g_haulableItemsGrouped) for (TileType item : group.second) sp.acceptedResources.insert(item); break;
                case 'D': sp.acceptedResources.clear(); break;
                case VK_LEFT: case VK_RIGHT: case 'Z': case VK_SPACE: case VK_RETURN: {
                    if (stockpilePanel_selectedLineIndex == -1) { for (const auto& group : g_haulableItemsGrouped) for (TileType item : group.second) sp.acceptedResources.insert(item); }
                    else if (stockpilePanel_selectedLineIndex == -2) { sp.acceptedResources.clear(); }
                    else if (stockpilePanel_selectedLineIndex >= 0) {
                        auto& selected_item = displayItemsFlat[stockpilePanel_selectedLineIndex];
                        if (selected_item.second == -1) stockpilePanel_categoryExpanded[selected_item.first] = !stockpilePanel_categoryExpanded[selected_item.first];
                        else {
                            TileType itemType = g_haulableItemsGrouped.at(selected_item.first)[selected_item.second];
                            if (sp.acceptedResources.count(itemType)) sp.acceptedResources.erase(itemType);
                            else sp.acceptedResources.insert(itemType);
                        }
                    }
                    break;
                }
                default: needsRedraw = false; break;
                }
            }
            else if (currentTab == Tab::WORK) {
                if (!colonists.empty()) {
                    workUI_selectedPawn = max(0, min((int)colonists.size() - 1, workUI_selectedPawn));
                    workUI_selectedJob = max(0, min((int)JobTypeNames.size() - 1, workUI_selectedJob));
                }
                else { workUI_selectedPawn = -1; workUI_selectedJob = -1; }
                switch (wParam) {
                case VK_UP: if (workUI_selectedPawn > 0) workUI_selectedPawn--; break;
                case VK_DOWN: if (workUI_selectedPawn != -1 && workUI_selectedPawn < (int)colonists.size() - 1) workUI_selectedPawn++; break;
                case VK_LEFT: if (workUI_selectedJob > 0) workUI_selectedJob--; break;
                case VK_RIGHT: if (workUI_selectedJob != -1 && workUI_selectedJob < (int)JobTypeNames.size() - 1) workUI_selectedJob++; break;
                case VK_PRIOR: if (workUI_selectedPawn != -1 && workUI_selectedJob != -1) colonists[workUI_selectedPawn].priorities.at((JobType)workUI_selectedJob) = min(4, colonists[workUI_selectedPawn].priorities.at((JobType)workUI_selectedJob) + 1); break;
                case VK_NEXT: if (workUI_selectedPawn != -1 && workUI_selectedJob != -1) colonists[workUI_selectedPawn].priorities.at((JobType)workUI_selectedJob) = max(0, colonists[workUI_selectedPawn].priorities.at((JobType)workUI_selectedJob) - 1); break;
                default: needsRedraw = false; break;
                }
            }
            else if (currentTab == Tab::RESEARCH) {
                if (isInResearchGraphView) {
                    if (wParam == 'G') isInResearchGraphView = false;
                    else if (wParam == VK_LEFT) researchGraphScrollX = max(0, researchGraphScrollX - scaledRankSpacingX / 2);
                    else if (wParam == VK_RIGHT) researchGraphScrollX = min(totalGraphWidth - availablePanelWidth, researchGraphScrollX + scaledRankSpacingX / 2);
                    else needsRedraw = false;
                }
                else {
                    bool listNeedsUpdate = false;
                    switch (wParam) {
                    case VK_UP: researchUI_selectedProjectIndex--; break;
                    case VK_DOWN: researchUI_selectedProjectIndex++; break;
                    case VK_LEFT: researchUI_selectedEra = (ResearchEra)max(0, (int)researchUI_selectedEra - 1); listNeedsUpdate = true; break;
                    case VK_RIGHT: researchUI_selectedEra = (ResearchEra)min((int)ResearchEraNames.size() - 1, (int)researchUI_selectedEra + 1); listNeedsUpdate = true; break;
                    case VK_TAB: {
                        std::vector<std::wstring> sortedCategories = ResearchCategoryNames; std::sort(sortedCategories.begin() + 1, sortedCategories.end());
                        std::wstring currentCategoryName = ResearchCategoryNames[static_cast<int>(researchUI_selectedCategory)]; int currentSortedIndex = -1;
                        for (size_t i = 0; i < sortedCategories.size(); ++i) if (sortedCategories[i] == currentCategoryName) { currentSortedIndex = i; break; }
                        int nextSortedIndex = (currentSortedIndex + 1) % sortedCategories.size(); std::wstring nextCategoryName = sortedCategories[nextSortedIndex];
                        for (size_t i = 0; i < ResearchCategoryNames.size(); ++i) if (ResearchCategoryNames[i] == nextCategoryName) { researchUI_selectedCategory = static_cast<ResearchCategory>(i); break; }
                        listNeedsUpdate = true; break;
                    }
                    case 'G': isInResearchGraphView = true; researchGraphScrollX = 0; break;
                    case 'Z': case VK_SPACE: case VK_RETURN:
                        if (!researchUI_projectList.empty()) {
                            const auto& project = g_allResearch.at(researchUI_projectList[researchUI_selectedProjectIndex]);
                            bool canResearch = g_completedResearch.find(project.id) == g_completedResearch.end();
                            for (const auto& prereq : project.prerequisites) if (g_completedResearch.find(prereq) == g_completedResearch.end()) canResearch = false;
                            if (canResearch) {
                                int benchX = -1, benchY = -1;
                                for (int y = 0; y < WORLD_HEIGHT && benchX == -1; ++y) for (int x = 0; x < WORLD_WIDTH && benchX == -1; ++x) if (Z_LEVELS[BIOSPHERE_Z_LEVEL][y][x].type == TileType::RESEARCH_BENCH) { benchX = x; benchY = y; }
                                if (benchX != -1) {
                                    g_currentResearchProject = project.id; g_researchProgress = 0;
                                    int targetX = -1, targetY = -1;
                                    for (int dy = -1; dy <= 1 && targetX == -1; ++dy) for (int dx = -1; dx <= 1 && targetX == -1; ++dx) if (isWalkable(benchX + dx, benchY + dy, BIOSPHERE_Z_LEVEL)) { targetX = benchX + dx; targetY = benchY + dy; }
                                    if (targetX != -1) jobQueue.push_back({ JobType::Research, targetX, targetY, BIOSPHERE_Z_LEVEL });
                                    currentTab = Tab::NONE;
                                }
                            }
                        }
                        break;
                    default: needsRedraw = false; break;
                    }
                    if (listNeedsUpdate) { researchUI_selectedProjectIndex = 0; researchUI_scrollOffset = 0; }
                }
            }
            else if (currentTab == Tab::STUFFS) {
                std::vector<TileType> itemsToShow;
                if (currentStuffsCategory != StuffsCategory::CRITTERS) {
                    for (const auto& pair : TILE_DATA) {
                        bool shouldAdd = false;
                        const auto& tags = pair.second.tags;
                        if (pair.first == TileType::EMPTY || pair.first == TileType::BLUEPRINT || std::find(tags.begin(), tags.end(), TileTag::TREE_PART) != tags.end() || std::find(tags.begin(), tags.end(), TileTag::STRUCTURE) != tags.end() || std::find(tags.begin(), tags.end(), TileTag::FURNITURE) != tags.end() || std::find(tags.begin(), tags.end(), TileTag::LIGHTS) != tags.end() || std::find(tags.begin(), tags.end(), TileTag::PRODUCTION) != tags.end() || std::find(tags.begin(), tags.end(), TileTag::STOCKPILE_ZONE) != tags.end()) continue;
                        switch (currentStuffsCategory) {
                        case StuffsCategory::STONES: if (std::any_of(tags.begin(), tags.end(), [](TileTag t) { return t == TileTag::SEDIMENTARY || t == TileTag::IGNEOUS_INTRUSIVE || t == TileTag::IGNEOUS_EXTRUSIVE || t == TileTag::METAMORPHIC || t == TileTag::INNER_STONE || t == TileTag::STONE; }) && std::find(tags.begin(), tags.end(), TileTag::CHUNK) == tags.end() && std::find(tags.begin(), tags.end(), TileTag::ORE) == tags.end()) shouldAdd = true; break;
                        case StuffsCategory::CHUNKS: if (std::find(tags.begin(), tags.end(), TileTag::CHUNK) != tags.end()) shouldAdd = true; break;
                        case StuffsCategory::WOODS: if (std::find(tags.begin(), tags.end(), TileTag::WOOD) != tags.end()) shouldAdd = true; break;
                        case StuffsCategory::METALS: if (std::find(tags.begin(), tags.end(), TileTag::METAL) != tags.end()) shouldAdd = true; break;
                        case StuffsCategory::ORES: if (std::find(tags.begin(), tags.end(), TileTag::ORE) != tags.end()) shouldAdd = true; break;
                        case StuffsCategory::TREES: switch (pair.first) { case TileType::OAK: case TileType::ACACIA: case TileType::SPRUCE: case TileType::BIRCH: case TileType::PINE: case TileType::POPLAR: case TileType::CECROPIA: case TileType::COCOA: case TileType::CYPRESS: case TileType::MAPLE: case TileType::PALM: case TileType::TEAK: case TileType::SAGUARO: case TileType::PRICKLYPEAR: case TileType::CHOLLA: shouldAdd = true; break; default: break; } break;
                        case StuffsCategory::CRITTERS: break;
                        }
                        if (shouldAdd) itemsToShow.push_back(pair.first);
                    }
                }
                std::vector<CritterType> crittersToShow;
                if (currentStuffsCategory == StuffsCategory::CRITTERS) for (const auto& pair : g_CritterData) crittersToShow.push_back(pair.first);
                if (!itemsToShow.empty() && g_stuffsAlphabeticalSort) std::sort(itemsToShow.begin(), itemsToShow.end(), [](TileType a, TileType b) { return TILE_DATA.at(a).name < TILE_DATA.at(b).name; });
                if (!crittersToShow.empty() && g_stuffsAlphabeticalSort) std::sort(crittersToShow.begin(), crittersToShow.end(), [](CritterType a, CritterType b) { return g_CritterData.at(a).name < g_CritterData.at(b).name; });
                int panelHeight_calc = 400, panelY_calc = windowHeight - panelHeight_calc - 60;
                RECT panelRect_calc = { 0, panelY_calc, 0, panelY_calc + panelHeight_calc };
                int itemListStartY_calc = panelRect_calc.top + 45, itemListEndY_calc = panelRect_calc.bottom - 60;
                int lineHeight_calc = 18, maxVisibleItems = max(0, (itemListEndY_calc - itemListStartY_calc) / lineHeight_calc);
                size_t listSize = (currentStuffsCategory == StuffsCategory::CRITTERS) ? crittersToShow.size() : itemsToShow.size();
                switch (wParam) {
                case VK_UP: stuffsUI_selectedItem = max(0, stuffsUI_selectedItem - 1); break;
                case VK_DOWN: if (listSize > 0) stuffsUI_selectedItem = min((int)listSize - 1, stuffsUI_selectedItem + 1); break;
                case VK_LEFT: currentStuffsCategory = (StuffsCategory)max(0, (int)currentStuffsCategory - 1); stuffsUI_selectedItem = 0; stuffsUI_scrollOffset = 0; break;
                case VK_RIGHT: currentStuffsCategory = (StuffsCategory)min((int)StuffsCategoryNames.size() - 1, (int)currentStuffsCategory + 1); stuffsUI_selectedItem = 0; stuffsUI_scrollOffset = 0; break;
                case 'O': g_stuffsAlphabeticalSort = !g_stuffsAlphabeticalSort; stuffsUI_selectedItem = 0; stuffsUI_scrollOffset = 0; break;
                default: needsRedraw = false; break;
                }
                if (listSize > 0) {
                    if (stuffsUI_selectedItem < stuffsUI_scrollOffset) stuffsUI_scrollOffset = stuffsUI_selectedItem;
                    else if (stuffsUI_selectedItem >= stuffsUI_scrollOffset + maxVisibleItems) stuffsUI_scrollOffset = stuffsUI_selectedItem - maxVisibleItems + 1;
                }
            }
            else if (currentTab == Tab::MENU) {
                if (isInSettingsMenu) {
                    switch (wParam) {
                    case VK_UP: settingsUI_selectedOption = max(0, settingsUI_selectedOption - 1); break;
                    case VK_DOWN: settingsUI_selectedOption = min(4, settingsUI_selectedOption + 1); break;
                    case VK_LEFT: case VK_RIGHT: case VK_RETURN:
                        switch (settingsUI_selectedOption) {
                        case 2: if (wParam == VK_LEFT) targetFPS = max(30, targetFPS - 15); else targetFPS = min(240, targetFPS + 15); break;
                        case 3: if (wParam == VK_LEFT) g_cursorSpeed = max(1, g_cursorSpeed - 1); else g_cursorSpeed = min(5, g_cursorSpeed + 1); break;
                        case 4: isDebugMode = !isDebugMode; break;
                        } break;
                    }
                }
                else {
                    switch (wParam) {
                    case VK_UP: menuUI_selectedOption = max(0, menuUI_selectedOption - 1); break;
                    case VK_DOWN: menuUI_selectedOption = min(3, menuUI_selectedOption + 1); break;
                    case 'Z': case VK_SPACE: case VK_RETURN:
                        switch (menuUI_selectedOption) {
                        case 0: currentTab = Tab::NONE; gameSpeed = lastGameSpeed; break;
                        case 1: isInSettingsMenu = true; settingsUI_selectedOption = 0; break;
                        case 2: currentState = GameState::MAIN_MENU; gameSpeed = 1; break;
                        case 3: running = false; break;
                        } break;
                    }
                }
                InvalidateRect(hwnd, nullptr, FALSE); return 0;
            }
            else {
                if (wParam == VK_PRIOR) { currentZ = min(TILE_WORLD_DEPTH + 2, currentZ + 1); }
                else if (wParam == VK_NEXT) { currentZ = max(0, currentZ - 1); }
                else if (wParam == VK_SPACE) { if (gameSpeed > 0) { lastGameSpeed = gameSpeed; gameSpeed = 0; } else { gameSpeed = lastGameSpeed; } }
                else if (wParam == VK_F1) gameSpeed = 0; else if (wParam == VK_F2) gameSpeed = 1; else if (wParam == VK_F3) gameSpeed = 2; else if (wParam == VK_F4) gameSpeed = 3; else if (wParam == VK_F5) gameSpeed = 4;
                else if (wParam == 'A') { currentTab = (currentTab == Tab::ARCHITECT) ? Tab::NONE : Tab::ARCHITECT; isSelectingArchitectGizmo = false; currentArchitectCategory = ArchitectCategory::ORDERS; }
                else if (wParam == 'W') { if (currentTab == Tab::WORK) { currentTab = Tab::NONE; } else { currentTab = Tab::WORK; workUI_selectedPawn = colonists.empty() ? -1 : 0; workUI_selectedJob = 0; } }
                else if (wParam == 'R') {
                    if (currentTab == Tab::RESEARCH) currentTab = Tab::NONE;
                    else { currentTab = Tab::RESEARCH; isInResearchGraphView = false; researchUI_selectedProjectIndex = 0; researchUI_scrollOffset = 0; researchUI_selectedEra = ResearchEra::NEOLITHIC; researchUI_selectedCategory = ResearchCategory::ALL; }
                }
                else if (wParam == 'S') { currentTab = (currentTab == Tab::STUFFS) ? Tab::NONE : Tab::STUFFS; }
                else if (wParam == 'E') {
                    if (currentTab == Tab::NONE && gameSpeed > 0) lastGameSpeed = gameSpeed;
                    currentTab = (currentTab == Tab::MENU) ? Tab::NONE : Tab::MENU;
                    if (currentTab == Tab::MENU) gameSpeed = 0; else gameSpeed = lastGameSpeed;
                    menuUI_selectedOption = 0; isInSettingsMenu = false;
                }
                else if (currentTab == Tab::ARCHITECT) {
                    if (isSelectingArchitectGizmo) {
                        std::vector<std::pair<std::wstring, TileType>> dynamicGizmos; int gizmoCount = 0;
                        if (currentArchitectCategory == ArchitectCategory::ORDERS) gizmoCount = 3; else if (currentArchitectCategory == ArchitectCategory::ZONES) gizmoCount = 1; else { dynamicGizmos = getAvailableGizmos(currentArchitectCategory); gizmoCount = dynamicGizmos.size(); }
                        if (wParam == VK_UP) architectGizmoSelection = max(0, architectGizmoSelection - 1);
                        else if (wParam == VK_DOWN) architectGizmoSelection = min(gizmoCount - 1, architectGizmoSelection + 1);
                        else if (wParam == VK_LEFT) isSelectingArchitectGizmo = false;
                        else if (wParam == 'Z' || wParam == VK_SPACE || wParam == VK_RETURN) {
                            if (currentArchitectCategory == ArchitectCategory::ORDERS) {
                                if (architectGizmoSelection == 0) currentArchitectMode = ArchitectMode::DESIGNATING_MINE;
                                else if (architectGizmoSelection == 1) currentArchitectMode = ArchitectMode::DESIGNATING_CHOP;
                                else if (architectGizmoSelection == 2) currentArchitectMode = ArchitectMode::DESIGNATING_DECONSTRUCT;
                            }
                            else if (currentArchitectCategory == ArchitectCategory::ZONES) {
                                if (architectGizmoSelection == 0) currentArchitectMode = ArchitectMode::DESIGNATING_STOCKPILE;
                            }
                            else {
                                auto dynamicGizmos = getAvailableGizmos(currentArchitectCategory);
                                if (!dynamicGizmos.empty() && architectGizmoSelection < dynamicGizmos.size()) {
                                    currentArchitectMode = ArchitectMode::DESIGNATING_BUILD; computeGlobalReachability(); buildableToPlace = dynamicGizmos[architectGizmoSelection].second;
                                }
                            }
                            isSelectingArchitectGizmo = false; currentTab = Tab::NONE;
                        }
                        else needsRedraw = false;
                    }
                    else {
                        if (wParam == VK_UP) { int cat = (int)currentArchitectCategory - 1; if (cat < 0) cat = static_cast<int>(ArchitectCategory::DECORATION); currentArchitectCategory = (ArchitectCategory)cat; }
                        else if (wParam == VK_DOWN) { currentArchitectCategory = (ArchitectCategory)(((int)currentArchitectCategory + 1) % (static_cast<int>(ArchitectCategory::DECORATION) + 1)); }
                        else if (wParam == 'Z' || wParam == VK_SPACE || wParam == VK_RETURN || wParam == VK_RIGHT) { isSelectingArchitectGizmo = true; architectGizmoSelection = 0; }
                        else { needsRedraw = false; }
                    }
                }
                else if (currentZ == BIOSPHERE_Z_LEVEL) {
                    if (wParam == VK_RETURN) {
                        int stockpileID = Z_LEVELS[currentZ][cursorY][cursorX].stockpileId;
                        if (stockpileID != -1) {
                            for (size_t i = 0; i < g_stockpiles.size(); ++i) if (g_stockpiles[i].id == stockpileID) {
                                inspectedStockpileIndex = static_cast<int>(i); stockpilePanel_selectedLineIndex = -1; stockpilePanel_scrollOffset = 0; break;
                            }
                        }
                        else {
                            for (size_t i = 0; i < colonists.size(); ++i) if (colonists[i].x == cursorX && colonists[i].y == cursorY) {
                                inspectedPawnIndex = static_cast<int>(i); currentPawnInfoTab = PawnInfoTab::OVERVIEW; followedPawnIndex = static_cast<int>(i); pawnInfo_scrollOffset = 0; break;
                            }
                        }
                    }
                    else if (wParam == 'D') { for (size_t i = 0; i < colonists.size(); ++i) if (colonists[i].x == cursorX && colonists[i].y == cursorY) colonists[i].isDrafted = !colonists[i].isDrafted; }
                    else if (wParam == 'Z') { for (auto& p : colonists) if (p.isDrafted) { p.targetX = cursorX; p.targetY = cursorY; } }
                    else if (wParam >= '1' && wParam <= '9') {
                        int pawnIndex = static_cast<int>(wParam) - '1';
                        if (pawnIndex >= 0 && pawnIndex < static_cast<int>(colonists.size())) {
                            cursorX = colonists[pawnIndex].x; cursorY = colonists[pawnIndex].y; currentZ = colonists[pawnIndex].z; followedPawnIndex = pawnIndex;
                        }
                    }
                    else { needsRedraw = false; }
                }
                else { needsRedraw = false; }
            }
            break;
        }
        default: needsRedraw = false; break;
        }
        if (needsRedraw) InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }

    default:
        return DefWindowProc(hwnd, uMsg, wParam, lParam);
    }
}


// --- Main Entry Point ---
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nShowCmd) {
    srand(static_cast<unsigned>(time(0)));
    initGameData();
    WNDCLASS wc = {}; wc.lpfnWndProc = window_callback; wc.hInstance = hInstance; wc.lpszClassName = L"ASCIIColonyManagement"; wc.hCursor = LoadCursor(nullptr, IDC_ARROW); wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.hbrBackground = NULL;
    if (!RegisterClass(&wc)) return -1;
    HWND window = CreateWindow(wc.lpszClassName, L"ASCII Colony Management", WS_OVERLAPPEDWINDOW | WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT, 1280, 780, nullptr, nullptr, hInstance, nullptr);
    if (window == nullptr) return -1;

    // Get a temporary HDC to initialize the font and its metrics once.
    HDC tempHdc = GetDC(window);
    UpdateDisplayFont(tempHdc); // Perform initial font setup
    ReleaseDC(window, tempHdc); // Release the temporary HDC

    DiscordRichPresence::init();

    double accumulator = 0.0;
    ULONGLONG lastFrameTime = GetTickCount64();

    while (running) {
        MSG message; while (PeekMessage(&message, nullptr, 0, 0, PM_REMOVE)) { if (message.message == WM_QUIT) running = false; TranslateMessage(&message); DispatchMessage(&message); }
        handleInput(window);
        ULONGLONG currentTime = GetTickCount64();
        double deltaTime = (currentTime - lastFrameTime) / 1000.0;
        lastFrameTime = currentTime;

        accumulator += deltaTime;

        // The Discord SDK functions are inside the namespace
        if (DiscordRichPresence::core) { // <-- CHANGE THIS
            DiscordRichPresence::core->RunCallbacks(); // <-- CHANGE THIS
            DiscordRichPresence::update(); // <-- CHANGE THIS
        }

        const double TIME_PER_UPDATE = 1.0 / 60.0; // Fixed update time step

        while (accumulator >= TIME_PER_UPDATE) {
            if (currentState == GameState::IN_GAME) {
                updateGame();
            }
            accumulator -= TIME_PER_UPDATE;
        }

        if (currentTime - lastFPSTime >= 1000) {
            fps = frameCount;
            frameCount = 0;
            lastFPSTime = currentTime;
        }

        InvalidateRect(window, nullptr, FALSE);
        frameCount++;

        // FPS Limiter
        ULONGLONG frameEndTime = GetTickCount64();
        DWORD frameDuration = (DWORD)(frameEndTime - currentTime);
        DWORD sleepDuration = 0;
        if (targetFPS > 0) {
            DWORD targetFrameTime = 1000 / targetFPS;
            if (frameDuration < targetFrameTime) {
                sleepDuration = targetFrameTime - frameDuration;
            }
        }
        if (sleepDuration > 0) {
            Sleep(sleepDuration);
        }
    }

    DiscordRichPresence::shutdown();

    return 0;
}
