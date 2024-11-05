#include <libevdev/libevdev.h>
#include <iostream>
#include <string>
#include <unistd.h>
#include <fcntl.h>

int main() {
	struct libevdev *dev = nullptr;

	for (int i = 0;; i++) {
		std::string path = "/dev/input/event" + std::to_string(i);
		int fd = open(path.c_str(), O_RDONLY|O_CLOEXEC);
		if (fd == -1) {
			break; // no more character devices
		}
		if (libevdev_new_from_fd(fd, &dev) == 0) {
			const char *phys_cstr =  libevdev_get_phys(dev);
			const char *name_cstr = libevdev_get_name(dev);
			
			std::string phys = phys_cstr?phys_cstr:"";
			std::string name = name_cstr?name_cstr:"";

			std::cout << path << std::endl;
			std::cout << "- phys: " << phys << std::endl;
			std::cout << "- name: " << name << std::endl;
			std::cout << std::endl;

			libevdev_free(dev);
			dev = nullptr;
		}
		close(fd);
	}
	return 0;
}
