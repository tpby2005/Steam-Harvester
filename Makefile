CXX = g++
CXXFLAGS = -g $(shell pkg-config --cflags gtk+-3.0 webkit2gtk-4.0)
LDFLAGS = $(shell pkg-config --libs gtk+-3.0 webkit2gtk-4.0) -lcurl

steam-harvester: main.cpp
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

release: main.cpp
	$(CXX) $(CXXFLAGS) -O3 -o steam-harvester $^ $(LDFLAGS)