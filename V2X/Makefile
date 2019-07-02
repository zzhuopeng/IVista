LIBS	:= $(TOPDIR)/lib/libV2X.a

AOBJS	:= $(patsubst %.s, %.o, $(wildcard *.s))
COBJS	:= $(patsubst %.c, %.o, $(wildcard *.c))
CPPOBJS	:= $(patsubst %.cpp, %.o, $(wildcard *.cpp))
OBJS	:= $(AOBJS) $(COBJS) $(CPPOBJS)
#CFLAGS += -I. -lm -lpthread

all: $(OBJS)
	$(AR)  cr $(LIBS) $^

clean:
	rm -f $(OBJS) $(LIBS)