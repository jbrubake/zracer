/*
 * Remigiusz Jan Andrzej Modrzejeski, http://lrem.net/ <lrem at go2.pl>
 * Distributed under the GPL, for more details see: 
 * http://lrem.net/zracer.xhtml
 * 
 * ZRacer - a simple arcade game in ncurses
 *
 * ZRacer is a racing game where 1 - 2 players race on a randomly
 * generated racecourse with split-screen and using the same keyboard.
 * 
 * Conventions taken:
 * 	- coordinates order is (y, x)
 * 	- (0, 0) is upper left corner of everything
 * 	- as a result, finish line is at line 0
 * 	- car doesn't take up the whole rectangle (for collision checking)
 * 	- when a car crashes, it's window is frozen and no input is taken
 * 	- the track is stored as its ascii-art representation
 */

#include <curses.h>
#include <cstdarg>
#include <cstdio>
#include <string>
#include <vector>
#include <ctime>
#include <cassert>
#include <cstdlib>

using namespace std;

// This one defines a random function with results in range 0..1 (double).
#define drand()((double)rand()/RAND_MAX)

// Various constants
#define MAX_CAR_SIZE 20
#define MAX_PLAYERS 2
#define INF 123456789
#define KEY_ESC 27 // Missing in ncurses...
#define RESULTS_COLORS 11
#define MESSAGE_LENGTH 100

// Action values
#define ACCELERATE -1
#define BRAKE 1
#define LEFT -1
#define RIGHT 1

// Make passage, but don't exceed available space.
#define MINIMAL_WIDTH\
	(min(settings.players*settings.car_size*2.5, (double)settings.race_width-2))

// Main menu item defines, for convenience.
#define MENU_QUIT 0
#define MENU_START 1
#define MENU_OPTIONS 2

/*
 * In-game settings. This needn't really by a struct, but it looks more
 * readable to access settings by "settings.delay" than "delay".
 */

struct _settings
{
	// Basic game delay, is not equal to move time, but is a factor.
	timespec delay;
	// The axis of splitscreen.
	bool vertical_split;
	// Whether both players race on the same-looking racecourse.
	bool similar_track;
	// Or maybe literally the same one? (Collisions possible)
	bool shared_track;
	// The sizes of the course, better make it bigger than the car ;)
	int race_length, race_width;
	// The minimal width of the road the players can drive.
	int minimal_width;
	// The number of participants.
	int players;
	// Character with which the cars are drawn.
	char character;
	// The size of the car.
	int car_size;
	// The distance interval at which the speed of the car changes.
	int speed_base;
	// The chance of generating a rock on a given line
	double rock_chance;
	// The chance of generating a turning on a given line
	double turn_chance;
	// Keys players use to interact with the game.
	int controls[MAX_PLAYERS][4];

	void reset(void)
	{
		delay.tv_sec = 0;
		// Hundredth of a second * const.
		delay.tv_nsec = 1000000*25;
		similar_track = vertical_split = true;
		shared_track = true;
		race_length = 500;
		// Zero makes these 2 variables adjusted to screen size.
		race_width = 0;
		minimal_width = 0;
		players = 1;
		character = '^';
		car_size = 10;
		speed_base = 5;
		rock_chance = 0.025;
		turn_chance = 0.125;
		
		// Arrow keys for first player
		controls[0][0]=KEY_UP;
		controls[0][1]=KEY_DOWN;
		controls[0][2]=KEY_LEFT;
		controls[0][3]=KEY_RIGHT;
		// WSAD for second
		controls[1][0]='w';
		controls[1][1]='s';
		controls[1][2]='a';
		controls[1][3]='d';
	}

	void editor(void);
	void _edit_players(void);
	void _edit_length(void);
	void _edit_width(void);
	void _edit_rocks(void);
	void _edit_turns(void);
	void _edit_delay(void);
	void _edit_sharing(void);
} settings;

class car_image
{
	/*
	 * This is where we store the image of the car.
	 * For performance purposes, it is created only once when scaled.
	 */
	bool storage [MAX_CAR_SIZE+1][MAX_CAR_SIZE+1];
	// And as list of used pixels coords.
	vector<pair<int, int> > dots;
	char character;
	int color, size;

	/*
	 * Internal functions. First one is _clear() - it just sets all
	 * the storage area to false. Second one is line() - takes coords
	 * of 2 points and draws a line between them (or more literally
	 * just marks certain values within storage as true).
	 */
	void _clear(void);
	void _line(int, int, int, int);
	
	public:
	/*
	 * Constructor, takes input from the settings.
	 */
	car_image(void);
	/*
	 * This one simply draws the car on the given window. It assumes 
	 * that this can clearly be done, so all the checks need to be
	 * done earlier. The parameters it takes are the window and position
	 * of upper left corner of the car.
	 */
	void display(WINDOW*, int, int);
	/*
	 * Draws the explosion of the car. Parameters are windows, position
	 * of upper left corner of the car. Position is relative to the window, 
	 * _not_ the track.
	 */
	void explode(WINDOW*, int, int);
	/*
	 * Collision happens only when an obstacle is on a pace taken by the
	 * car. It is possible to have the obstacle between the car's "ribs".
	 * This checks whether given pace relative to *car's position* is
	 * taken by the car.
	 */
	bool collision_check(int, int);
	/*
	 * These are simple mutators.
	 */
	void set_character(char);
	void set_color(int);
	
	// And a simple accessor.
	vector<pair<int, int> > get_dots(void);
};

class track
{
	/*
	 * These are the most important data for the game. The track should
	 * be generated only once each game, preferably shared between players.
	 */
	vector<vector<char> > circuit;

	public:
	/*
	 * Two constructors. One takes input from the settings, second just 
	 * copies another track.
	 */
	track(void);
	track(track*);
	/*
	 * Takes the number of the top line to display and the window.
	 * Window's height and width are grabbed by getmaxyx().
	 * There's an assertion that the window is wide enough.
	 */
	void display(WINDOW*, int);
	/*
	 * Tells whether there's an obstacle at a given pace.
	 */
	bool taken(int, int);

	void mark(int, int, car_image*);
	void unmark(int, int, car_image*);
};

class player_handler
{
	WINDOW* screen;
	track* course;
	car_image* car;
	// last_move is the time value of the previous player's action
	// (y, x) is the position of the upper left corner of the car
	// top_line is the top displayed line of the course
	// commands store what player clicked
	int last_move, y, x, top_line, command_x, command_y, screen_height;
	int controls[4];

	public:
	/*
	 * This constructor prepares the part of the screen for the player.
	 * It decides which part of screen to take, and the color of the car,
	 * taking the settings and the player number. The track is generated
	 * outside it.
	 */
	player_handler(int, track*);
	/*
	 * Player's main loop. Returns true if the player continues to play,
	 * and false if the game ends for him. Also does redraw the window.
	 */
	bool tick(int);
	/*
	 * Knowing the player's key controls, this one does what it's named for.
	 * Only sets the action to perform during next move.
	 */
	void parse_input(int);
	/*
	 * These are used only in shared track races in order to catch player-player
	 * collisions.
	 */
	void mark_position(void);
	void unmark_position(void);
	/*
	 * Apart from deallocating standard structures, this one has also to
	 * destroy the ncurses window it created, so it needs a separate destructor.
	 */
	//~player_handler(void);
};

class game
{
	int time;
	player_handler* players[MAX_PLAYERS];
	bool alive[MAX_PLAYERS];
	
	public:
		/*
		 * Constructor does all the fancy things like initializing ncurses,
		 * while destructor brings back normal tty behaviour. This is great,
		 * as it doesn't force me to remember about deinitialization any
		 * time I want to break the game.
		 */
		game(void);
		~game(void);
		/*
		 * This is the game's main loop action...
		 * It returns true as long as game continues.
		 */
		bool tick(void);
};

int main_menu (void);
// This is a wrapper around printw, also accepts arbitrary number of arguments
void message (char*, ...);

int main (void)
{
	bool keep_asking = true;

	settings.reset();
	
	while(keep_asking)
	{
		switch(main_menu())
		{
			case MENU_START:
				{
					game race;
					while(race.tick())
						nanosleep(&settings.delay, NULL);
					break;
				}
			case MENU_OPTIONS:
				settings.editor();
				break;
			case MENU_QUIT:
				keep_asking = false;
		}
	}

	// In C a "return 0;" would come here, but this is not C...
}

/*
 * This function opens a window with a message disregarding anything else that was
 * running. Good for displaying error messages, final results and so. Waits for an
 * ESC pressed before quiting.
 */
void message (char* format_string, ...)
{
	va_list args;
	// Allocate a buffer for the message.
	char* final_string = new char[MESSAGE_LENGTH];
	
	// Fetch the "..." arguments.
	va_start(args, format_string);
	// Transform all the arguments into a single string.
	vsprintf(final_string, format_string, args);
	// Finalize the work on the "..." arguments.
	va_end(args);
	
	// Get screen resolution.
	int screen_height, screen_width;
	getmaxyx(stdscr, screen_height, screen_width);

	// Create a window for the message, of it's size 
	WINDOW* message_win = newwin(1, strlen(final_string),
			// and at the centre of the screen. 
			screen_height/2, screen_width/2 - strlen(final_string)/2);
	
	// Print the message.
	wattron(message_win, COLOR_PAIR(RESULTS_COLORS));
	wattron(message_win, A_BOLD);
	waddstr(message_win, final_string);
	wattroff(message_win, COLOR_PAIR(RESULTS_COLORS));
	wattroff(message_win, A_BOLD);
	wrefresh(message_win);
	
	// Wait for an ESC.
	while(getch()!=KEY_ESC)
		nanosleep(&settings.delay, NULL);

	// Clean up after myself.
	delwin(message_win);
}

/*
 * This class is created solely for the cool trick used in game constructor/destructor.
 * It's struct just because it doesn't have anything private.
 * Watch it's ingenuity and simplicity!
 */
struct simple_curses
{
	simple_curses(void)
	{
		// Initialize ncurses. This is done separately from initializing for
		// the game in order to have different options. 
		initscr();
		cbreak();
		clear();
	}

	~simple_curses(void)
	{
		// Fall back to normal options
		nocbreak();
		endwin();
	}
};

int main_menu (void)
{
	// Whole trick is: constructor get's run here, destructor whenever leaving
	// the function.
	simple_curses enviroment;

	// Tell them what to do...
	printw("\t\tWelcome to ZRacer by lRem!\n");
	printw("If you like this game look for more at http://lrem.net/\n\n");
	printw("\t\t\tMAIN MENU:\n\n");
	printw("q) Quit the game.\n");
	printw("s) Start a new game.\n");
	printw("o) Options.\n\n");
	
	// And wait for them do it.
	char pressed = 0;
	for(;;)
	{
		printw("Choose any option: ");
		refresh();
		pressed = getch();
		switch(pressed)
		{
			case 'q':
				return MENU_QUIT;
				// No breaks - return already quits the switch.
			case 's':
				return MENU_START;
			case 'o':
				return MENU_OPTIONS;
		}
		// In case user missed the key, print the next request in next line.
		addch('\n');
	}
}

void _settings::editor(void)
{
	simple_curses enviroment;

	printw("\t\tSETTINGS EDITOR\n");
	printw("q) Quit the editor\n");
	printw("p) Set the number of players\n");
	printw("l) Set the length of the racecourse\n");
	printw("r) Set the chance to generate a rock\n");
	printw("t) Set the chance to generate a turning\n");
	printw("s) Set master delay\n");
	printw("h) Set track sharing\n");
	// This doesn't give nice results...
	//printw("w) Set the width of the racecourse\n");
	char pressed = 0;
	for(;;)
	{
		printw("Choose any option: ");
		refresh();
		pressed = getch();
		switch(pressed)
		{	
			case 'q':
				return;
			case 'p':
				_edit_players();
				break;
			case 'l':
				_edit_length();
				break;
			case 'r':
				_edit_rocks();
				break;
			case 't':
				_edit_turns();
				break;
			case 's':
				_edit_delay();
				break;
			case 'h':
				_edit_sharing();
			//case 'w':
			//	_edit_width();
			//	break;
		}
	}
	
}

void _settings::_edit_players(void)
{
	printw("\n\tSelect the number of players (1 - %d, currently %d):", MAX_PLAYERS, players);
	players = 0;
	// While players outside the possible range.
	for(; players<1 || MAX_PLAYERS<players;)
	{
		players = getch() - '0';
		addch(' ');
	}
	addch('\n');
}

void _settings::_edit_length(void)
{
	printw("\n\tSet the length of the track (arbitrary, currently %d):", race_length);
	race_length = -1;
	// While players outside the possible range.
	for(; race_length<0;)
	{
		scanw("%d", &race_length);
	}
}

void _settings::_edit_width(void)
{
	printw("\n\tSet the width of the track (narrower than terminal, 0 means max, currently %d):", race_width);
	race_width = -1;
	// While players outside the possible range.
	for(; race_width<0;)
	{
		scanw("%d", &race_width);
	}
}

void _settings::_edit_rocks(void)
{
	printw("\n\tSet the chance of generating a rock (0-1, currently %lf):", rock_chance);
	rock_chance = -1;
	for(; rock_chance<0 || 1<rock_chance;)
	{
		scanw("%lf", &rock_chance);
	}
}

void _settings::_edit_turns(void)
{
	printw("\n\tSet the chance of generating a turn (0-1, currently %lf):", turn_chance);
	turn_chance = -1;
	for(; turn_chance<0 || 1<turn_chance;)
	{
		scanw("%lf", &turn_chance);
	}
}

void _settings::_edit_delay(void)
{
	printw("\n\tSet the master delay (positive, nanosceonds, currently %d):", delay.tv_nsec);
	delay.tv_nsec = -1;
	for(; delay.tv_nsec<0;)
	{
		scanw("%d", &delay.tv_nsec);
	}
}

void _settings::_edit_sharing(void)
{
	printw("\n\tShould the track be Similar, sHared or Different for different palyers? ");
	char response;
	for(; response!='s' && response!='h' && response!='d';)
	{
		scanw("%c", &response);
		switch(tolower(response))
		{
			case 's':
				similar_track = true;
				shared_track = false;
				break;
			case 'h':
				similar_track = true;
				shared_track = true;
				break;
			case 'd':
				similar_track = false;
				shared_track = false;
				break;
		}
	}
}

bool game::tick(void)
{
	bool game_continues = false;
	time++;

	// For every key waiting in buffer...
	int pressed_key;
	while((pressed_key = getch()) != ERR)
	{
		if(pressed_key == KEY_ESC) // End the game.
			for(int i = 0; i<settings.players; i++)
				alive[i]=false; // By killing all players.

		// Pass the input to each player.
		for(int i = 0; i<settings.players; i++)
			players[i]->parse_input(pressed_key);
	}

	// Checking for player-player collisions is realized by marking each player's position
	// as an obstacle on the track.
	if(settings.shared_track)
		for(int i=0; i<settings.players; i++)
			if(alive[i])
				players[i]->mark_position();

	for(int i=0; i<settings.players; i++)
		if(alive[i])
			if(settings.shared_track)
			{
				players[i]->unmark_position();
				game_continues = (alive[i] = players[i]->tick(time)) || game_continues;
				players[i]->mark_position();
			}
			else
				// If the player dies it sets his alive status to false.
				// If he lives, then there is a reason to continue the game.
				game_continues = (alive[i] = players[i]->tick(time)) || game_continues;

	if(settings.shared_track)
		for(int i=0; i<settings.players; i++)
			players[i]->unmark_position();

	
	// Results.
	if(!game_continues)
		message("Game finished after %d turns.", time);
	
	return game_continues;
}

game::game (void)
{
	// Initialize the RNG
	// We have already a variable called "time", and we need a function of the same name.
	// In order to circumvent this problem, we use the namespaces.
	srand(std::time(NULL));
	
	// Initialize ncurses.
	initscr();
	// Initialize colors (refuse to start without them).
	assert(has_colors());
	start_color();
	keypad(stdscr, TRUE);
	cbreak();
	noecho();
	nonl();
	nodelay(stdscr, true);

	// Color palette.
	init_pair(COLOR_BLACK, COLOR_BLACK, COLOR_BLACK);
	init_pair(COLOR_RED, COLOR_RED, COLOR_BLACK);
	init_pair(COLOR_GREEN, COLOR_GREEN, COLOR_BLACK);
	init_pair(COLOR_YELLOW, COLOR_YELLOW, COLOR_BLACK);
	init_pair(COLOR_BLUE, COLOR_BLUE, COLOR_BLACK);
	init_pair(COLOR_MAGENTA, COLOR_MAGENTA, COLOR_BLACK);
	init_pair(COLOR_CYAN, COLOR_CYAN, COLOR_BLACK);
	init_pair(COLOR_WHITE, COLOR_WHITE, COLOR_BLACK);
	init_pair(RESULTS_COLORS, COLOR_YELLOW, COLOR_BLUE);
	
	// Adjust settings, if needed.
	if(settings.race_width == 0)
	{
		int y, x;
		getmaxyx(stdscr, y, x);
		settings.race_width = settings.vertical_split ? x/settings.players : x;
	}
	if(settings.minimal_width == 0)
		settings.minimal_width = (int)(MINIMAL_WIDTH);
	
	// Prepare players
	if(settings.shared_track)
	{
		track* course = new track();
		for(int i = 0; i<settings.players; i++)
			players[i]=new player_handler(i, course);
	}
	else
		if(settings.similar_track)
		{
			track* course = new track();
			for(int i = 0; i<settings.players; i++)
				players[i]=new player_handler(i, course);
		}
		else
		{
			for(int i = 0; i<settings.players; i++)
				players[i]=new player_handler(i, new track());
		}
	for(int i = 0; i<settings.players; i++)
		alive[i]=true;

	// Let the moves begin.
	time = 0;
}

game::~game (void)
{
	echo();
	nl();
	nocbreak();
	nodelay(stdscr, false);
	endwin();
}

track::track (void)
{
	// Allocate the structures.
	circuit.resize(settings.race_length);
	for(vector<vector<char> >::iterator it = circuit.begin(); it!=circuit.end(); it++)
		it->resize(settings.race_width);

	// And create the course!
	int borders[2];
	int borders_directions[2]={0,0};

	// Initially make the road halfway between minimal and maximal possible.
	assert(settings.minimal_width <= settings.race_width);
	borders[0] = (settings.race_width - settings.minimal_width)/4;
	borders[1] = (settings.race_width*3 + settings.minimal_width)/4;

	// And generate the lines.
	for(int i=settings.race_length-1; 0<=i; i--)
	{
		// Put some background.
		for(int j=0; j<(int)circuit[i].size(); j++)
			circuit[i][j]=' ';
		// Distance meter.
		circuit[i][0]='0'+i%10;
		// Occasional rock on the track :>
		if(drand()<settings.rock_chance)
			circuit[i][rand()%settings.race_width]='*';
		// Move the kerbs.
		borders[0]+=borders_directions[0];
		borders[1]+=borders_directions[1];
		// Draw the kerbs.
		switch(borders_directions[0])
		{// Different chars, depending on the kerb direction.
			case 0:
				circuit[i][borders[0]]='|';
				break;
			case 1:
				circuit[i][borders[0]]='/';
				break;
			case -1:
				circuit[i][borders[0]]='\\';
		}
		switch(borders_directions[1])
		{
			case 0:
				circuit[i][borders[1]]='|';
				break;
			case 1:
				circuit[i][borders[1]]='/';
				break;
			case -1:
				circuit[i][borders[1]]='\\';
		}


		// Turn the kerbs...
		int tries = 0; // This is in case it gets to narrow and no space at once (hangs).
		while(
				tries++<5 &&
				(drand()<settings.turn_chance || // If RNG wants so,
				borders[0]+borders_directions[0] == 0 // or no space.
				))
			borders_directions[0] = rand()%3-1;
		if(borders[1]-borders[0] < settings.minimal_width) // If to narrow,
			borders_directions[0] = -1; // Make it wider
		// A sanity check.
		if(borders[0]+borders_directions[0] == 0)
			borders_directions[0] = 0;
		// And the second one.
		tries = 0;
		while(
				tries++<5 &&
				(drand()<settings.turn_chance || // If RNG wants so,
				borders[1]+borders_directions[1] == settings.race_width // or no space.
				))
			borders_directions[1]=rand()%3-1;
		if(borders[1]-borders[0] < settings.minimal_width)
			borders_directions[1] = 1;
		if(borders[1]+borders_directions[1] == settings.race_width)
			borders_directions[1] = 0;
	}
}

void track::display(WINDOW* screen, int top_line)
{
	// Get the geometry.
	int screen_width, screen_height;
	getmaxyx(screen, screen_height, screen_width);

	// For every visible line...
	for(int i = top_line; i<top_line+screen_height; i++)
	{
		// Move the cursor at it's beginning...
		// Position at screen centre.
		wmove(screen, i-top_line, (screen_width-settings.race_width)/2);
		// And print all the characters.
		for(int j=0; j<settings.race_width; j++)
			waddch(screen, circuit[i][j]);
	}
}

bool track::taken(int y, int x)
{
	return circuit[y][x]!=' ';
}

void track::mark(int y, int x, car_image *car)
{
	vector<pair<int, int> > dots = car->get_dots();

	for(unsigned int i=0; i<dots.size(); i++)
		if(circuit[y+dots[i].first][x+dots[i].second] == ' ')
			circuit[y+dots[i].first][x+dots[i].second] = settings.character;
}

void track::unmark(int y, int x, car_image *car)
{
	vector<pair<int, int> > dots = car->get_dots();

	for(unsigned int i=0; i<dots.size(); i++)
		if(circuit[y+dots[i].first][x+dots[i].second] == settings.character)
			circuit[y+dots[i].first][x+dots[i].second] = ' ';

}

player_handler::player_handler(int position, track* racecourse)
{
	// Set the sizes for the windows.
	// Height gets reused and thus is declared within class.
	int screen_width;
	getmaxyx(stdscr, screen_height, screen_width);
	int width = settings.vertical_split? screen_width/settings.players : screen_width;
	int height = settings.vertical_split? screen_height : screen_height/settings.players;
	
	// We can't set the track to be wider than the display.
	assert(settings.race_width<=width);
	
	// Prepare screen part.
	if(settings.vertical_split)
	{
		screen = newwin(
				height, width,
				// Take the corresponding vertical stripe.
				0, width*(settings.players - position - 1)
				);
	}
	else
	{
		screen = newwin(
				height, width,
				// Take the corresponding horizontal stripe.
				height*position, 0
				);
	}

	// Just copy this pointer.
	course = racecourse;

	// And create an image for yourself
	car = new car_image();

	// Place the car at a reasonable place.
	y = settings.race_length - settings.car_size;
	if(settings.shared_track)
		x = (settings.race_width - (settings.car_size+1)*(settings.players-2*position))/ 2;
	else
		x = settings.race_width/2;

	// We want the car at the very bottom of the screen.
	top_line = settings.race_length - height;

	// If not set, the player actually could freeze for a while.
	last_move = -INF;

	// Copy the controls.
	memcpy(controls, settings.controls[position], 4*sizeof(int));
	// And make sure player doesn't take off.
	command_y = command_x = 0;
}

/*player_handler::~player_handler(void)
{
	// It's so simple...
	delwin(screen);
}*/

// Just a simple switched assignment.
void player_handler::parse_input(int pressed_key)
{
	if(pressed_key == controls[0])
		command_y=ACCELERATE;
	if(pressed_key == controls[1])
		command_y=BRAKE;
	if(pressed_key == controls[2])
		command_x=LEFT;
	if(pressed_key == controls[3])
		command_x=RIGHT;
}

void player_handler::mark_position(void)
{
	course->mark(y, x, car);
}

void player_handler::unmark_position(void)
{
	course->unmark(y, x, car);
}

bool player_handler::tick(int time)
{
	bool survive = true;
	
	// The higher the car on the screen, the faster it moves.
	if(last_move + (y-top_line)/settings.speed_base < time)
	{
		last_move=time;
		y--;
		
		// Watch to not segfault here.
		top_line=max(0, top_line-1);
		
		// Handle commands
		y+=command_y;
		x+=command_x;
		command_y = command_x = 0;

		// Make sure he doesn't escape from the screen.
		y = max(top_line, min(top_line + screen_height - settings.car_size, y));
		
		if(y <= 0) // Plain win
			return false;

		course->display(screen, top_line);
		// Check for collisions.
		for(int i=y; i<y+settings.car_size; i++)
			for(int j=x; j<x+settings.car_size; j++)
				if(course->taken(i, j) && car->collision_check(i-y, j-x))
					survive=false;

		if(survive)
			car->display(screen, y-top_line, x);
		else
			car->explode(screen, y-top_line, x);
		wrefresh(screen);
	}
	
	return survive;
}

car_image::car_image(void)
{
	character = settings.character;
	color = COLOR_YELLOW;
	size = settings.car_size;

	// The coolest part - drawing the damned thing	
	_clear();
	// Original key points were (0,0), (1,4), (2,0), (3,4) and (4,0).
	// Now we just need to scale them and draw lines between.
	_line(0, 0, 1*(size-1)/4, 4*(size-1)/4);
	_line(1*(size-1)/4, 4*(size-1)/4, 2*(size-1)/4, 0);
	_line(2*(size-1)/4, 0, 3*(size-1)/4, 4*(size-1)/4);
	_line(3*(size-1)/4, 4*(size-1)/4, 4*(size-1)/4, 0);
}

void car_image::_clear(void)
{
	// Simply clear the image
	for(int i=0; i<size; i++)
		for(int j=0; j<size; j++)
			storage[i][j]=false;
}

void car_image::display(WINDOW* screen, int y, int x)
{
	// We don't need to store this anywhere, so it can be set ad-hoc.

	wattron(screen, COLOR_PAIR(color));
	wattron(screen, A_BOLD);

	for(int i=0; i<size; i++)
	{
		for(int j=0; j<size; j++)
			if(storage[i][j])
				mvwaddch(screen, y+i, x+j, character);
				
	}

	// Restore the normal color for everything else...
	wattroff(screen, COLOR_PAIR(color));
	wattroff(screen, A_BOLD);
}

void car_image::explode(WINDOW* screen, int y, int x)
{// Even in ASCII we can do cool explosions :>
	for(int i=0; i<size; i++)
	{
		for(int j=0; j<size; j++)
			if(rand()%2)
			{
				int color = rand()%7 + 1;
				wattron(screen, COLOR_PAIR(color));
				mvwaddch(screen, y+i, x+j, '*');
				wattroff(screen, COLOR_PAIR(color));
			}
				
	}
	
}

bool car_image::collision_check(int y, int x)
{
	return storage[y][x];
}

vector<pair<int, int> > car_image::get_dots(void)
{
	return dots;
}

/*
 * This function is the heart of the original task for which this program
 * was created. It is based on the simple formula, that for a line between
 * (y1, x1) and (y2, x2) and a given coordinate x, the y coordinate for a
 * point on a line is y1 + (y2-y1) / ( (x2-x1)/(x-x1) ).
 */
void car_image::_line(int y1, int x1, int y2, int x2)
{
	// It can be given the other way round...
	if(x2 < x1)
	{
		// So just swap the points.
		int tmp = x1;
		x1 = x2;
		x2 = tmp;
		// And swap the other coordinate.
		tmp = y1;
		y1 = y2;
		y2 = tmp;
	}
	for(int i=x1; i<=x2; i++)
	{
		int y = y1 + (int)((float) (y2-y1)*(i-x1)/(x2-x1)+0.5);
		// +0.5 is in order to do real rounding, not just truncation.
		storage[y][i]=true;
		dots.push_back(make_pair(y, i));
	}
}
