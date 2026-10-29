############################################################################
# Living Canvas - openvela Contest 2026 team 482
############################################################################

include $(APPDIR)/Make.defs

CFLAGS += ${INCDIR_PREFIX}$(CURDIR)/include
CFLAGS += ${INCDIR_PREFIX}$(APPDIR)/packages/ai_agent/include
CFLAGS += ${INCDIR_PREFIX}$(APPDIR)/packages/ai_agent/src
CFLAGS += ${INCDIR_PREFIX}$(APPDIR)/../frameworks/multimedia/media/include

PROGNAME  = living_canvas
PRIORITY  = SCHED_PRIORITY_DEFAULT
STACKSIZE = 40960
MODULE    = $(CONFIG_LVX_USE_DEMO_CONTEST2026_482_LIVING_CANVAS)

MAINSRC = hello_app_main.c
CSRCS = src/lc_state.c src/lc_greeting.c src/lc_clock.c src/lc_dinner.c src/lc_memory.c \
        src/lc_ui.c src/lc_voice.c src/lc_audio_transport.c src/lc_pcm_recorder.c \
        src/lc_voice_client.c src/lc_pcm_player.c src/lc_native_voice.c \
        src/lc_accessory_event.c src/lc_accessory_client.c \
        src/lc_accessory_sync.c \
        src/lc_accessory_stream.c \
        src/lc_accessory_runtime.c \
        src/lc_checkin.c \
        src/lc_checkin_feedback.c \
        src/lc_local_accessory.c \
        src/lc_local_accessory_mbedtls.c \
        src/lc_local_accessory_server.c \
        src/lc_private_http.c \
        src/lc_strict_https.c \
        src/lc_presence.c src/lc_presence_flow.c \
        src/lc_mr60_presence.c \
        src/lc_esphome_presence.c \
        src/lc_agent.c \
        src/lc_agent_bridge.c src/lc_choice.c src/lc_choice_prompt.c \
        src/lc_competition.c \
        src/lc_product_flow.c \
        src/lc_product_accessory.c \
        src/lc_product_dialogue.c \
        src/lc_product_intent.c \
        src/lc_product_transport.c \
        src/lc_product_trust.c \
        src/lc_touch.c \
        src/lc_backend_contract.c \
        src/lc_display.c \
        src/lc_room_view.c \
        src/generated/lc_choice_assets.c \
        src/generated/lc_font_cjk_14.c

include $(APPDIR)/Application.mk
