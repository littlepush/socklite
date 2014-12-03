# Make file for Socket Lite
# Powered By Push Chen

# GNU GPL LICENSE
# socklite -- a C++ socket library for Linux/Windows/iOS
# Copyright (C) 2014  Push Chen

# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.

# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.

# You should have received a copy of the GNU General Public License along
# with this program; if not, write to the Free Software Foundation, Inc.,
# 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.

# You can connect me by email: littlepush@gmail.com, 
# or @me on twitter: @littlepush

SL_ROOT = ./
OUT_DIR = $(SL_ROOT)/result

SL_DEFINES = -DVERSION=\"$(shell ./version)\" -DTARGET=\"$(shell gcc -v 2> /dev/stdout | grep Target | cut -d ' ' -f 2)\"
THIRDPARTY = -I./inc

ifeq "$(MAKECMDGOALS)" "release"
	DEFINES = $(SL_DEFINES) $(THIRDPARTY) -DSL_RELEASE -DRELEASE
	CPPFLAGS = 
	CFLAGS = -O2 -Wall $(DEFINES) -fPIC
	CXXFLAGS = -O2 -Wall $(DEFINES) -fPIC
	CMDGOAL_DEF := $(MAKECMDGOALS)
else
	ifeq "$(MAKECMDGOALS)" "withpg"
		DEFINES = $(SL_DEFINES) $(THIRDPARTY) -DSL_WITHPG -DWITHPG -DDEBUG
		CPPFLAGS = 
		CFLAGS = -g -pg -Wall $(DEFINES) -fPIC
		CXXFLAGS = -g -pg -Wall $(DEFINES) -fPIC
		CMDGOAL_DEF := $(MAKECMDGOALS)
	else
		DEFINES = $(SL_DEFINES) $(THIRDPARTY) -DSL_RELEASE -DRELEASE
		CPPFLAGS =
		CFLAGS = -O2 -Wall $(DEFINES) -fPIC
		CXXFLAGS = -O2 -Wall $(DEFINES) -fPIC
		CMDGOAL_DEF := $(MAKECMDGOALS)
	endif
endif

vpath %.h   ./inc/
vpath %.cpp	./src/

CC	 = g++
CPP	 = g++
CXX	 = g++
AR	 = ar

CPP_FILES = $(wildcard ./src/*.cpp)
OBJ_FILES = $(CPP_FILES:.cpp=.o)

STATIC_LIBS = 
DYNAMIC_LIBS = libsocklite.so
EXECUTABLE = 
TEST_CASE = 
RELAY_OBJECT = 

all	: PreProcess $(STATIC_LIBS) $(DYNAMIC_LIBS) $(EXECUTABLE) $(TEST_CASE) AfterMake

PreProcess :
	@mkdir -p $(OUT_DIR)/static
	@mkdir -p $(OUT_DIR)/dynamic
	@mkdir -p $(OUT_DIR)/bin
	@mkdir -p $(OUT_DIR)/test
	@mkdir -p $(OUT_DIR)/data
	@mkdir -p $(OUT_DIR)/conf
	@mkdir -p $(OUT_DIR)/log
	@echo $(CPP_FILES)

cmdgoalError :
	@echo '***************************************************'
	@echo '******You must specified a make command goal.******'
	@echo '***************************************************'

clean :
	rm -vf src/*.o; rm -rf $(OUT_DIR)

AfterMake : 
	rm -vf src/*.o;
	mv -vf $(SL_ROOT)/libsocklite.so $(OUT_DIR)/dynamic/libsocklite.so.$(shell ./version)

debug : PreProcess $(STATIC_LIBS) $(DYNAMIC_LIBS) $(EXECUTABLE) $(TEST_CASE) AfterMake
	@exit 0

release : PreProcess $(STATIC_LIBS) $(DYNAMIC_LIBS) $(EXECUTABLE) $(TEST_CASE) AfterMake
	@exit 0

withpg : PreProcess $(STATIC_LIBS) $(DYNAMIC_LIBS) $(EXECUTABLE) $(TEST_CASE) AfterMake
	@exit 0

%.o: src/%.cpp
	$(CC) $(CXXFLAGS) -c -o $@ $<

libsocklite.so : $(OBJ_FILES)
	$(CC) -shared -o $@ $^

