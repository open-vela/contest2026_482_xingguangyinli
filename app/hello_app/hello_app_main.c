/****************************************************************************
 * Living Canvas - openvela Contest 2026 team 482
 ****************************************************************************/

#include "lc_agent.h"
#include "lc_accessory_event.h"
#include "lc_accessory_runtime.h"
#include "lc_display.h"
#include "lc_presence.h"
#include "lc_presence_flow.h"
#include "lc_state.h"
#include "lc_voice.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#ifdef __NuttX__
#include "lc_agent_bridge.h"

#  include <time.h>
#  include <unistd.h>

static lc_agent_bridge_t *g_agent_bridge;
#endif

static lc_agent_session_t g_agent;
static bool g_agent_initialized;

static bool ensure_agent_initialized(void)
{
  if (!g_agent_initialized)
    {
      if (!lc_agent_init(&g_agent))
        {
          return false;
        }

      g_agent_initialized = true;
    }

  return true;
}

static bool parse_presence_source(const char *name,
                                  lc_presence_source_t *source)
{
  if (name == NULL || source == NULL)
    {
      return false;
    }

  if (strcmp(name, "accessory") == 0)
    {
      *source = LC_PRESENCE_SOURCE_ACCESSORY;
    }
  else if (strcmp(name, "mmwave") == 0)
    {
      *source = LC_PRESENCE_SOURCE_MMWAVE;
    }
  else if (strcmp(name, "camera") == 0)
    {
      *source = LC_PRESENCE_SOURCE_CAMERA;
    }
  else
    {
      return false;
    }

  return true;
}

static int run_presence_preview(lc_state_machine_t *machine,
                                const char *source_name)
{
  lc_presence_tracker_t tracker;
  lc_presence_observation_t observation;
  lc_presence_source_t source;
  lc_actions_t actions;

  if (machine == NULL || !parse_presence_source(source_name, &source))
    {
      fprintf(stderr,
              "Living Canvas presence source must be accessory, mmwave, "
              "or camera\n");
      return 2;
    }

  lc_presence_tracker_init(&tracker, 70u, 1200u);
  if (!lc_presence_from_binary(source, true, 1000u, &observation))
    {
      return 2;
    }

  actions = lc_presence_flow_handle(&tracker, machine, &observation);
  if ((actions & LC_ACTION_PLAY_GREETING) == 0u)
    {
      fprintf(stderr, "Living Canvas presence did not pass the safety gate\n");
      return 2;
    }

  return lc_display_run_greeting_preview();
}

static int run_accessory_preview(lc_state_machine_t *machine,
                                 const char *wire)
{
  lc_accessory_runtime_t runtime;
  lc_accessory_runtime_result_t result;
  lc_accessory_runtime_status_t runtime_status;
  size_t index;

  if (machine == NULL || wire == NULL)
    {
      fprintf(stderr,
              "Living Canvas accessory preview requires an LC1 presence "
              "event\n");
      return 2;
    }

  if (!lc_accessory_runtime_init(&runtime, machine,
                                 LC_PRESENCE_SOURCE_ACCESSORY, 70u, 1200u))
    {
      return 2;
    }

  for (index = 0u; wire[index] != '\0'; index++)
    {
      runtime_status = lc_accessory_runtime_feed(
        &runtime, (unsigned char)wire[index], 1000u, &result);
      if (runtime_status != LC_ACCESSORY_RUNTIME_NONE)
        {
          fprintf(stderr, "Living Canvas accessory stream ended early\n");
          return 2;
        }
    }

  runtime_status = lc_accessory_runtime_feed(&runtime, '\n', 1000u,
                                              &result);
  if ((result.event.type != LC_ACCESSORY_PRESENCE &&
       result.event.type != LC_ACCESSORY_PRESENCE_CONFIDENCE) ||
      (runtime_status != LC_ACCESSORY_RUNTIME_GREETING &&
       runtime_status != LC_ACCESSORY_RUNTIME_ACCEPTED))
    {
      fprintf(stderr,
              "Living Canvas accessory preview requires an LC1 presence "
              "event\n");
      return 2;
    }

  if (runtime_status == LC_ACCESSORY_RUNTIME_GREETING)
    {
      int display_status = lc_display_run_greeting_preview();
      (void)lc_accessory_runtime_finish_greeting(&runtime, 1800u);
      return display_status;
    }

  return result.event.value == 0 ? 0 : 2;
}

#ifdef __NuttX__
static uint32_t monotonic_ms(void)
{
  struct timespec current;
  uint64_t milliseconds;

  if (clock_gettime(CLOCK_MONOTONIC, &current) != 0)
    {
      return 0u;
    }

  milliseconds = (uint64_t)current.tv_sec * 1000u;
  milliseconds += (uint64_t)current.tv_nsec / 1000000u;
  return (uint32_t)milliseconds;
}

static int run_agent_prompt(const char *prompt)
{
  lc_agent_status_t status;
  char reply[LC_AGENT_REPLY_MAX];

  if (g_agent_bridge == NULL)
    {
      g_agent_bridge = lc_agent_bridge_open(&g_agent, "living_canvas");
      if (g_agent_bridge == NULL)
        {
          fprintf(stderr,
                  "Living Canvas agent unavailable; start ai_agent first\n");
          return 2;
        }
    }

  status = lc_agent_bridge_ask(g_agent_bridge, prompt, monotonic_ms(),
                               15000u);
  if (status != LC_AGENT_OK)
    {
      fprintf(stderr, "Living Canvas agent request rejected (%d)\n",
              (int)status);
      if (status == LC_AGENT_REMOTE_ERROR)
        {
          (void)lc_agent_bridge_close(g_agent_bridge);
          g_agent_bridge = NULL;
        }
      return 2;
    }

  do
    {
      usleep(100000u);
      status = lc_agent_bridge_poll(g_agent_bridge, monotonic_ms());
    }
  while (status == LC_AGENT_WAITING);

  if (status == LC_AGENT_READY &&
      lc_agent_bridge_copy_reply(g_agent_bridge, reply, sizeof(reply)) ==
        LC_AGENT_OK)
    {
      printf("Living Canvas agent reply: %s\n", reply);
      (void)lc_agent_bridge_close(g_agent_bridge);
      g_agent_bridge = NULL;
      return 0;
    }

  fprintf(stderr, "Living Canvas agent stopped safely (%d)\n", (int)status);
  if (status == LC_AGENT_ERROR)
    {
      (void)lc_agent_bridge_close(g_agent_bridge);
      g_agent_bridge = NULL;
    }
  return 2;
}
#endif

int main(int argc, char *argv[])
{
  lc_state_machine_t machine;
  lc_voice_session_t voice;

  lc_state_init(&machine, 30000u);
  if (!lc_voice_init(&voice, 15000u))
    {
      fprintf(stderr, "Living Canvas voice safety init failed\n");
      return 1;
    }

  if (!ensure_agent_initialized())
    {
      fprintf(stderr, "Living Canvas agent safety init failed\n");
      return 1;
    }

#if defined(__NuttX__) || defined(LC_MAIN_HOST_TEST)
  if (argc == 2 && strcmp(argv[1], "--ui-preview") == 0)
    {
      return lc_display_run_preview();
    }

  if (argc == 2 && strcmp(argv[1], "--image-preview") == 0)
    {
      return lc_display_run_image_preview();
    }

  if (argc == 2 && strcmp(argv[1], "--greeting-preview") == 0)
    {
      return lc_display_run_greeting_preview();
    }

  if (argc == 3 && strcmp(argv[1], "--presence-preview") == 0)
    {
      return run_presence_preview(&machine, argv[2]);
    }

  if (argc == 3 && strcmp(argv[1], "--accessory-preview") == 0)
    {
      return run_accessory_preview(&machine, argv[2]);
    }

  if (argc == 2 && strcmp(argv[1], "--choice-preview") == 0)
    {
      return lc_display_run_choice_preview();
    }

  if (argc == 2 && strcmp(argv[1], "--product") == 0)
    {
      return lc_display_run_product();
    }

  if (argc == 4 && strcmp(argv[1], "--product-accessory") == 0)
    {
      lc_presence_source_t source;

      if (!parse_presence_source(argv[3], &source))
        {
          fprintf(stderr,
                  "Living Canvas presence source must be accessory, mmwave, "
                  "or camera\n");
          return 2;
        }

      return lc_display_run_product_accessory(argv[2], source);
    }

  if (argc == 5 && strcmp(argv[1], "--product-inputs") == 0)
    {
      lc_presence_source_t source;

      if (!parse_presence_source(argv[3], &source))
        {
          fprintf(stderr,
                  "Living Canvas presence source must be accessory, mmwave, "
                  "or camera\n");
          return 2;
        }

      return lc_display_run_product_inputs(argv[2], source, argv[4]);
    }

  if (argc == 3 && strcmp(argv[1], "--competition-demo") == 0)
    {
      return lc_display_run_competition_demo(argv[2]);
    }

  if (argc == 3 && strcmp(argv[1], "--project-demo") == 0)
    {
      return lc_display_run_project_demo(argv[2]);
    }

#  ifdef __NuttX__
  if (argc == 3 && strcmp(argv[1], "--agent-prompt") == 0)
    {
      return run_agent_prompt(argv[2]);
    }
#  endif

  if (argc != 1)
    {
      fprintf(stderr,
              "Usage: living_canvas [--ui-preview | --image-preview | "
              "--choice-preview | "
              "--greeting-preview | "
              "--presence-preview <accessory|mmwave|camera> | --product | "
              "--product-accessory <event-path> "
              "<accessory|mmwave|camera> | "
              "--product-inputs <presence-path> "
              "<accessory|mmwave|camera> <checkin-path> | "
              "--accessory-preview <LC1-event> | "
              "--competition-demo <handoff-url> | "
              "--project-demo <public-project-url> | "
              "--agent-prompt \"request\"]\n");
      return 2;
    }
#else
  (void)argc;
  (void)argv;
#endif

  printf("Living Canvas core ready (state=%d, voice_active=%d, agent_state=%d)\n",
         (int)machine.state, lc_voice_is_active(&voice) ? 1 : 0,
         (int)g_agent.status);
  return 0;
}
