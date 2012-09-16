Purpose

Receives heart rate data from the Polar OEM wireless module and logs to
an SD card.  The info is logged as a text file using a FAT filesystem.

Information

For more information, please see the project blog at:
http://tinkerish.com/blog/?p=130

* main.c, Makefile - main project
* mmc_if.c, mmc_if.h - MMC/SD card driver
* diskio.c, diskio.h - lowlevel disk I/O shim between driver and filesystem
* ttf.c, ttf.h - tiny FAT filesystem

License

This project contains code shared under the GPL v2.  Some files are included
with this project and may be under a different license.  For each of those
files, the original copyright and source information is included, where
applicable.  If you have any questions, please contact me.

