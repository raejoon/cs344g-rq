CXX = g++

# The CPPFLAGS variable sets compile flags for g++:
#  -g		   compile with debug information
#  -Wall  	   give all diagnostic warnings
#  -pedantic   require compliance with ANSI standard
CPPFLAGS = -g -Wall -pendantic

# The LDFLAGS variable sets flags for linker
LDFLAGS = 

# list of files that are part of the project
# If you add/change names of header/source files, here is where you edit the
# Makefile.
PROGRAMS = sender.cc receiver.cc
SOURCES = $(PROGRAMS) $(EXTRAS)
OBJECTS = $(SOURCES:.cc=.o)
TARGETS = $(PROGRAMS:.cc=)

default: $(TARGETS)

sender: sender.o
	$(CXX) $(CPPFLAGS) -o $@ $^ $(LDFLAGS)

receiver: receiver.o
	$(CXX) $(CPPFLAGS) -o $@ $^ $(LDFLAGS)

# Phony means not a "real" target, it doesn't build anything
# The phony target "clean" is used to remove all compiled object files.

.PHONY: clean

clean:
	@rm -rf $(TARGETS) $(OBJECTS)
