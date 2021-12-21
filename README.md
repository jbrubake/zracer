# ZRacer

[Original](https://lrem.net/software/zracer.html) version by Remigiusz 'lRem'
Modrzejewski, PhD

## Features

- Semi-graphical ncurses driven interface.
- Hotseat multiplayer.
- Random track generator.
- Runs on both Linux and Windows, tested.
- Beautiful source code.

## Development state

v.1.0 - complete

## Instructions

Player 1 controls his car with arrow keys, and player 2 does it with wsad. The
higher the car is on the screen, the faster it moves. Game time is measured with
turns, where 1 turn is the time needed to move the car when it’s at the top
verge of the screen. The track has two kerbs and occasional rocks are generated.
When a car hits a rock or a kerb, it explodes. Your goal is to get to the finish
line, without exploding and within shortest possible time. Have fun.

## Remarks

When running on Windows, you can experience unexplained lags. You should also
change the terminal resolution to something much bigger. Generally the Windows
standard terminal is quite weak, don’t blame me. To compile it under Windows you
need Dev-C++ and pdcurses module (from the updates downloader).

## License

ZRacer is distributed under the terms of GNU General Public License (GPL), which
can be found at http://www.gnu.org/copyleft/gpl.html. Additionally you are
encouraged to mail me any feedback about my software.

