# libevdev-virtual-input
push code into sos

scp -r libevdev-virtual-input root@10.239.93.54:~/

build all binary in sos
```
cd libevdev-virtual-input
make all
```

1. enumerating all devices

```
./step-1-enumerating-devices

/dev/input/event6
- phys:
- name: himax-touchscreen_HMX2025

/dev/input/event7
- phys:
- name: himax-touchscreen_HMX2026
```


2. create virtual input device that the input event come from himax-touchscreen_HMX2025
```
./step-2-virtual-input "himax-touchscreen_HMX2025"
```

3. monitor virtual input

```
evtest
No device specified, trying to scan all of /dev/input/event*
Available devices:
/dev/input/event0:      Power Button
/dev/input/event1:      Sleep Button
/dev/input/event2:      Power Button
/dev/input/event3:      Video Bus
/dev/input/event4:      himax-touchscreen_HMX2023
/dev/input/event5:      himax-touchscreen_HMX2024
/dev/input/event6:      himax-touchscreen_HMX2025
/dev/input/event7:      himax-touchscreen_HMX2026
/dev/input/event8:      Virtual Touch Input
```

choose 8

touch screen himax-touchscreen_HMX2025
and we will get input event from virtual input


