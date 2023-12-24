Template Project
================

This project can be used as a template for your next little C project.
It comes with lots of "frog DNA" from [libite (-lite)][1], that holds
utility functions missing from the standard C library, and an `epoll()`
based event loop from [libuEv][1] -- all you need for that made-easy
programming experience!

The project is built using GNU autotools, in a special stripped-down
setup that even the most die-hard CMake or Meson champion will find
user friendly.


Take Care!  
 /Joachim <3


Example
-------

The included source is an example of how to periodically poll the Linux
hwmon temperature sensors and save data to a JSON file for consumption
by some other tool.

[1]: https://github.com/troglobit/libite
[2]: https://github.com/troglobit/libuev
