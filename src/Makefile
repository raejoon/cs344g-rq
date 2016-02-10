CXX = g++

# The CPPFLAGS variable sets compile flags for g++:
#  -g		   compile with debug information
#  -Wall  	   give all diagnostic warnings
#  -pedantic   require compliance with ANSI standard
CPPFLAGS = -g -Wall -pedantic -Wextra -Weffc++ -Werror -std=c++11 -pthread -I/usr/local/include/RaptorQ/

# The LDFLAGS variable sets flags for linker
LDFLAGS = -L/usr/local/lib

# list of files that are part of the project
# If you add/change names of header/source files, here is where you edit the
# Makefile.
PROGRAMS = sender.cc receiver.cc
EXTRAS = address.cc file_descriptor.cc poller.cc socket.cc timestamp.cc
HEADERS = $(EXTRAS:.cc=.h) util.hh
SOURCES = $(PROGRAMS) $(EXTRAS)
OBJECTS = $(SOURCES:.cc=.o)
TARGETS = $(PROGRAMS:.cc=)

default: $(TARGETS)

sender: sender.o address.o socket.o file_descriptor.o timestamp.o poller.o
	$(CXX) $(CPPFLAGS) -o $@ $^ $(LDFLAGS)

receiver: receiver.o address.o socket.o file_descriptor.o timestamp.o poller.o
	$(CXX) $(CPPFLAGS) -o $@ $^ $(LDFLAGS)

Makefile.dependencies:: $(SOURCES) $(HEADERS)
	$(CXX) $(CPPFLAGS) -MM $(SOURCES) > Makefile.dependencies

-include Makefile.dependencies

# Phony means not a "real" target, it doesn't build anything
# The phony target "clean" is used to remove all compiled object files.

.PHONY: clean

clean:
	@rm -rf $(TARGETS) $(OBJECTS)
