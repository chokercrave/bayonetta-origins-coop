#----------------------------- User configuration -----------------------------

# Common settings
#------------------------

# How you're loading your module. Used to determine how to find the target module. (AsRtld/Module/Kip)
LOAD_KIND := Module

# ExeFS slot of the module. The dynamic-FPS mod uses subsdk9, so co-op takes subsdk8 and both can be installed.
SUBSDK := subsdk8

# Program you're targetting. Used to determine where to deploy your files.
PROGRAM_ID := 0100CF5010FEC000

# Optional path to copy the final ELF to, for convenience.
ELF_EXTRACT :=

# Python command to use. Must be Python 3.4+.
PYTHON := python3

# JSON to use to make .npdm
NPDM_JSON := application.json

# Additional C/C++ flags to use. Co-op options (source/program/config.hpp) go here, e.g. -DCOOP_P2_HUG=0.
C_FLAGS :=
CXX_FLAGS :=

# AsRtld settings
#------------------------

# Path to the SD card. Used to mount and deploy files on SD, likely with hekate UMS.
MOUNT_PATH := /mnt/k

# Module settings
#------------------------

# Settings for deploying over FTP. Used by the deploy-ftp.py script.
FTP_IP :=
FTP_PORT := 5000
FTP_USERNAME := anonymous
FTP_PASSWORD :=

# Settings for deploying to Ryu. Used by the deploy-ryu.sh script.
RYU_PATH :=
