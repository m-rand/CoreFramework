# CoreFramework

Alternative implementation of Apple's CoreFoundation framework. Written for and tested on Linux and Win32 back in the 200x days mostly at my spare time. :) 

All basic classes like Data for binary buffers, all collections (array, set, dictionary), char8 strings, also RunLoop with timers and system and custom sources, MessagePort and NotificationCenter are implemented. On the other hand, a lot of classes are missing (e.g. URL, unicode strings, streams, time and dates) are missing.

All 'make' stuff and tests are included in the `_rpy` directory in proprietary [Rhapsody's](https://en.wikipedia.org/wiki/Rational_Rhapsody) files. Probably could be extracted, but I have no interests to do it these days. :)

PS: All functions are written with respect to MISRA-C requirement of only one function exit point. Most of the functions should also be compliant to MISRA-C 2004 standard. 
