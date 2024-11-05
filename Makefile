all : step-1-enumerating-devices step-2-virtual-input

step-1-enumerating-devices : step-1-enumerating-devices.cpp
	g++ -o $@ $^ `pkg-config --cflags --libs libevdev` -pthread
step-2-virtual-input : step-2-virtual-input.cpp
	g++ -o $@ $^ `pkg-config --cflags --libs libevdev` -pthread

clean:
	rm -rf step-1-enumerating-devices  step-2-virtual-input


