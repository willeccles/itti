#include <iostream>
#include "wrestd.h"
#include <thread>
#include "readprocess.h" // getting song info
#include <string>
#include <regex>
#include <vector>
#include <iomanip> // for manipulating iostreams
#include <stdio.h>
#include <unistd.h> // sleep(seconds)
#include <ncurses.h> // this should make the development easier
#include <signal.h>
#include <algorithm>
#include "itunescommands.h" // itunes commands
#include <time.h> // nanosleep(nanoseconds), nonbusy sleep

#define BAR_PAIR 1
#define RESET_PAIR 2
#define GENERAL_PAIR 3
#define DEFAULT_PAIR RESET_PAIR // same thing, but makes it look better in some cases to use this

// convert MS to NS
#define MILLIS(m) m * 1000000
// sleep in terms of milliseconds
#define MSLEEP(ms) nanosleep(MILLIS(ms))

short fg_color, bg_color; // store colors
bool isResizing = false; // whether or not the display is resizing
bool willQuit = false; // when this is set to true, helper threads will end and can be joined

// note: must keep in mind that LINES and COLS exist
#define ROWS LINES // ROWS/COLS seems more reasonable to me, alias ROWS to LINES

// prints song info after the titles that are placed in printTitles()
void printSongInfo(std::string title, std::string album, std::string artist) {
	/*
	  Title: [title]
	 Artist: [artist]
	  Album: [album]
	 */
	
	// get the max length of a line
	int maxlen = COLS - 10 - 2; // this is the number of columns minus the two spaces on either end and the heading
	
	// TODO: add an ellipsis when the string is too long to show all of it
	
	// now I just have to print this information.
	
	// find out what rows to print on
	int r1 = ROWS/2-1;
	mvwaddnstr(stdscr, r1, 10, title.c_str(), maxlen);
	mvwaddnstr(stdscr, r1+1, 10, artist.c_str(), maxlen);
	mvwaddnstr(stdscr, r1+2, 10, album.c_str(), maxlen);
}

// prints the title bar
void printTitles() {
	std::string title = "";
	for (int i = 0; i < COLS; i++)
		title += " ";
	
	// add the "itti" in the middle (or as close to the middle as possible)
	title.replace(COLS/2-2, 4, "itti");
	
	attron(A_BOLD);
	attron(COLOR_PAIR(BAR_PAIR));
	mvwaddstr(stdscr, 0, 0, title.c_str());
	attroff(COLOR_PAIR(BAR_PAIR));
	
	// now we can print the headings for song info
	char titleheading[12];
	sprintf(titleheading, "%10s", "Title: ");
	char artistheading[12];
	sprintf(artistheading, "%10s", "Artist: ");
	char albumheading[12];
	sprintf(albumheading, "%10s", "Album: ");
	
	// print the headings for title, artist, etc.
	int rowNum0 = ROWS/2-1;
	mvwaddstr(stdscr, rowNum0, 0, titleheading);
	mvwaddstr(stdscr, rowNum0+1, 0, artistheading);
	mvwaddstr(stdscr, rowNum0+2, 0, albumheading);
	
	attroff(A_BOLD);
	refresh();
}

// this handles when the display is resized
void handleResize(int sig) {
	endwin();
	// Needs to be called after an endwin() so ncurses will initialize
	// itself with the new terminal dimensions.
	refresh();
	clear();
	printTitles();
}

// currentTime = player progress
// totalTime = song duration
// both are in seconds
std::string timeStr(double currentTime, double totalTime) {
	int cSeconds, cMinutes, cHours,
		tSeconds, tMinutes, tHours;
	
	cMinutes = (int)currentTime/60;
	cHours = cMinutes/60;
	cSeconds = (int)currentTime%60;
	
	tMinutes = (int)totalTime/60;
	tHours = tMinutes/60;
	tSeconds = (int)totalTime%60;
	
	char cString[10]; // holds the current time string
	char tString[10]; // holds the total time string
	if (tHours > 0) {
		sprintf(tString, "%02d:%02d:%02d", tHours, tMinutes%60, tSeconds);
		sprintf(cString, "%02d:%02d:%02d", cHours, cMinutes%60, cSeconds);
	}
	else if (tMinutes > 0) {
		sprintf(tString, "%02d:%02d", tMinutes%60, tSeconds);
		sprintf(cString, "%02d:%02d", cMinutes%60, cSeconds);
	}
	else {
		sprintf(tString, "0:%02d", tSeconds);
		sprintf(cString, "0:%02d", cSeconds);
	}
	
	char allParts[22];
	sprintf(allParts, "%s/%s", cString, tString);
	
	return std::string(allParts);
}

// this is run by the thread in charge of updating the display
void updateDisplay() {
	for (;;) {
		if (willQuit) return;
		if (!isResizing) {
			// this stuff grabs the song info
			std::string info;
			int child_stdout = -1;
			pid_t child_pid = openChildProc(SONG_INFO, 0, &child_stdout);
			if (!child_pid)
				std::cout << "A thing went wrong D:\n";
			else {
				char buff[5000];
				if (readProcessOut(child_stdout, buff, sizeof(buff)))
					info = std::string(buff);
				else
					std::cout << "A different thing went wrong D:\n";
			}
			
			// name, artist, album, duration, player position, sound volume
			std::vector<std::string> songparts;
			
			// store the stuff there
			std::string delimiter = "SONG_PART_DELIM";
			size_t pos = 0;
			std::string token;
			while ((pos = info.find(delimiter)) != std::string::npos) {
				token = info.substr(0, pos);
				songparts.push_back(token);
				info.erase(0, pos + delimiter.length());
			}
			songparts.push_back(info); // this gets the very last element
			
			std::string tStr = timeStr(std::stod(songparts[4]), std::stod(songparts[3]));
			
			// get the length of the progress bar, with 2 spaces of padding on each end + the time string and the two brackets to put on it
			size_t barLen = COLS - 4 - tStr.length() - 1 - 2;
			// 4 = padding
			// 1 = space between time and bar
			// 2 = for the brackets
			
			// set up the progress bar
			std::string progBar(tStr);
			size_t p = size_t( (std::stod(songparts[4])/std::stod(songparts[3])) * (double)barLen );
			progBar = "  " + progBar + " [" + std::string(p, '#') + std::string(barLen - p, '-') + "]  ";
			
			// print the title and whatnot
			printSongInfo(songparts[0], songparts[2], songparts[1]);
			
			// print it
			attron(COLOR_PAIR(BAR_PAIR));
			mvwaddstr(stdscr, ROWS-1, 0, progBar.c_str());
			attroff(COLOR_PAIR(BAR_PAIR));
			
			refresh();
			sleep(1);
		}
	}
}

int main(void) {
	// initialize the screen
	initscr();
	start_color();
	noecho();
	cbreak();
	curs_set(0); // 0, 1, and 2 are the visibilities for the cursor, so 0 is "not there"
	
	struct sigaction sa;
	memset(&sa, 0, sizeof(struct sigaction));
	sa.sa_handler = handleResize;
	sigaction(SIGWINCH, &sa, NULL);
	
	// color pairs
	init_pair(BAR_PAIR, COLOR_WHITE, COLOR_BLUE); // white on yellow
	init_pair(RESET_PAIR, -1, -1);
	init_pair(GENERAL_PAIR, -1, -1);
	
	refresh();
	
	// draw the top bar
	printTitles();
	
	std::thread t(updateDisplay);
	
	/*
	 Proposed Controls:
	 q - quit
	 < - prev
	 > - next
	 ] or = - vol up
	 [ or - - vol down
	 */
	
	bool inputLoop = true;
	int key = -1;
	
	// TODO: Replace system() calls
	while (inputLoop && (key = getch()) != -(INT_MAX - 1)) {
		// if the key isn't q and it's not -1 (aka no key)
		switch(key) {
			case 113: // q
				inputLoop = false;
				willQuit = true;
				break;
			case 60: // <
				system(PLAYER_PREV);
				break;
			case 62: // >
				system(PLAYER_NEXT);
				break;
			default:
				break;
		}
	}
	willQuit = true; // kill the other threads
	
	t.join();
	
	endwin();
	
	return 0;
}
