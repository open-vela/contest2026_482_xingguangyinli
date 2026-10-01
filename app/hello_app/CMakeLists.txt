# Living Canvas - openvela Contest 2026 team 482
if(CONFIG_LVX_USE_DEMO_CONTEST2026_482_LIVING_CANVAS)
  nuttx_add_application(
    NAME living_canvas
    SRCS
      hello_app_main.c
      src/lc_state.c
      src/lc_clock.c
      src/lc_dinner.c
      src/lc_memory.c
      src/lc_ui.c
      src/lc_voice.c
      src/lc_agent.c
      src/lc_agent_bridge.c
      src/lc_choice.c
      src/lc_touch.c
      src/lc_competition.c
      src/lc_backend_contract.c
      src/lc_display.c
      src/generated/lc_choice_assets.c
    INCLUDE_DIRECTORIES
      ${CMAKE_CURRENT_SOURCE_DIR}/include
      ${NUTTX_APPS_DIR}/packages/ai_agent/include
    DEPENDS
      lvgl
    STACKSIZE 40960)
endif()
