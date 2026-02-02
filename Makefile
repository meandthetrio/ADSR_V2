# Project Name
TARGET = ADSR_V2
APP_TYPE = BOOT_QSPI

# Sources
CPP_SOURCES = main.cpp
CPP_SOURCES += params.cpp
CPP_SOURCES += audio_engine.cpp
CPP_SOURCES += ui_logic.cpp
CPP_SOURCES += ui_render.cpp
CPP_SOURCES += oled_pager.cpp
CPP_SOURCES += voice_engine.cpp

# Library Locations
LIBDAISY_DIR = ../../libDaisy
DAISYSP_DIR = ../../DaisySP

# Core location, and generic Makefile.
SYSTEM_FILES_DIR = $(LIBDAISY_DIR)/core
include $(SYSTEM_FILES_DIR)/Makefile
