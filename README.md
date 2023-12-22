Template Project
================

Use this project as a template for your next little C project.  It comes
with a simple event loop from [libuEv][1], which uses all the new fancy
Linux kernel stuff.  The template also comes with lots of "frog DNA"
from [libite (-lite)][2], for that made easy C programming environment.

The project is built using GNU autotools, in a special stripped-down
setup that even the most die-hard CMake or Meson champoin will find
user friendly.


Take Care!  
 /Joachim <3


Example
-------

The included source is an example of how to periodically poll a Linux
hwmon temperature sensor and save that to a JSON file for consumption
by some other tool.


[1]: https://github.com/troglobit/libuev
[2]: https://github.com/troglobit/libite
