CC ?= cc
CFLAGS ?= -std=c11 -Wall -Wextra -Werror -pedantic -O2

TARGETS := test_lc_state test_lc_greeting test_lc_clock test_lc_dinner test_lc_memory test_lc_ui test_lc_voice test_lc_audio_transport test_lc_accessory_event test_lc_accessory_stream test_lc_accessory_runtime test_lc_checkin test_lc_presence test_lc_presence_flow test_lc_agent test_lc_agent_bridge test_lc_main test_lc_choice test_lc_choice_prompt test_lc_backend_contract test_lc_competition test_lc_product_flow test_lc_product_accessory test_lc_product_dialogue test_lc_product_intent test_lc_product_transport test_lc_touch test_lc_core_loop_isolation
INCLUDES := -I../../include
TARGETS += test_lc_pcm_recorder test_lc_pcm_recorder_clock
TARGETS += test_lc_pcm_player
TARGETS += test_lc_mr60_presence
TARGETS += test_lc_esphome_presence
TARGETS += test_lc_local_accessory
TARGETS += test_lc_local_accessory_server
TARGETS += test_lc_local_accessory_loopback
TARGETS += test_lc_checkin_feedback
TARGETS += test_lc_private_http
TARGETS += test_lc_private_http_loopback
ifneq ($(CJSON_DIR),)
TARGETS += test_lc_voice_client test_lc_voice_tts
TARGETS += test_lc_product_session
TARGETS += test_lc_native_voice
TARGETS += test_lc_accessory_client
TARGETS += test_lc_accessory_sync
endif

.PHONY: all test clean

all: $(TARGETS)

test_lc_mr60_presence: test_lc_mr60_presence.c ../../src/lc_mr60_presence.c ../../include/lc_mr60_presence.h ../../src/lc_presence_flow.c ../../src/lc_presence.c ../../src/lc_state.c
	$(CC) $(CFLAGS) $(INCLUDES) test_lc_mr60_presence.c ../../src/lc_mr60_presence.c ../../src/lc_presence_flow.c ../../src/lc_presence.c ../../src/lc_state.c -o $@

test_lc_esphome_presence: test_lc_esphome_presence.c ../../src/lc_esphome_presence.c ../../include/lc_esphome_presence.h
	$(CC) $(CFLAGS) $(INCLUDES) test_lc_esphome_presence.c ../../src/lc_esphome_presence.c -o $@

test_lc_local_accessory: test_lc_local_accessory.c ../../src/lc_local_accessory.c ../../include/lc_local_accessory.h
	$(CC) $(CFLAGS) $(INCLUDES) test_lc_local_accessory.c ../../src/lc_local_accessory.c -o $@

test_lc_local_accessory_server: test_lc_local_accessory_server.c ../../src/lc_local_accessory_server.c ../../src/lc_local_accessory.c ../../include/lc_local_accessory_server.h ../../include/lc_local_accessory.h
	$(CC) $(CFLAGS) $(INCLUDES) test_lc_local_accessory_server.c ../../src/lc_local_accessory_server.c ../../src/lc_local_accessory.c -o $@

test_lc_local_accessory_loopback: test_lc_local_accessory_loopback.c ../../src/lc_local_accessory_server.c ../../src/lc_local_accessory.c ../../include/lc_local_accessory_server.h ../../include/lc_local_accessory.h
	$(CC) $(CFLAGS) $(INCLUDES) test_lc_local_accessory_loopback.c ../../src/lc_local_accessory_server.c ../../src/lc_local_accessory.c -o $@

test_lc_checkin_feedback: test_lc_checkin_feedback.c ../../src/lc_checkin_feedback.c ../../include/lc_checkin_feedback.h
	$(CC) $(CFLAGS) $(INCLUDES) test_lc_checkin_feedback.c ../../src/lc_checkin_feedback.c -o $@

test_lc_private_http: test_lc_private_http.c ../../src/lc_private_http.c ../../include/lc_private_http.h
	$(CC) $(CFLAGS) $(INCLUDES) test_lc_private_http.c ../../src/lc_private_http.c -o $@

test_lc_private_http_loopback: test_lc_private_http_loopback.c ../../src/lc_private_http.c ../../include/lc_private_http.h
	$(CC) $(CFLAGS) $(INCLUDES) test_lc_private_http_loopback.c ../../src/lc_private_http.c -pthread -o $@

test_lc_state: test_lc_state.c ../../src/lc_state.c ../../include/lc_state.h
	$(CC) $(CFLAGS) $(INCLUDES) test_lc_state.c ../../src/lc_state.c -o $@

test_lc_greeting: test_lc_greeting.c ../../src/lc_greeting.c ../../include/lc_greeting.h
	$(CC) $(CFLAGS) $(INCLUDES) test_lc_greeting.c ../../src/lc_greeting.c -o $@

test_lc_voice: test_lc_voice.c ../../src/lc_voice.c ../../include/lc_voice.h
	$(CC) $(CFLAGS) $(INCLUDES) test_lc_voice.c ../../src/lc_voice.c -o $@

test_lc_audio_transport: test_lc_audio_transport.c ../../src/lc_audio_transport.c ../../include/lc_audio_transport.h
	$(CC) $(CFLAGS) $(INCLUDES) test_lc_audio_transport.c ../../src/lc_audio_transport.c -o $@

test_lc_pcm_recorder: test_lc_pcm_recorder.c ../../src/lc_pcm_recorder.c ../../include/lc_pcm_recorder.h
	$(CC) $(CFLAGS) $(INCLUDES) test_lc_pcm_recorder.c ../../src/lc_pcm_recorder.c -pthread -o $@

test_lc_pcm_recorder_clock: test_lc_pcm_recorder_clock.c ../../src/lc_pcm_recorder.c ../../include/lc_pcm_recorder.h
	$(CC) $(CFLAGS) -Dclock_gettime=lc_test_clock_failure $(INCLUDES) test_lc_pcm_recorder_clock.c ../../src/lc_pcm_recorder.c -pthread -o $@

test_lc_pcm_player: test_lc_pcm_player.c ../../src/lc_pcm_player.c ../../include/lc_pcm_player.h
	$(CC) $(CFLAGS) $(INCLUDES) test_lc_pcm_player.c ../../src/lc_pcm_player.c -pthread -o $@

# Supply CJSON_DIR from the SDK, e.g. /private/tmp/lc-sdk-cjson. No mock parser.
test_lc_voice_client: test_lc_voice_client.c ../../src/lc_voice_client.c ../../src/lc_product_transport.c ../../src/lc_private_http.c ../../include/lc_voice_client.h
	$(CC) $(CFLAGS) $(INCLUDES) -I$(CJSON_DIR) test_lc_voice_client.c ../../src/lc_voice_client.c ../../src/lc_product_transport.c ../../src/lc_private_http.c $(CJSON_DIR)/cJSON.c -lm -o $@

test_lc_voice_tts: test_lc_voice_tts.c ../../src/lc_voice_client.c ../../src/lc_product_transport.c ../../src/lc_private_http.c ../../include/lc_voice_client.h
	$(CC) $(CFLAGS) $(INCLUDES) -I$(CJSON_DIR) test_lc_voice_tts.c ../../src/lc_voice_client.c ../../src/lc_product_transport.c ../../src/lc_private_http.c $(CJSON_DIR)/cJSON.c -lm -o $@

test_lc_product_session: test_lc_product_session.c ../../src/lc_product_transport.c ../../src/lc_product_trust.c ../../src/lc_private_http.c ../../include/lc_product_transport.h
	$(CC) $(CFLAGS) -D__NuttX__ $(INCLUDES) -Ifakes -I$(CJSON_DIR) test_lc_product_session.c ../../src/lc_product_transport.c ../../src/lc_product_trust.c ../../src/lc_private_http.c $(CJSON_DIR)/cJSON.c -lm -o $@

test_lc_native_voice: test_lc_native_voice.c ../../src/lc_native_voice.c ../../include/lc_native_voice.h ../../src/lc_pcm_recorder.c ../../src/lc_pcm_player.c ../../src/lc_voice_client.c ../../src/lc_product_transport.c ../../src/lc_private_http.c
	$(CC) $(CFLAGS) $(INCLUDES) -I$(CJSON_DIR) test_lc_native_voice.c ../../src/lc_native_voice.c ../../src/lc_pcm_recorder.c ../../src/lc_pcm_player.c ../../src/lc_voice_client.c ../../src/lc_product_transport.c ../../src/lc_private_http.c $(CJSON_DIR)/cJSON.c -pthread -lm -o $@

test_lc_accessory_client: test_lc_accessory_client.c ../../src/lc_accessory_client.c ../../src/lc_accessory_event.c ../../src/lc_product_transport.c ../../src/lc_private_http.c ../../include/lc_accessory_client.h
	$(CC) $(CFLAGS) $(INCLUDES) -I$(CJSON_DIR) test_lc_accessory_client.c ../../src/lc_accessory_client.c ../../src/lc_accessory_event.c ../../src/lc_product_transport.c ../../src/lc_private_http.c $(CJSON_DIR)/cJSON.c -lm -o $@

test_lc_accessory_sync: test_lc_accessory_sync.c ../../src/lc_accessory_sync.c ../../src/lc_accessory_client.c ../../src/lc_accessory_runtime.c ../../src/lc_checkin.c ../../src/lc_accessory_stream.c ../../src/lc_accessory_event.c ../../src/lc_presence.c ../../src/lc_presence_flow.c ../../src/lc_state.c ../../src/lc_product_transport.c ../../src/lc_private_http.c ../../include/lc_accessory_sync.h
	$(CC) $(CFLAGS) $(INCLUDES) -I$(CJSON_DIR) test_lc_accessory_sync.c ../../src/lc_accessory_sync.c ../../src/lc_accessory_client.c ../../src/lc_accessory_runtime.c ../../src/lc_checkin.c ../../src/lc_accessory_stream.c ../../src/lc_accessory_event.c ../../src/lc_presence.c ../../src/lc_presence_flow.c ../../src/lc_state.c ../../src/lc_product_transport.c ../../src/lc_private_http.c $(CJSON_DIR)/cJSON.c -lm -o $@

test_lc_accessory_event: test_lc_accessory_event.c ../../src/lc_accessory_event.c ../../include/lc_accessory_event.h
	$(CC) $(CFLAGS) $(INCLUDES) test_lc_accessory_event.c ../../src/lc_accessory_event.c -o $@

test_lc_accessory_stream: test_lc_accessory_stream.c ../../src/lc_accessory_stream.c ../../src/lc_accessory_event.c ../../include/lc_accessory_stream.h ../../include/lc_accessory_event.h
	$(CC) $(CFLAGS) $(INCLUDES) test_lc_accessory_stream.c ../../src/lc_accessory_stream.c ../../src/lc_accessory_event.c -o $@

test_lc_accessory_runtime: test_lc_accessory_runtime.c ../../src/lc_accessory_runtime.c ../../src/lc_accessory_stream.c ../../src/lc_accessory_event.c ../../src/lc_presence.c ../../src/lc_presence_flow.c ../../src/lc_state.c ../../include/lc_accessory_runtime.h
	$(CC) $(CFLAGS) $(INCLUDES) test_lc_accessory_runtime.c ../../src/lc_accessory_runtime.c ../../src/lc_accessory_stream.c ../../src/lc_accessory_event.c ../../src/lc_presence.c ../../src/lc_presence_flow.c ../../src/lc_state.c -o $@

test_lc_checkin: test_lc_checkin.c ../../src/lc_checkin.c ../../src/lc_accessory_stream.c ../../src/lc_accessory_event.c ../../include/lc_checkin.h ../../include/lc_accessory_stream.h ../../include/lc_accessory_event.h
	$(CC) $(CFLAGS) $(INCLUDES) test_lc_checkin.c ../../src/lc_checkin.c ../../src/lc_accessory_stream.c ../../src/lc_accessory_event.c -o $@

test_lc_presence: test_lc_presence.c ../../src/lc_presence.c ../../include/lc_presence.h
	$(CC) $(CFLAGS) $(INCLUDES) test_lc_presence.c ../../src/lc_presence.c -o $@

test_lc_presence_flow: test_lc_presence_flow.c ../../src/lc_presence_flow.c ../../src/lc_presence.c ../../src/lc_state.c ../../include/lc_presence_flow.h
	$(CC) $(CFLAGS) $(INCLUDES) test_lc_presence_flow.c ../../src/lc_presence_flow.c ../../src/lc_presence.c ../../src/lc_state.c -o $@

test_lc_agent: test_lc_agent.c ../../src/lc_agent.c ../../include/lc_agent.h
	$(CC) $(CFLAGS) $(INCLUDES) test_lc_agent.c ../../src/lc_agent.c -o $@

test_lc_agent_bridge: test_lc_agent_bridge.c ../../src/lc_agent_bridge.c ../../src/lc_agent.c ../../include/lc_agent_bridge.h ../../include/lc_agent.h
	$(CC) $(CFLAGS) $(INCLUDES) -Ifakes test_lc_agent_bridge.c ../../src/lc_agent_bridge.c ../../src/lc_agent.c -pthread -o $@

test_lc_main: test_lc_main.c ../../hello_app_main.c ../../src/lc_accessory_event.c ../../src/lc_accessory_stream.c ../../src/lc_accessory_runtime.c ../../src/lc_state.c ../../src/lc_presence.c ../../src/lc_presence_flow.c ../../src/lc_voice.c ../../src/lc_agent.c
	$(CC) $(CFLAGS) $(INCLUDES) test_lc_main.c ../../src/lc_accessory_event.c ../../src/lc_accessory_stream.c ../../src/lc_accessory_runtime.c ../../src/lc_state.c ../../src/lc_presence.c ../../src/lc_presence_flow.c ../../src/lc_voice.c ../../src/lc_agent.c -o $@

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

test_lc_choice_prompt: test_lc_choice_prompt.c ../../src/lc_choice_prompt.c ../../include/lc_choice_prompt.h ../../include/lc_choice.h
	$(CC) $(CFLAGS) $(INCLUDES) test_lc_choice_prompt.c ../../src/lc_choice_prompt.c -o $@

test_lc_backend_contract: test_lc_backend_contract.c ../../src/lc_backend_contract.c ../../src/lc_choice.c ../../include/lc_backend_contract.h ../../include/lc_choice.h
	$(CC) $(CFLAGS) $(INCLUDES) test_lc_backend_contract.c ../../src/lc_backend_contract.c ../../src/lc_choice.c -o $@

test_lc_competition: test_lc_competition.c ../../src/lc_competition.c ../../include/lc_competition.h ../../include/lc_choice.h
	$(CC) $(CFLAGS) $(INCLUDES) test_lc_competition.c ../../src/lc_competition.c -o $@

test_lc_product_flow: test_lc_product_flow.c ../../src/lc_product_flow.c ../../include/lc_product_flow.h ../../include/lc_choice.h
	$(CC) $(CFLAGS) $(INCLUDES) test_lc_product_flow.c ../../src/lc_product_flow.c -o $@

test_lc_product_accessory: test_lc_product_accessory.c ../../src/lc_product_accessory.c ../../src/lc_product_flow.c ../../include/lc_product_accessory.h ../../include/lc_accessory_runtime.h ../../include/lc_product_flow.h
	$(CC) $(CFLAGS) $(INCLUDES) test_lc_product_accessory.c ../../src/lc_product_accessory.c ../../src/lc_product_flow.c -o $@

test_lc_product_dialogue: test_lc_product_dialogue.c ../../src/lc_product_dialogue.c ../../src/lc_product_flow.c ../../include/lc_product_dialogue.h ../../include/lc_product_flow.h
	$(CC) $(CFLAGS) $(INCLUDES) test_lc_product_dialogue.c ../../src/lc_product_dialogue.c ../../src/lc_product_flow.c -o $@

test_lc_product_intent: test_lc_product_intent.c ../../src/lc_product_intent.c ../../include/lc_product_intent.h ../../include/lc_choice.h
	$(CC) $(CFLAGS) $(INCLUDES) test_lc_product_intent.c ../../src/lc_product_intent.c -o $@

test_lc_product_transport: test_lc_product_transport.c ../../src/lc_product_transport.c ../../src/lc_private_http.c ../../include/lc_product_transport.h ../../include/lc_product_flow.h ../../include/lc_private_http.h
	$(CC) $(CFLAGS) $(INCLUDES) test_lc_product_transport.c ../../src/lc_product_transport.c ../../src/lc_private_http.c -o $@

test_lc_touch: test_lc_touch.c ../../src/lc_touch.c ../../include/lc_touch.h
	$(CC) $(CFLAGS) $(INCLUDES) test_lc_touch.c ../../src/lc_touch.c -o $@

test_lc_core_loop_isolation: test_lc_core_loop_isolation.c ../../src/lc_checkin.c ../../src/lc_accessory_stream.c ../../src/lc_accessory_event.c ../../src/lc_product_accessory.c ../../src/lc_product_flow.c ../../src/lc_presence_flow.c ../../src/lc_presence.c ../../src/lc_state.c
	$(CC) $(CFLAGS) $(INCLUDES) test_lc_core_loop_isolation.c ../../src/lc_checkin.c ../../src/lc_accessory_stream.c ../../src/lc_accessory_event.c ../../src/lc_product_accessory.c ../../src/lc_product_flow.c ../../src/lc_presence_flow.c ../../src/lc_presence.c ../../src/lc_state.c -o $@

test: $(TARGETS)
	./test_lc_state
	./test_lc_greeting
	./test_lc_clock
	./test_lc_dinner
	./test_lc_memory
	./test_lc_ui
	./test_lc_voice
	./test_lc_audio_transport
	./test_lc_pcm_recorder
	./test_lc_pcm_recorder_clock
	./test_lc_pcm_player
	./test_lc_mr60_presence
	./test_lc_esphome_presence
	./test_lc_local_accessory
	./test_lc_local_accessory_server
	./test_lc_local_accessory_loopback
	./test_lc_checkin_feedback
	./test_lc_private_http
	./test_lc_private_http_loopback
	bash ./test_private_http_source.sh
ifneq ($(CJSON_DIR),)
	./test_lc_voice_client
	./test_lc_voice_tts
	./test_lc_product_session
	./test_lc_native_voice
	./test_lc_accessory_client
	./test_lc_accessory_sync
endif
	./test_lc_accessory_event
	./test_lc_accessory_stream
	./test_lc_accessory_runtime
	./test_lc_checkin
	./test_lc_presence
	./test_lc_presence_flow
	./test_lc_agent
	./test_lc_agent_bridge
	./test_lc_main
	./test_lc_choice
	./test_lc_choice_prompt
	./test_lc_backend_contract
	./test_lc_competition
	./test_lc_product_flow
	./test_lc_product_accessory
	./test_lc_product_dialogue
	./test_lc_product_intent
	./test_lc_product_transport
	./test_lc_touch
	./test_lc_core_loop_isolation
	bash ./test_skill_metadata.sh
	bash ./test_product_voice_source.sh
	bash ./test_product_transport_source.sh
	python3 ./test_product_trust.py
	python3 ./test_room_source.py
	python3 ./test_presence_bridge.py
	python3 ./test_esphome_presence_filter.py
	python3 ./test_esphome_presence_c_filter.py
	python3 ./test_resolve_mmwave_port.py
	python3 ./test_sample_mmwave_presence.py
	python3 ./test_supervise_mmwave_relay.py
	python3 ./test_mmwave_display_source.py
	python3 ./test_accessory_http_relay.py
	python3 ./test_checkin_display_source.py

clean:
	rm -rf $(TARGETS) $(addsuffix .dSYM,$(TARGETS))
