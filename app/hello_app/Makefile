############################################################################
# Living Canvas - openvela Contest 2026 team 482
############################################################################

include $(APPDIR)/Make.defs

CFLAGS += ${INCDIR_PREFIX}$(CURDIR)/include
CFLAGS += ${INCDIR_PREFIX}$(APPDIR)/packages/ai_agent/include

PROGNAME  = living_canvas
PRIORITY  = SCHED_PRIORITY_DEFAULT
STACKSIZE = 40960
MODULE    = $(CONFIG_LVX_USE_DEMO_CONTEST2026_482_LIVING_CANVAS)

MAINSRC = hello_app_main.c
CSRCS = src/lc_state.c src/lc_clock.c src/lc_dinner.c src/lc_memory.c \
        src/lc_ui.c src/lc_voice.c src/lc_agent.c \
        src/lc_agent_bridge.c src/lc_choice.c src/lc_competition.c \
        src/lc_touch.c \
        src/lc_backend_contract.c \
        src/lc_display.c \
        src/generated/lc_choice_assets.c

include $(APPDIR)/Application.mk
