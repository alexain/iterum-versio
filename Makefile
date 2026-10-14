# Iterum Versio - firmware for the Noise Engineering Versio (Daisy Seed)

TARGET = IterumVersio
CPP_SOURCES = src/IterumVersio.cpp
OPT = -O2

# Libraries: clone them into lib/ (see README) or override on the command line,
# e.g. make LIBDAISY_DIR=~/libDaisy DAISYSP_DIR=~/DaisySP
LIBDAISY_DIR ?= lib/libDaisy
DAISYSP_DIR  ?= lib/DaisySP

SYSTEM_FILES_DIR = $(LIBDAISY_DIR)/core
include $(SYSTEM_FILES_DIR)/Makefile
