#include <libevdev/libevdev.h>
#include <libevdev/libevdev-uinput.h>
#include <linux/uinput.h>
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
#include <memory>
#include <csignal>
#include <cstring>

#define DEBUG_INPUT 0

static inline uint64_t rdtsc(void)
{
	uint32_t lo, hi;

	asm volatile("rdtsc" : "=a"(lo), "=d"(hi));
	return ((uint64_t)hi << 32U) | lo;
}

class VirtualTouchInput {
    private:
	struct libevdev *dev = nullptr;
	struct libevdev_uinput *m_uinput = nullptr;
	std::mutex m_mouseMutex;

    public:
	VirtualTouchInput()
	{
		dev = libevdev_new();
	}
	~VirtualTouchInput()
	{
		libevdev_free(dev);
		libevdev_uinput_destroy(m_uinput);
	}

	int Init(const std::string &name)
	{
		libevdev_set_name(dev, name.c_str());
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

		struct input_absinfo abs_info = {};
		abs_info = {
			.value = 0,
			.minimum = 0,
			.maximum = 9,
		};
		libevdev_enable_event_code(dev, EV_ABS, ABS_MT_SLOT, &abs_info);
		abs_info = {
			.value = 0,
			.minimum = 0,
			.maximum = 200,
		};
		libevdev_enable_event_code(dev, EV_ABS, ABS_MT_TOUCH_MAJOR,
					   &abs_info);
		libevdev_enable_event_code(dev, EV_ABS, ABS_MT_WIDTH_MAJOR,
					   &abs_info);
		libevdev_enable_event_code(dev, EV_ABS, ABS_MT_PRESSURE,
					   &abs_info);
		abs_info = {
			.value = 0,
			.minimum = 0,
			.maximum = 2559,
		};
		libevdev_enable_event_code(dev, EV_ABS, ABS_MT_POSITION_X,
					   &abs_info);
		if (name.find("HMX2025") != std::string::npos ||
		    name.find("HMX2026") != std::string::npos) {
			abs_info = {
				.value = 0,
				.minimum = 0,
				//.maximum = 1439,
				.maximum = 1599,
			};
		} else if (name.find("HMX2023") != std::string::npos ||
			   name.find("HMX2024") != std::string::npos) {
			abs_info = {
				.value = 0,
				.minimum = 0,
				.maximum = 1439,
			};
		}
		libevdev_enable_event_code(dev, EV_ABS, ABS_MT_POSITION_Y,
					   &abs_info);
		abs_info = {
			.value = 0,
			.minimum = 0,
			.maximum = 65535,
		};

		libevdev_enable_event_code(dev, EV_ABS, ABS_MT_TRACKING_ID,
					   &abs_info);

		int r = libevdev_uinput_create_from_device(
			dev, LIBEVDEV_UINPUT_OPEN_MANAGED, &m_uinput);
		return r;
	}

	void process_event(struct input_event *ev)
	{
		std::lock_guard<std::mutex> guard(m_mouseMutex);
#if DEBUG_INPUT
		printf("kanli %ld type: %d code %d value %d\n", rdtsc(), ev->type, ev->code,
		       ev->value);
#endif
		libevdev_uinput_write_event(m_uinput, ev->type, ev->code,
					    ev->value);
	}
};

static std::atomic_bool sNeedStop = false;

class InputEventToVirtualEvent {
    private:
	const std::string mInputName;
	struct libevdev *dev;
	std::unique_ptr<VirtualTouchInput> mVirtualInput;

    public:
	InputEventToVirtualEvent(const char *input_name)
		: mInputName(input_name)
	{
		mVirtualInput = std::make_unique<VirtualTouchInput>();
		mVirtualInput->Init(input_name);
	}
	~InputEventToVirtualEvent()
	{
	}

	void process_event(struct input_event *ev)
	{
		mVirtualInput->process_event(ev);
	}

	static struct libevdev *
	find_device_by_name(const std::string &requested_name)
	{
		struct libevdev *dev = nullptr;

		for (int i = 0;; i++) {
			std::string path =
				"/dev/input/event" + std::to_string(i);
			int fd = open(path.c_str(), O_RDWR | O_CLOEXEC);
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

	void process_events()
	{
		struct input_event ev = {};
		int rc = 0;
		const auto flags = LIBEVDEV_READ_FLAG_NORMAL |
				   LIBEVDEV_READ_FLAG_BLOCKING;

		if (dev == nullptr) {
			return;
		}

		do {
			rc = libevdev_next_event(dev, flags, &ev);
			if (rc == LIBEVDEV_READ_STATUS_SYNC) {
				// "::::::::::::::::::::: dropped ::::::::::::::::::::::\n"
				while (rc == LIBEVDEV_READ_STATUS_SYNC) {
					process_event(&ev);
					rc = libevdev_next_event(
						dev, LIBEVDEV_READ_FLAG_SYNC,
						&ev);
					if (sNeedStop) {
						break;
					}
				}
				// "::::::::::::::::::::: re-synced ::::::::::::::::::::::\n"
			} else if (rc == LIBEVDEV_READ_STATUS_SUCCESS) {
				process_event(&ev);
			}
			if (sNeedStop) {
				break;
			}
		} while (rc == LIBEVDEV_READ_STATUS_SYNC ||
			 rc == LIBEVDEV_READ_STATUS_SUCCESS || rc == -EAGAIN);

		if (rc != LIBEVDEV_READ_STATUS_SUCCESS && rc != -EAGAIN)
			fprintf(stderr, "Failed to handle events: %s\n",
				strerror(-rc));

		rc = 0;
	out:
		libevdev_free(dev);
	}

	void retry_find_device(const char *name)
	{
		do {
			dev = find_device_by_name(name);
			if (dev == nullptr) {
				std::this_thread::sleep_for(
					std::chrono::milliseconds(500));
				fprintf(stderr, "wait for event %s ready\n",
					name);
			}
			if (sNeedStop) {
				break;
			}
		} while (dev == nullptr);
	}

	static void main(const char *name)
	{
		std::unique_ptr<InputEventToVirtualEvent> itov =
			std::make_unique<InputEventToVirtualEvent>(
				std::string(std::string("virt-") + name)
					.c_str());
		itov->retry_find_device(name);
		//itov->set_abs_info();
		itov->process_events();
	}
};

void signalHandler(int signum)
{
	std::cout << "Interrupt signal (" << signum << ") received.\n";
	// Perform cleanup tasks if needed
	sNeedStop = true;
}

int main(int argc, char *argv[])
{
	// Register signal handler for SIGINT
	signal(SIGINT, signalHandler);
	signal(SIGKILL, signalHandler);
	signal(SIGTERM, signalHandler);
	signal(SIGQUIT, signalHandler);

	std::cout << "Press Ctrl+C to interrupt the program.\n";

	std::thread mouse_thread01(InputEventToVirtualEvent::main,
				   "himax-touchscreen_HMX2023");
	std::thread mouse_thread02(InputEventToVirtualEvent::main,
				   "himax-touchscreen_HMX2024");
	std::thread mouse_thread03(InputEventToVirtualEvent::main,
				   "himax-touchscreen_HMX2025");
	std::thread mouse_thread04(InputEventToVirtualEvent::main,
				   "himax-touchscreen_HMX2026");

	while (!sNeedStop) {
		// Do some work
		std::this_thread::sleep_for(std::chrono::milliseconds(1000));
	}
	exit(0);

	return 0;
}
