zracer: zracer.cpp
	g++ -Os -Wall -lncurses -o zracer zracer.cpp
	
zracer.exe: zracer.cpp
	/opt/xmingw/bin/i386-mingw32msvc-g++ -I /opt/xmingw/i386-mingw32msvc/include -Wall -lncurses -o zracer.exe zracer.cpp

clean:
	rm zracer

install:
	install -g games -o root zracer /usr/games/bin/
