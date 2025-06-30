TARGET := key_event_handler

FLAGS += -DMTK_GPS_FEATURE

INCLUDES := -I. \
            -I${STAGING_INCDIR}/libevdev-1.0 \
			-I../recipe-sysroot/usr/include/libevdev-1.0 \


SRCS := \
           ./key_event_handler.c \


CXXSRCS := \
#           ./platform/atcid_adaptation.cpp \


EXTRA_CXXFLAGS := -std=c++11

OBJS := ${SRCS:%.c=%.o}

CXXOBJS := ${CXXSRCS:%.cpp=%.o}

#LDFLAGS = -L. -lz -ldl -lpthread -lstdc++
LDFLAGS2 = -L. -levdev -L../recipe-sysroot/usr/lib

.PHONY: all
all : $(TARGET)

${TARGET}: $(OBJS) $(CXXOBJS)
	$(CC) $(CFLAGS) $(OBJS) $(CXXOBJS) $(LDFLAGS) $(LDFLAGS2) $(LIBS) -o $@

%.o: %.c
	${CC} $(CFLAGS) -c $< -o $@ $(INCLUDES) $(FLAGS)
	
%.o: %.cpp
	${CXX} $(CXXFLAGS) -c $< -o $@ $(INCLUDES) $(FLAGS) $(EXTRA_CXXFLAGS)

.PHONY: clean
clean:
	$(warning "makefile key_event_handler clean")
	rm -rf $(OBJS) $(CXXOBJS) $(TARGET)
