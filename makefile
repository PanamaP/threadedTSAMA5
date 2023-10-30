# Compiler settings
CXX = g++
CXXFLAGS = -std=c++11 -Wall

# Targets
all: tsamgroup37 client

tsamgroup37: new_server.cpp
	$(CXX) $(CXXFLAGS) new_server.cpp -o tsamgroup37

client: client.cpp
	$(CXX) $(CXXFLAGS) client.cpp -o client

clean:
	rm -f tsamgroup37 client

.PHONY: all tsamgroup37 client clean

