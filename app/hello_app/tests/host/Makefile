CC ?= cc
CFLAGS ?= -std=c11 -Wall -Wextra -Werror -pedantic -O2

TARGETS := test_lc_state test_lc_clock test_lc_dinner test_lc_memory test_lc_ui test_lc_voice test_lc_agent test_lc_agent_bridge test_lc_main test_lc_choice test_lc_backend_contract test_lc_competition test_lc_touch
INCLUDES := -I../../include

.PHONY: all test clean

all: $(TARGETS)

test_lc_state: test_lc_state.c ../../src/lc_state.c ../../include/lc_state.h
	$(CC) $(CFLAGS) $(INCLUDES) test_lc_state.c ../../src/lc_state.c -o $@

test_lc_voice: test_lc_voice.c ../../src/lc_voice.c ../../include/lc_voice.h
	$(CC) $(CFLAGS) $(INCLUDES) test_lc_voice.c ../../src/lc_voice.c -o $@

test_lc_agent: test_lc_agent.c ../../src/lc_agent.c ../../include/lc_agent.h
	$(CC) $(CFLAGS) $(INCLUDES) test_lc_agent.c ../../src/lc_agent.c -o $@

test_lc_agent_bridge: test_lc_agent_bridge.c ../../src/lc_agent_bridge.c ../../src/lc_agent.c ../../include/lc_agent_bridge.h ../../include/lc_agent.h
	$(CC) $(CFLAGS) $(INCLUDES) -Ifakes test_lc_agent_bridge.c ../../src/lc_agent_bridge.c ../../src/lc_agent.c -pthread -o $@

test_lc_main: test_lc_main.c ../../hello_app_main.c ../../src/lc_state.c ../../src/lc_voice.c ../../src/lc_agent.c
	$(CC) $(CFLAGS) $(INCLUDES) test_lc_main.c ../../src/lc_state.c ../../src/lc_voice.c ../../src/lc_agent.c -o $@

test_lc_clock: test_lc_clock.c ../../src/lc_clock.c ../../include/lc_clock.h
	$(CC) $(CFLAGS) $(INCLUDES) test_lc_clock.c ../../src/lc_clock.c -o $@

test_lc_dinner: test_lc_dinner.c ../../src/lc_dinner.c ../../include/lc_dinner.h ../../include/lc_state.h
	$(CC) $(CFLAGS) $(INCLUDES) test_lc_dinner.c ../../src/lc_dinner.c -o $@

test_lc_memory: test_lc_memory.c ../../src/lc_memory.c ../../include/lc_memory.h ../../include/lc_dinner.h
	$(CC) $(CFLAGS) $(INCLUDES) test_lc_memory.c ../../src/lc_memory.c -o $@

test_lc_ui: test_lc_ui.c ../../src/lc_ui.c ../../src/lc_clock.c ../../include/lc_ui.h
	$(CC) $(CFLAGS) $(INCLUDES) test_lc_ui.c ../../src/lc_ui.c ../../src/lc_clock.c -o $@

test_lc_choice: test_lc_choice.c ../../src/lc_choice.c ../../include/lc_choice.h
	$(CC) $(CFLAGS) $(INCLUDES) test_lc_choice.c ../../src/lc_choice.c -o $@

test_lc_backend_contract: test_lc_backend_contract.c ../../src/lc_backend_contract.c ../../src/lc_choice.c ../../include/lc_backend_contract.h ../../include/lc_choice.h
	$(CC) $(CFLAGS) $(INCLUDES) test_lc_backend_contract.c ../../src/lc_backend_contract.c ../../src/lc_choice.c -o $@

test_lc_competition: test_lc_competition.c ../../src/lc_competition.c ../../include/lc_competition.h ../../include/lc_choice.h
	$(CC) $(CFLAGS) $(INCLUDES) test_lc_competition.c ../../src/lc_competition.c -o $@

test_lc_touch: test_lc_touch.c ../../src/lc_touch.c ../../include/lc_touch.h
	$(CC) $(CFLAGS) $(INCLUDES) test_lc_touch.c ../../src/lc_touch.c -o $@

test: $(TARGETS)
	./test_lc_state
	./test_lc_clock
	./test_lc_dinner
	./test_lc_memory
	./test_lc_ui
	./test_lc_voice
	./test_lc_agent
	./test_lc_agent_bridge
	./test_lc_main
	./test_lc_choice
	./test_lc_backend_contract
	./test_lc_competition
	./test_lc_touch
	bash ./test_skill_metadata.sh

clean:
	rm -rf $(TARGETS) $(addsuffix .dSYM,$(TARGETS))
