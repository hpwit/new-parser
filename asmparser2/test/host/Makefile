SRC_DIR := ../../src
BUILD_DIR := build
CXX ?= g++
CXXFLAGS ?= -std=c++17 -g -O0 -I$(SRC_DIR)

SRCS := $(wildcard $(SRC_DIR)/*.cpp) test_parser.cpp
OBJS := $(addprefix $(BUILD_DIR)/,$(notdir $(SRCS:.cpp=.o)))

$(BUILD_DIR)/test_parser: $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $(OBJS)

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: %.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

run: $(BUILD_DIR)/test_parser
	./$(BUILD_DIR)/test_parser

# ArduinoJson-dependent test for updateJsonParameters() (see
# test_json.cpp) -- separate build dir/flags so the default `run` target
# above never needs __JSON_OPTION__ or ArduinoJson at all.
JSON_BUILD_DIR := build-json
JSON_SRCS := $(wildcard $(SRC_DIR)/*.cpp) test_json.cpp
JSON_OBJS := $(addprefix $(JSON_BUILD_DIR)/,$(notdir $(JSON_SRCS:.cpp=.o)))
JSON_CXXFLAGS = -std=c++17 -g -O0 -D__JSON_OPTION__ -I$(SRC_DIR) -I$(ARDUINOJSON_DIR)

run-json: $(JSON_BUILD_DIR)/test_json
	./$(JSON_BUILD_DIR)/test_json

$(JSON_BUILD_DIR)/test_json: $(JSON_OBJS)
	$(CXX) $(JSON_CXXFLAGS) -o $@ $(JSON_OBJS)

$(JSON_BUILD_DIR)/%.o: $(SRC_DIR)/%.cpp | $(JSON_BUILD_DIR)
	$(CXX) $(JSON_CXXFLAGS) -c $< -o $@

$(JSON_BUILD_DIR)/%.o: %.cpp | $(JSON_BUILD_DIR)
	$(CXX) $(JSON_CXXFLAGS) -c $< -o $@

$(JSON_BUILD_DIR):
	mkdir -p $(JSON_BUILD_DIR)

# sc_examples pipeline check (see test_sc_examples.cpp) -- compiles the
# real *.sc scripts from the sibling v1 repo's sc_examples/ through the
# full parse->createBinary->createExecutableFromBinary pipeline. Separate
# binary/build dir from the default `run` target, same reasoning as
# run-json above: keeps test_parser.cpp's 30-odd hand-written tests free
# of a second main().
SC_BUILD_DIR := build-sc
SC_SRCS := $(wildcard $(SRC_DIR)/*.cpp) test_sc_examples.cpp
SC_OBJS := $(addprefix $(SC_BUILD_DIR)/,$(notdir $(SC_SRCS:.cpp=.o)))

run-sc: $(SC_BUILD_DIR)/test_sc_examples
	./$(SC_BUILD_DIR)/test_sc_examples

$(SC_BUILD_DIR)/test_sc_examples: $(SC_OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $(SC_OBJS)

$(SC_BUILD_DIR)/%.o: $(SRC_DIR)/%.cpp | $(SC_BUILD_DIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(SC_BUILD_DIR)/%.o: %.cpp | $(SC_BUILD_DIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(SC_BUILD_DIR):
	mkdir -p $(SC_BUILD_DIR)

# Large-script / resource-budget check (see test_large_script.cpp) --
# compiles fixtures/multi_effect_controller.sc, a purpose-written
# several-hundred-line script, and checks the resulting binary against
# an ESP32-without-PSRAM size budget. Separate binary/build dir, same
# reasoning as run-json/run-sc above.
LARGE_BUILD_DIR := build-large
LARGE_SRCS := $(wildcard $(SRC_DIR)/*.cpp) test_large_script.cpp
LARGE_OBJS := $(addprefix $(LARGE_BUILD_DIR)/,$(notdir $(LARGE_SRCS:.cpp=.o)))

run-large: $(LARGE_BUILD_DIR)/test_large_script
	./$(LARGE_BUILD_DIR)/test_large_script

$(LARGE_BUILD_DIR)/test_large_script: $(LARGE_OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $(LARGE_OBJS)

$(LARGE_BUILD_DIR)/%.o: $(SRC_DIR)/%.cpp | $(LARGE_BUILD_DIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(LARGE_BUILD_DIR)/%.o: %.cpp | $(LARGE_BUILD_DIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(LARGE_BUILD_DIR):
	mkdir -p $(LARGE_BUILD_DIR)

clean:
	rm -rf $(BUILD_DIR) $(JSON_BUILD_DIR) $(SC_BUILD_DIR) $(LARGE_BUILD_DIR)

.PHONY: run run-json run-sc run-large clean
