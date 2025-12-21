CROSS_COMPILE := mips-sde-elf-
CC		  := $(CROSS_COMPILE)gcc
CFLAGS  = -EL -g -march=m14kc -msoft-float -O1 -I . -G0
# CFLAGS		  := -O -G 0 -mno-abicalls -fno-builtin -Wa,-xgot -Wall -fPIC
LD		  := $(CROSS_COMPILE)ld
OD = mips-sde-elf-objdump
OC = mips-sde-elf-objcopy
SZ = mips-sde-elf-size

