/*
 * Game module -- see header file for more info
 */

#include <stdio.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <iostream>
#include <sstream>
#include <time.h>
#include <limits.h>

#include <vector>
#include <algorithm>

#include "ConfigFile.h"

#include "common.h"
#include "video.h"
#include "protocol.h"
#include "game.h"
#include "netcode.h"
#include "input.h"
#include "broadcaster.h"

namespace Game {

using namespace std;
using namespace Protocol;

Point<double> getSpawn(const char team);

static void getInput(string input);
string to_lower_case(string str);
bool file_exists(const char *filename);

int windowWidth = 800;
int windowHeight = 600;
bool fullscreen = false;

double gameWidth = 510;
double gameHeight = 510;
string path = "./";

ConfigFile *config = NULL;

GameState game;

//------------------------------------------------------------------------------

class Command
{
	public:
	typedef void (*Func)(const Message &);
	typedef map<string, pair<Func,size_t> > List;

	static List list;

	Command(string name, Func func, size_t count)
		{ list[to_lower_case(name)] = make_pair(func,count); }
};

Command::List Command::list;

#define CMD(name, count, arg, ...)                                   \
	void _ ## name(const Message &arg) { name(__VA_ARGS__); }        \
	Command _cmd_ ## name(#name, _ ## name, count);


//------------------------------------------------------------------------------

void KeyUp(Button btn)   { binds.processUp(btn); }
void KeyDown(Button btn) { binds.processDown(btn); }
//void MouseMove(word x, word y) {}

void Initialize(int argc, char *argv[])
{
	// Process command line arguments
	for (int i = 0; i < argc - 1; ++i)
	{
		if      (!strcmp(argv[i], "-x") || !strcmp(argv[i], "--map-width"))
			gameWidth = atof(argv[++i]);
		else if (!strcmp(argv[i], "-y") || !strcmp(argv[i], "--map-height"))
			gameHeight = atof(argv[++i]);
		else if (!strcmp(argv[i], "-p") || !strcmp(argv[i], "--path"))
			path = argv[++i];
		else if (!strcmp(argv[i], "-w") || !strcmp(argv[i], "--screen-width"))
			windowWidth = atoi(argv[++i]);
		else if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--screen-height"))
			windowHeight = atoi(argv[++i]);
	}
	
	for (int i = 0; i < argc; ++i)
	{
		if (!strcmp(argv[i], "--fullscreen"))
			fullscreen = true;
	}
	
	// Global subsystem initializations
	srand(time(NULL));
	
	// Create window and set up viewports
	game.window = new Video::Window(windowWidth, windowHeight,
	                                GAME_NAME, fullscreen);
	Video::Viewport *view = new Video::Viewport(1,1);
	game.window->viewports.push_back(view);
	
	// Show loading screen
	// Problem: we can't use the hud prior to asset loading
	
	// Load game assets
	Assets::Initialize(argc, argv);
	
	// Process config file
	if (!file_exists(CONFIG_FILE))
	{
		ConfigFile newconfig;
		newconfig.add("playername", "Unnamed");
		newconfig.add("team", 'a');
		newconfig.save(CONFIG_FILE);
	}
	
	try { config = new ConfigFile(CONFIG_FILE); }
	catch (ConfigFile::file_not_found e)
	{
		delete config;
		config = NULL;
		puts("Unable to create config file: " CONFIG_FILE "!");
		exit(EXIT_FAILURE);
	}
	
	config->readInto(NetCode::MessageOfTheDay, "motd",
		string("No message of the day ;("));
	
	// Set up game world
	game.root = World(gameWidth, gameHeight);
	game.world = TO(World,game.root);
	game.topId = 1;
	
	game.teams.insert(make_pair('a', Team('a')));
	game.teams.insert(make_pair('b', Team('b')));
	
	string name;
	unsigned char team;
	config->readInto(name, "playername", string("Unnamed"));
	team = config->read("team", 'a');
	
	Player::Id pid = game.topId++;
	ObjectHandle player = Player(pid, team, name);
	game.player = TO(Player,player);
	game.player->weapon = weapLaser;
	game.player->origin = getSpawn(team);
	game.root->children.insert(player);
	game.players[pid] = player;
	
	game.world->terrain->placeStructure(GridPoint(2,2), Mine());
	game.world->terrain->placeStructure(GridPoint(2,48), Mine());
	game.world->terrain->placeStructure(GridPoint(48,2), Mine());
	game.world->terrain->placeStructure(GridPoint(48,48), Mine());
	game.world->terrain->placeStructure(GridPoint(25,25), RichMine());
	game.world->terrain->placeStructure(GridPoint(15,25), Mine());
	game.world->terrain->placeStructure(GridPoint(35,25), Mine());
	game.world->terrain->placeStructure(GridPoint(25,5), RichMine());
	game.world->terrain->placeStructure(GridPoint(25,45), RichMine());
	game.world->terrain->placeStructure(GridPoint(6,9), Mine());
	game.world->terrain->placeStructure(GridPoint(6,41), Mine());
	game.world->terrain->placeStructure(GridPoint(44,9), Mine());
	game.world->terrain->placeStructure(GridPoint(44,41), Mine());
	game.world->terrain->placeStructure(GridPoint(10,17), Mine());
	game.world->terrain->placeStructure(GridPoint(10,33), Mine());
	game.world->terrain->placeStructure(GridPoint(40,17), Mine());
	game.world->terrain->placeStructure(GridPoint(40,33), Mine());

	ObjectHandle RedBot = player;
	ObjectHandle BlueBot = player;
	
	if (team == 'b')
	{
		RedBot = Player(INT_MAX - 'a', 'a', "RedBot", Pd(-1000,-1000,-1000));
		//game.world->children.insert(RedBot);
		game.players[TO(Player, RedBot)->id] = RedBot;
	}
	else if (team == 'a')
	{
		BlueBot = Player(INT_MAX - 'b', 'b', "BlueBot", Pd(-1000,-1000,-1000));
		//game.world->children.insert(BlueBot);
		game.players[TO(Player, BlueBot)->id] = BlueBot;
	}

	game.world->terrain->placeStructure(GridPoint(3,25), HeadQuarters(TO(Player, BlueBot)->id));
	game.world->terrain->placeStructure(GridPoint(48,26), HeadQuarters(TO(Player, RedBot)->id));

	game.world->terrain->placeStructure(GridPoint(4,26), DefenseTower(TO(Player, BlueBot)->id));
	game.world->terrain->placeStructure(GridPoint(4,23), DefenseTower(TO(Player, BlueBot)->id));
	game.world->terrain->placeStructure(GridPoint(46,27), DefenseTower(TO(Player, RedBot)->id));
	game.world->terrain->placeStructure(GridPoint(46,24), DefenseTower(TO(Player, RedBot)->id));

	for(int i = 0; i <= 50; i++) {
		game.world->terrain->placeStructure(GridPoint(0,i), Wall());
		game.world->terrain->placeStructure(GridPoint(50,i), Wall());
		game.world->terrain->placeStructure(GridPoint(i,0), Wall());
		game.world->terrain->placeStructure(GridPoint(i,50), Wall());
	}

	for(int i = 8; i <= 42; i++) {
		if (i < 23 || i > 27) {
			game.world->terrain->placeStructure(GridPoint(25,i), Wall());
			game.world->terrain->placeStructure(GridPoint(i,5), Wall());
			game.world->terrain->placeStructure(GridPoint(i,45), Wall());
		}
		if (i < 22 || i > 28) {
			game.world->terrain->placeStructure(GridPoint(i,15), Wall());
			game.world->terrain->placeStructure(GridPoint(i,35), Wall());
		}
		if (i < 13 || i > 37) {
			game.world->terrain->placeStructure(GridPoint(i,25), Wall());
		}
	}

	for (int i = 5; i <=11; i++) {
		game.world->terrain->placeStructure(GridPoint(4,i), Wall());
		game.world->terrain->placeStructure(GridPoint(46,i), Wall());
		game.world->terrain->placeStructure(GridPoint(i,11), Wall());
		game.world->terrain->placeStructure(GridPoint(i,39), Wall());
	}

	for (int i = 39; i <=45; i++) {
		game.world->terrain->placeStructure(GridPoint(4,i), Wall());
		game.world->terrain->placeStructure(GridPoint(46,i), Wall());
		game.world->terrain->placeStructure(GridPoint(i,11), Wall());
		game.world->terrain->placeStructure(GridPoint(i,39), Wall());
	}

	for (int i = 15; i <= 35; i++) {
		game.world->terrain->placeStructure(GridPoint(8,i), Wall());
		game.world->terrain->placeStructure(GridPoint(42,i), Wall());
	}

	for (int i = 3; i <= 47; i++) {
		if (i < 12 || i > 38 || (i > 19 && i < 31)) {
			game.world->terrain->placeStructure(GridPoint(18,i), Wall());
			game.world->terrain->placeStructure(GridPoint(32,i), Wall());
		}
	}

	for (int i = 13; i <= 37; i++) {
		if (i < 18 || i > 32) {
			game.world->terrain->placeStructure(GridPoint(i,20), Wall());
			game.world->terrain->placeStructure(GridPoint(i,30), Wall());
		}
	}

	game.world->terrain->placeStructure(GridPoint(11,1), Wall());
	game.world->terrain->placeStructure(GridPoint(11,2), Wall());
	game.world->terrain->placeStructure(GridPoint(11,49), Wall());
	game.world->terrain->placeStructure(GridPoint(11,48), Wall());
	game.world->terrain->placeStructure(GridPoint(39,1), Wall());
	game.world->terrain->placeStructure(GridPoint(39,2), Wall());
	game.world->terrain->placeStructure(GridPoint(39,49), Wall());
	game.world->terrain->placeStructure(GridPoint(39,48), Wall());

	// Set up user interface
	view->world = game.root;
	game.controller = new Controller(view->camera, player);
	game.input = new Input(*game.window);
	game.input->onKeyUp = KeyUp;
	game.input->onKeyDown = KeyDown;
	//input->onMouseMove = MouseMove;
	
	Echo("Everything loaded!");
	Echo("Welcome to the game");
	Echo(NetCode::MessageOfTheDay);
}

//------------------------------------------------------------------------------

#define CLEAN(x) if (x) delete x; x = NULL;

void Terminate()
{
	CLEAN(game.input);
	CLEAN(game.controller);
	
	game.player = NULL;
	game.world = NULL;
	game.root.clear();
	CLEAN(config)

	Video::Viewport *view = *game.window->viewports.begin();
	CLEAN(game.window)
	delete view;
	
	Assets::Terminate();
}

//------------------------------------------------------------------------------

void Call(string command)
{
	Protocol::Message args = command;
	if (args.size() < 1) return;

	string cmd = to_lower_case(args[0]);
	args.erase(args.begin());

	if (!Command::list.count(cmd))
		Echo(string("Unknown command: ") + cmd);
	else
	{
		pair<Command::Func,size_t> func = Command::list[cmd];
		if (args.size() < func.second)
			Echo(string("To few arguments: ") + cmd);
		else
			func.first(args);
	}
}

//------------------------------------------------------------------------------

bool Callable(std::string command)
{
	Protocol::Message args = command;
	if (args.size() < 1) return false;

	string cmd = to_lower_case(args[0]);

	return !!Command::list.count(cmd);
}

//------------------------------------------------------------------------------

string to_lower_case(string str)
{
	transform(str.begin(), str.end(), str.begin(), ::tolower);
	return str;
}

//------------------------------------------------------------------------------

Point<double> getSpawn(const char team){
	switch(team){
	case 'b':{
			//Pd spawn1 = game.world->terrain->ToPointD(GridPoint(5, 24));
			Pd spawn2 = game.world->terrain->ToPointD(GridPoint(5, 25));
			int randval = rand() % 2;
			if(randval == 0){
				return spawn2;
			}else{
				return spawn2;
			}
		 }
		 break;
	case 'a':{
			//Pd spawn1 = game.world->terrain->ToPointD(GridPoint(45,25));
		    Pd spawn2 = game.world->terrain->ToPointD(GridPoint(45,26));
			int randval = rand() % 2;
			if(randval == 0){
				return spawn2;
			}else{
				return spawn2;
			}
	
		 }
		 break;
	default:{
			return Pd();
		}
	}
}

//------------------------------------------------------------------------------

static void getInput(string input)
{
	HUD *hud = game.world->hud;

	if(!input.empty()) Game::Call(input);

	set<ObjectHandle>::iterator it;
	TextInput *tInput;
	for (it = hud->children.begin(); it != hud->children.end();)
	{
		tInput = TO(TextInput, *it);
		if (tInput)
		{
			hud->children.erase(it);
			break;
		}
		else
			++it;
	}
}

//------------------------------------------------------------------------------

bool file_exists(const char *filename)
{
	FILE *fp = fopen(filename, "rb");
	if (!fp) return false;
	fclose(fp);
	return true;
}

//==============================================================================

CMD(Echo, 1, arg, (string) arg[0])
void Echo(string msg)
{
	game.world->hud->messageDisplayer->addMessage(SystemMessage(msg));
}

//------------------------------------------------------------------------------

CMD(Notice, 1, arg, (string) arg[0])
void Notice(string msg)
{
	// Temporarily
	Echo(string("*** ") + msg + string(" ***"));
}

//------------------------------------------------------------------------------

CMD(Prompt, 0, arg, (arg.empty() ? "" : (string) arg[0]))
void Prompt(string cmd)
{
	int &width = game.world->hud->width;
	int &height = game.world->hud->height;
	game.input->text = cmd;
	game.input->onText = getInput; // Move this to init
	game.world->hud->children.insert(TextInput(game.input, 0, 0, width, height));
}

//------------------------------------------------------------------------------

CMD(ShowLog, 0, arg)
void ShowLog()
{
	game.world->hud->messageDisplayer->setShowAlways(true);
}

//------------------------------------------------------------------------------

CMD(HideLog, 0, arg)
void HideLog()
{
	game.world->hud->messageDisplayer->setShowAlways(false);
}

//------------------------------------------------------------------------------

CMD(Quit, 0, arg)
void Quit()
{
	Video::StopEventLoop();
}

//------------------------------------------------------------------------------

CMD(RQuit, 0, arg, (arg.size() < 1 ? "AAAAaaaRRGG!!" : arg[0]))
void RQuit(string msg)
{
	Say(msg);

	// Let player explode

	Quit();
}

//------------------------------------------------------------------------------

CMD(Bind, 2, arg, (string) arg[0], (string) arg[1])
void Bind(string button, string line)
{
	binds.bind(button, line);
}

//------------------------------------------------------------------------------

CMD(Exec, 1, arg, (string) arg[0])
void Exec(string filename)
{
	FILE *fp = fopen((path+filename).c_str(), "rt");
	if (!fp)
	{
		Echo(string("Unable to open script file: " + filename));
		return;
	}

	Echo(string("Executing ") + filename + string("..."));

	char buffer[1024], *ptr;
	while(fgets(buffer, sizeof (buffer), fp))
	{
		ptr = buffer;
		while ((*ptr == ' ') || (*ptr == '\t')) ptr++;
		if (!*ptr || (*ptr == '#')) continue;

		int len = strlen(ptr) - 1;
		if (ptr[len] == '\n') ptr[len--] = 0;
		if (ptr[len] == '\r') ptr[len--] = 0;

		if (len >= 0) Call(string(ptr));
	}
	fclose(fp);
}

//------------------------------------------------------------------------------

CMD(Get, 1, arg, (string) arg[0])
void Get(string key)
{
	if (!config) return;
	string value;
	config->readInto(value, key, string("Null"));
	Echo(key + string(" = ") + value);
}

//------------------------------------------------------------------------------

CMD(Set, 2, arg, (string) arg[0], (string) arg[1])
void Set(string key, string value)
{
	if (!config) return;
	config->add(key, value);
	config->save(CONFIG_FILE);
}

//------------------------------------------------------------------------------

CMD(GrabMouse, 0, arg)
void GrabMouse()
{
	game.input->grabMouse();
}

//------------------------------------------------------------------------------

CMD(ReleaseMouse, 0, arg)
void ReleaseMouse()
{
	game.input->releaseMouse();
}

//------------------------------------------------------------------------------

CMD(Connect, 1, arg, (string) arg[0])
void Connect(string address)
{
	Game::Notice(string("Connecting to ") + address + string("..."));
	if (!NetCode::Connect(address))
		Game::Notice(string("Unable to connect to " + address + string("!")));
	else
	{
		Game::Notice(string("Connected to " + address + string("!")));		
		game.connecting = true;
	}
}

//------------------------------------------------------------------------------

CMD(Disconnect, 0, arg)
void Disconnect()
{
	NetCode::Disconnect();
}

//------------------------------------------------------------------------------

CMD(Say, 1, arg, (string) arg[0])
void Say(string msg)
{
	NetCode::Chat(msg);
	DisplayChatMsg(game.player, msg);
}

//------------------------------------------------------------------------------

CMD(TeamSay, 1, arg, (string) arg[0])
void TeamSay(string msg)
{
	NetCode::TeamChat(msg);
	DisplayTeamMsg(game.player, msg);
}

//------------------------------------------------------------------------------

CMD(MoveX, 1, arg, (double) arg[0])
void MoveX(double speed)
{
	game.controller->move[dirX] = speed;
	//game.controller->moveX(speed);
}

//------------------------------------------------------------------------------

CMD(MoveY, 1, arg, (double) arg[0])
void MoveY(double speed)
{
	game.controller->move[dirY] = speed;
	game.controller->moveY(speed);
}

//------------------------------------------------------------------------------

CMD(MoveZ, 1, arg, (double) arg[0])
void MoveZ(double speed)
{
	game.controller->move[dirZ] = speed;
	game.controller->moveZ(speed);
}

//------------------------------------------------------------------------------

CMD(LookX, 1, arg, (double) arg[0])
void LookX(double speed)
{
	game.controller->look[dirX] = speed;
	game.controller->lookX(speed);
}

//------------------------------------------------------------------------------

CMD(LookY, 1, arg, (double) arg[0])
void LookY(double speed)
{
	game.controller->look[dirY] = speed;
	game.controller->lookY(speed);
}

//------------------------------------------------------------------------------

CMD(LookZ, 1, arg, (double) arg[0])
void LookZ(double speed)
{
	game.controller->look[dirZ] = speed;
	game.controller->lookZ(speed);
}

//------------------------------------------------------------------------------

CMD(Jump, 0, arg)
void Jump()
{
}

//------------------------------------------------------------------------------

CMD(Fire, 0, arg)
void Fire()
{
	if (!game.input->grabbing) return; // Ignore when not selected
	if (!NetCode::TryLock())
	{
		game.firing = true;
		return;
	}
	game.firing = false;
	switch (game.player->weapon)
	{
		case weapWrench:
			Build();
			return;
		case weapLaser:
		{
		        Camera &cam = game.controller->camera;
			Vd vec = ~(game.player->rotation * Vd(0,1,0));
			double yaw = atan2(vec.x, vec.y);
			Pd gunLoc = game.player->origin;// + game.player->model.weapon->origin;
			gunLoc.x = gunLoc.x + game.player->model.weapon->origin.x * cos(yaw)
			                    + game.player->model.weapon->origin.y * sin(yaw);
			gunLoc.y = gunLoc.y + game.player->model.weapon->origin.x * sin(yaw)
			                    + game.player->model.weapon->origin.y * cos(yaw);
			gunLoc.z = gunLoc.z + game.player->model.weapon->origin.z;
			
			Vd lookVec = (~(Vd(game.controller->target)+ -Vd(cam.origin))) * 38;
			Pd target = game.controller->target;
			ObjectHandle collision;
			if(game.controller->firstPerson){
				lookVec = (~(game.controller->camAngle * Vd(0,1,0)))*38;
				collision = game.world->trace(game.controller->camera.origin, lookVec, game.player);
			}else{
				collision = game.world->trace(game.controller->target, lookVec, game.player);
			}
			if (collision)
			{	
				Pd collisionPoint;
				if(game.controller->firstPerson){
					collisionPoint = game.controller->camera.origin + (lookVec);
				}else{
					collisionPoint = game.controller->target + (lookVec);
				}
				Qd beam = gunLoc.lookAt(collisionPoint);
				
				ObjectHandle laser = LaserBeam(gunLoc, beam, !lookVec);
				game.world->addLaserBeam(laser);
				NetCode::Fire(*TO(LaserBeam,laser));
				Player *p = TO(Player, collision);
				if(p){
					//if(p->team != game.player->team){//Precent teamkill
						p->damage(10.0, game.player->id);
						NetCode::Hit(p->id, 10.0, true);
					//}
				}else{
					Building *b = TO(Building, collision);
					if(b){
						/* Enable team kill on towers to demolish
						Player *own = NULL;
						if(Game::game.players.count(t->owner))
							own = TO(Player, Game::game.players[t->owner]); 
						if(own && own->team != game.player->team){*/
							b->damage(10.0, game.player->id);
							NetCode::Attack(b->loc, 10.0, true);
						//}
					}
				}
			}
			else{
				game.world->addLaserBeam(ObjectHandle(LaserBeam(gunLoc, cam.objective, !lookVec)));
			}
			return;
		}
		break;
	}
}

//------------------------------------------------------------------------------

CMD(Build, 0, arg)
void Build()
{
	Camera &cam = game.controller->camera;
	GridPoint clicked = game.world->terrain->getGridCoordinates(cam.origin, cam.objective);
	if(clicked.isValid())
	{
		#ifdef AUTOBUILD
		int structure = game.world->terrain->canPlaceStructure(clicked);
		ObjectHandle tower;
		switch(structure){
		case 1: case 11: tower = Objects::DefenseTower(game.player->id); break;
		case 2: case 12: tower = Objects::ResourceMine(game.player->id); break;
		case 3: case 13: tower = Objects::RichResourceMine(game.player->id); break;
		default: tower = ObjectHandle();
		}
		Resource cost = 0;
		if(tower){
			Building *b = TO(Building, tower);
			if(b) cost = b->cost;
		}
		map<unsigned char,Team>::iterator it = Game::game.teams.find(Game::game.player->team);
		if(it != Game::game.teams.end()){
			if(it->second.resources >= cost){
				game.world->terrain->setSelected(GridPoint(-1, -1));
				if (!game.world->terrain->placeStructure(clicked, tower)){
					Echo("There's already a tower there");
				}else{
					NetCode::Build(clicked,TO(Structure,tower));
					it->second.resources -= cost;
				}
			}else{
				Echo("You don't have enough money");
			}
		}
		#endif
	}
	else
		Echo("Invalid place to build");
}

//------------------------------------------------------------------------------

CMD(Weapon, 1, arg, (WeaponType) (int) arg[0])
void Weapon(WeaponType weapon)
{
	WeaponType prevWeapon = game.player->weapon;
	Terrain *terrain = TO(Terrain, game.world->terrain);
	if (prevWeapon == weapon) return;

	game.player->weapon = weapon;
	if (weapon == weapWrench)
	{
		// Goto build mode
		terrain->showGrid = true;
		#ifndef AUTOBUILD
		game.world->hud->buildselector->show = true;
		#endif
		//game.controller->setView(true);
	}
	else if (prevWeapon == weapWrench)
	{
		// Leave build mode
		terrain->showGrid = false;
		#ifndef AUTOBUILD
		game.world->hud->buildselector->show = false;
		#endif
		game.world->terrain->setSelected(GridPoint(-1, -1));
		//game.controller->restoreView();
	}
}

//------------------------------------------------------------------------------

CMD(Tool, 1, arg, (ToolType) (int) arg[0])
void Tool(ToolType tool)
{
	ToolType prevTool = game.player->tool;
	if (prevTool == tool) return;

	game.player->tool = tool;
}

//------------------------------------------------------------------------------

CMD(Goto, 1, arg, (string) arg[0])
void Goto(string cmd)
{
	if (cmd == "gold")
		game.teams[game.player->team].resources += 1000.0;
}
//------------------------------------------------------------------------------

CMD(Join, 1, arg, (int) arg[0])
void Join(int id)
{
	if(id){
		string ip = Broadcaster::getIP(id - 1);
		if(ip.length()){
			Connect(ip);
		}
	}
}

//------------------------------------------------------------------------------

CMD(Discover, 0, arg)
void Discover()
{
	stringstream result(Broadcaster::getNeighbours());
	string line;
	Echo("The following players are located in your subnet:");
	while(getline(result, line)){
		Echo(line);
	}
}

//------------------------------------------------------------------------------

void DisplayChatMsg(Player *player, string line)
{
	if (!player) return;

	game.world->hud->messageDisplayer->addMessage(ChatMessage(*player, line));
}

//------------------------------------------------------------------------------

void DisplayTeamMsg(Player *player, string line)
{
	if (!player) return;

	game.world->hud->messageDisplayer->addMessage(ChatMessage(*player, line));
}

//------------------------------------------------------------------------------

void DisplayFragMsg(Player *player, Player *victim)
{
	if (!player) return;
	if (!victim)
		game.world->hud->messageDisplayer->addMessage(TowerFragMessage(*player));
	else
		game.world->hud->messageDisplayer->addMessage(PlayerFragMessage(*player, *victim));	
}

//------------------------------------------------------------------------------

void DisplayJoinMsg(Player *player)
{
	if (!player) return;

	game.world->hud->messageDisplayer->addMessage(PlayerJoinMessage(*player));
}

//------------------------------------------------------------------------------

void DisplayPartMsg(Player *player)
{
	if (!player) return;

	game.world->hud->messageDisplayer->addMessage(PlayerLeaveMessage(*player));
}

//------------------------------------------------------------------------------

void GameEnd(unsigned char team)
{
	ShowLog();
	if (team == 'a')
		Notice("The Alliance of Free Systems (Team Red) is victorious!");
	else if (team == 'b')
		Notice("The Galactic Empire (Team Blue) is victorious!");
	
	game.playing = false;
}

//------------------------------------------------------------------------------

CMD(PrintFPS, 0, arg)
void PrintFPS()
{
    Echo(string("Current FPS: ") + Argument((double) CurrentFPS()).str);
}

//------------------------------------------------------------------------------

CMD(PrintCPS, 0, arg)
void PrintCPS()
{
    Echo(string("Current CPS: ") + Argument((double) NetCode::CurrentCPS()).str);
}

//------------------------------------------------------------------------------

CMD(GameSpeed, 1, arg, (double) arg[0]);
void GameSpeed(double speed)
{
	Video::GameSpeed = speed;
}

//------------------------------------------------------------------------------

CMD(NetDebug, 0, arg)
void NetDebug()
{
	NetCode::Debug();
}

//------------------------------------------------------------------------------

CMD(PrintWorld, 0, arg)
void PrintWorld()
{
	Object::iterator it;
	for (it = game.world->begin(); it != game.world->end(); ++it)
		puts((string(it.level(),'\t') + string(**it)).c_str());
}

//------------------------------------------------------------------------------

CMD(PrintPlayers, 0, arg)
void PrintPlayers()
{
	Player *p;
	map<Player::Id,ObjectHandle>::iterator it;
	for (it = game.players.begin(); it != game.players.end(); ++it)
	{
		if (!(p = TO(Player,it->second))) continue;
		printf("[%d] %c %s\n", p->id, p->team, p->name.c_str());
	}
}

//------------------------------------------------------------------------------

CMD(Test, 1, arg, arg[0])
void Test(string str)
{
	if(str == "a"){
		game.world->children.insert(Droppable(Pd(0.0, 0.0, 0.0), 30));
	}else if(str == "b"){
		game.world->temporary.push_back(Droppable(Pd(0.0, 0.0, 0.0), 30));
	}else{
		Player::Id pid2 = game.topId++;
		ObjectHandle player2 = Player(pid2, 'a', "Carl");
		game.root->children.insert(player2);
		game.players[pid2] = player2;

		if(game.teams.count('a'))
			game.teams['a'].resources = 1000;

		game.world->terrain->placeStructure(GridPoint(22,24), DefenseTower(pid2));
	}

}

//==============================================================================

void Chain(string line)
{
	size_t pos;
	while ((pos = line.find(';')) != string::npos)
	{
		size_t pos2 = pos;
		while (line[--pos]  == ' ');
		while (line[++pos2] == ' ');
		Call(line.substr(0, pos + 1));
		line = line.substr(pos2);
	}
	Call(line);
}
CMD(Chain, 1, arg, (string) arg[0])

//------------------------------------------------------------------------------

struct Toggler
{
	vector<string> cmd;
	int last;
	Toggler() : last(0) {}
};

map<string,Toggler> toggles;

void Toggle(string line)
{
	if (!toggles.count(line))
	{
		Toggler &toggler = toggles[line];

		size_t pos;
		while ((pos = line.find('^')) != string::npos)
		{
			size_t pos2 = pos;
			while (line[--pos]  == ' ');
			while (line[++pos2] == ' ');
			toggler.cmd.push_back(line.substr(0, pos + 1));
			line = line.substr(pos2);
		}
		toggler.cmd.push_back(line);

		Call(toggler.cmd[toggler.last++]);
		toggler.last %= toggler.cmd.size();
		return;
	}

	Toggler &toggler = toggles[line];
	Call(toggler.cmd[toggler.last++]);
	toggler.last %= toggler.cmd.size();
}
CMD(Toggle, 1, arg, (string) arg[0])

//------------------------------------------------------------------------------

} // namespace Game

//------------------------------------------------------------------------------
