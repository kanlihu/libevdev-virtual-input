#include <libevdev/libevdev.h>
#include <libevdev/libevdev-uinput.h>
#include <iostream>
#include <string>
#include <mutex>
#include <set>
#include <thread>
#include <chrono>
#include <atomic>
#include <unistd.h>
#include <fcntl.h>
#include <grp.h>
#include <math.h>
#include <cstring>

class VirtualTouchInput {
public:
	struct libevdev_uinput* m_uinput = nullptr;
	std::mutex m_mouseMutex;
public:
	VirtualTouchInput() {}
	~VirtualTouchInput() {
		libevdev_uinput_destroy(m_uinput);
	}

	int Init() {
		struct libevdev* dev = libevdev_new();
		libevdev_set_name(dev, "Virtual Touch Input");
		libevdev_set_id_vendor(dev, 0x8086);
		libevdev_set_id_product(dev, 0x0002);

		libevdev_enable_property(dev, INPUT_PROP_DIRECT);
		libevdev_enable_event_type(dev, EV_SYN);
		libevdev_enable_event_type(dev, EV_KEY);
		libevdev_enable_event_code(dev, EV_KEY, KEY_HOME, nullptr);
		libevdev_enable_event_code(dev, EV_KEY, KEY_POWER, nullptr);
		libevdev_enable_event_code(dev, EV_KEY, KEY_MENU, nullptr);
		libevdev_enable_event_code(dev, EV_KEY, KEY_BACK, nullptr);
		libevdev_enable_event_code(dev, EV_KEY, KEY_SEARCH, nullptr);
		libevdev_enable_event_code(dev, EV_KEY, BTN_TOUCH, nullptr);
		libevdev_enable_event_code(dev, EV_KEY, KEY_APPSELECT, nullptr);
		libevdev_enable_event_type(dev, EV_ABS);

		struct input_absinfo abs_info;
		std::memset(&abs_info, sizeof(abs_info), 0);
		abs_info = 
		{
				.value = 0,
				.minimum = 0,
				.maximum = 9,
		};
		libevdev_enable_event_code(dev, EV_ABS, ABS_MT_SLOT, &abs_info);
		abs_info = 
		{
				.value = 0,
				.minimum = 0,
				.maximum = 200,
		};
		libevdev_enable_event_code(dev, EV_ABS, ABS_MT_TOUCH_MAJOR, &abs_info);
		libevdev_enable_event_code(dev, EV_ABS, ABS_MT_WIDTH_MAJOR, &abs_info);
		libevdev_enable_event_code(dev, EV_ABS, ABS_MT_PRESSURE, &abs_info);
		abs_info = 
		{
			.value = 0,
			.minimum = 0,
			.maximum = 2559,
		};
		libevdev_enable_event_code(dev, EV_ABS, ABS_MT_POSITION_X, &abs_info);
		abs_info = 
		{
			.value = 0,
			.minimum = 0,
			.maximum = 1599,
		};
		libevdev_enable_event_code(dev, EV_ABS, ABS_MT_POSITION_Y, &abs_info);
		abs_info = 
		{
			.value = 0,
			.minimum = 0,
			.maximum = 65535,
		};
		
		libevdev_enable_event_code(dev, EV_ABS, ABS_MT_TRACKING_ID, &abs_info);

		int r = libevdev_uinput_create_from_device(dev, LIBEVDEV_UINPUT_OPEN_MANAGED, &m_uinput);
		libevdev_free(dev);
		return r;
	}

	void process_event(struct input_event *ev) {
		std::lock_guard<std::mutex> guard(m_mouseMutex);
		libevdev_uinput_write_event(m_uinput, 
			ev->type, 
			ev->code, 
			ev->value);
	

	}
};

VirtualTouchInput g_mouse;

struct libevdev* find_device_by_name(const std::string& requested_name) {
	struct libevdev *dev = nullptr;

	for (int i = 0;; i++) {
		std::string path = "/dev/input/event" + std::to_string(i);
		int fd = open(path.c_str(), O_RDWR|O_CLOEXEC);
		if (fd == -1) {
			break; // no more character devices
		}
		if (libevdev_new_from_fd(fd, &dev) == 0) {
			std::string name = libevdev_get_name(dev);
			if (name == requested_name) {
				return dev;
			}
			libevdev_free(dev);
			dev = nullptr;
		}
		close(fd);
	}

	return nullptr;
}

std::set<int> g_pressedKeys;
std::mutex g_pressed_keys_mutex;

void process_event(struct input_event *ev) {
	std::lock_guard<std::mutex> guard(g_pressed_keys_mutex);
	g_mouse.process_event(ev);
}

void process_events(struct libevdev *dev) {
	struct input_event ev = {};
	int status = 0;
	auto is_error = [](int v) { return v < 0 && v != -EAGAIN; };
	auto has_next_event = [](int v) { return v >= 0; };
	const auto flags = LIBEVDEV_READ_FLAG_NORMAL | LIBEVDEV_READ_FLAG_BLOCKING;

	while (status = libevdev_next_event(dev, flags, &ev), !is_error(status)) {
		if (!has_next_event(status)) continue;
		process_event(&ev);
	}
}

std::atomic_bool g_run_mouse_thread;
void mouse_thread_fn(void*) {
	float rx = 0;
	float ry = 0;
	const float friction = 0.85;
	const float accel = 1.2/friction;
	while (g_run_mouse_thread) {
		float dx = 0;
		float dy = 0;

		float rs = 0;

		{
			std::lock_guard<std::mutex> guard(g_pressed_keys_mutex);
			if (g_pressedKeys.count(77) > 0) rx += accel;
			if (g_pressedKeys.count(75) > 0) rx -= accel;
			if (g_pressedKeys.count(76) > 0) ry += accel;
			if (g_pressedKeys.count(72) > 0) ry -= accel;

			if (g_pressedKeys.count(78) > 0) rs += 1;
			if (g_pressedKeys.count(14) > 0) rs -= 1;
		}

		// resize movement vector to be length 1
		if (fabs(dx)+fabs(dy) > accel) {
			dx *= .7;
			dy *= .7;
		}

		rx += dx;
		ry += dy;
		rx *= friction;
		ry *= friction;

		std::this_thread::sleep_for(std::chrono::milliseconds(10));
	}
}

int main(int argc, char *argv[]) {

	char name[1024];
	auto grp = getgrnam("input");
	if (grp == nullptr) {
		std::cerr << "getgrnam(\"input\") failed" << std::endl;
		return -1;
	}
	int oldgid = getgid();
	if (setgid(grp->gr_gid) < 0) {
		std::cerr << "couldn't change group to input!" << std::endl;
		return -1;
	}

	if (argc >= 2) {
		strncpy(name, argv[1], 1024);
	} else {
		strncpy(name, "himax-touchscreen_HMX2025", 1024);
	}
	printf("open event %s\n",name);
	struct libevdev *dev = find_device_by_name(name);

	if (dev == nullptr) {
		std::cerr << "Couldn't find device!" << std::endl;
		return -1;
	}

	// must init mouse before we drop permissions
	if (g_mouse.Init() != 0) {
		std::cerr << "couldn't init mouse!" << std::endl;
		return -1;
	}

	//drop back into old permissions
	if (setgid(oldgid) < 0) {
		std::cerr << "couldn't switch back to old group!" << std::endl;
		return -1;
	}

	g_run_mouse_thread = true;
	std::thread mouse_thread(mouse_thread_fn, nullptr);

	libevdev_grab(dev, LIBEVDEV_GRAB);

	process_events(dev);

	libevdev_free(dev);

	g_run_mouse_thread = false;
	mouse_thread.join();

	return 0;
}
