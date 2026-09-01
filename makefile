CXX		:= g++
FLAGS  	:= -std=c++17 -O2 -Wall -Wextra -MMD -MP
BUILD  	:= build
MAKEFLAGS += --silent

SRCS   	:= $(shell find . -type f -name "*.cpp" ! -path "./$(BUILD)/*" | sed 's|^\./||')
OBJS   	:= $(SRCS:%.cpp=$(BUILD)/%.o)
DEPS   	:= $(OBJS:.o=.d)
TARGET 	:= $(BUILD)/vivi
INCS	:= -Iinclude -Isrc

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(FLAGS) $(INCS) $^ -o $@ 
	rm -f $(OBJS) $(DEPS)
	@echo "Vivi build done."

$(BUILD)/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(FLAGS) $(INCS) -c $< -o $@

.PHONY: clean
clean:
	rm -rf $(BUILD)

-include $(DEPS)
