TOPDIR	:= $(PWD)

SUBDIRS	:= main V2X

APPS	:= APP
AOBJS	:= $(patsubst %.s, %.o, $(wildcard *.s))
COBJS	:= $(patsubst %.c, %.o, $(wildcard *.c))
CPPOBJS	:= $(patsubst %.cpp, %.o, $(wildcard *.cpp))
OBJS	:= $(AOBJS) $(COBJS) $(CPPOBJS)

#CROSS	:=
#CROSS	:=arm-none-linux-gnueabi-
ifeq ($(OPT), gcc)
	CROSS	:=
else
	CROSS	:=arm-none-linux-gnueabi-
endif

CC	:=$(CROSS)gcc
AR	:=$(CROSS)ar

INCLUDE	:= $(PWD)

#-std=gnu99
CFLAGS := -std=gnu11 -Wall -g -I$(TOPDIR)/include -I$(TOPDIR)/V2X

LDFLAGS	:= -lmain -lV2X
LDFLAGS += -L$(PWD)/lib -lm -lpthread
BIN_DIR =  bin

# if ver = release , version is release,do not printf useless information;
#  otherwise, version is debug,code will print much information
ifeq ($(ver), release)
CFLAGS += -c -O3
# 禁止assert调用
CFLAGS += -D NDEBUG
else
# 允许d_printf
CFLAGS += -Ddebug
# 在gdb中打印宏
CFLAGS += -g3 -gdwarf-2
endif

export CFLAGS TOPDIR CC

all: $(OBJS)
	@for dir in $(SUBDIRS) ; \
		do $(MAKE) -C $$dir all CC=$(CC) AR=$(AR); \
	done
	$(CC) -o $(BIN_DIR)/$(APPS) $(OBJS) $(LDFLAGS)

.PHONY: $(SUBDIRS) all clean

clean:
	rm -f  $(TOPDIR)/$(BIN_DIR)/$(APPS) $(OBJS) $(TOPDIR)/$(APPS) $(TOPDIR)/data.log
	@for dir in $(SUBDIRS) ; \
		do $(MAKE) -C $$dir clean ; \
	done
	clear



