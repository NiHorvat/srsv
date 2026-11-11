DEBUG = 1 #print all events?

CXX = g++
CXXFLAGS = -MMD -Wall -Wextra -Werror -DDEBUG=$(DEBUG)
LDFLAGS = -O
LDLIBS = -lpthread

INPUT_SIM = input_sim
OBJS_IS = input_sim.o interface.o

CONTROLLER = controller
OBJS_CNT = controller.o interface.o

PROGRAMS = $(INPUT_SIM) $(CONTROLLER)

all: $(PROGRAMS)

$(INPUT_SIM): $(OBJS_IS)
	$(CXX) $(LDFLAGS) $^ $(LDLIBS) -o $@

$(CONTROLLER): $(OBJS_CNT)
	$(CXX) $(LDFLAGS) $^ $(LDLIBS) -o $@

OBJECTS = $(OBJS_IS) $(OBJS_CNT)
DEPS := $(OBJECTS:.o=.d)

.PHONY: run clean

run: $(PROGRAMS)
	@echo "Starting input-simulator"
	@./$(INPUT_SIM) &
	@sleep 1
	@echo "Starting controller" 
	@./$(CONTROLLER)

clean:
	-rm -rf $(PROGRAMS) $(OBJECTS) $(DEPS)

-include $(DEPS)