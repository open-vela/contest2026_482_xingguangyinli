#define LC_MAIN_HOST_TEST 1
#define main living_canvas_main
#include "../../hello_app_main.c"
#undef main

#include <assert.h>
#include <stddef.h>
#include <stdio.h>

int lc_display_run_preview(void)
{
  return 31;
}

int lc_display_run_image_preview(void)
{
  return 32;
}

int lc_display_run_greeting_preview(void)
{
  return 37;
}

int lc_display_run_choice_preview(void)
{
  return 33;
}

int lc_display_run_product(void)
{
  return 36;
}

int lc_display_run_product_accessory(const char *event_path,
                                     lc_presence_source_t source)
{
  assert(event_path != NULL);
  assert(strcmp(event_path, "/dev/ttyS3") == 0);
  assert(source == LC_PRESENCE_SOURCE_MMWAVE);
  return 38;
}

int lc_display_run_product_inputs(const char *presence_event_path,
                                  lc_presence_source_t source,
                                  const char *checkin_event_path)
{
  assert(presence_event_path != NULL);
  assert(strcmp(presence_event_path, "/dev/ttyS3") == 0);
  assert(source == LC_PRESENCE_SOURCE_MMWAVE);
  assert(checkin_event_path != NULL);
  assert(strcmp(checkin_event_path, "/dev/ttyS4") == 0);
  return 39;
}

int lc_display_run_competition_demo(const char *qr_url)
{
  assert(qr_url != NULL);
  assert(strcmp(qr_url, "https://example.invalid/c/demo") == 0);
  return 34;
}

int lc_display_run_project_demo(const char *project_url)
{
  assert(project_url != NULL);
  assert(strcmp(project_url,
                "https://github.com/open-vela/"
                "contest2026_482_xingguangyinli") == 0);
  return 35;
}

int main(void)
{
  char *argv[] = {"living_canvas", NULL};
  char *image_argv[] = {"living_canvas", "--image-preview", NULL};
  char *greeting_argv[] = {"living_canvas", "--greeting-preview", NULL};
  char *presence_mmwave_argv[] = {
    "living_canvas", "--presence-preview", "mmwave", NULL
  };
  char *presence_camera_argv[] = {
    "living_canvas", "--presence-preview", "camera", NULL
  };
  char *presence_invalid_argv[] = {
    "living_canvas", "--presence-preview", "unknown", NULL
  };
  char *accessory_presence_argv[] = {
    "living_canvas", "--accessory-preview",
    "LC1,mmwave_bridge,presence_demo,1,presence,1", NULL
  };
  char *accessory_absent_argv[] = {
    "living_canvas", "--accessory-preview",
    "LC1,mmwave_bridge,presence_demo,1,presence,0", NULL
  };
  char *accessory_camera_argv[] = {
    "living_canvas", "--accessory-preview",
    "LC1,camera_bridge,presence_demo,1,presence_confidence,82", NULL
  };
  char *accessory_invalid_argv[] = {
    "living_canvas", "--accessory-preview", "not-an-event", NULL
  };
  char *choice_argv[] = {"living_canvas", "--choice-preview", NULL};
  char *product_argv[] = {"living_canvas", "--product", NULL};
  char *product_accessory_argv[] = {
    "living_canvas", "--product-accessory", "/dev/ttyS3", "mmwave", NULL
  };
  char *product_accessory_invalid_argv[] = {
    "living_canvas", "--product-accessory", "/dev/ttyS3", "face", NULL
  };
  char *product_inputs_argv[] = {
    "living_canvas", "--product-inputs", "/dev/ttyS3", "mmwave",
    "/dev/ttyS4", NULL
  };
  char *product_inputs_invalid_argv[] = {
    "living_canvas", "--product-inputs", "/dev/ttyS3", "face",
    "/dev/ttyS4", NULL
  };
  char *competition_argv[] = {"living_canvas", "--competition-demo",
                              "https://example.invalid/c/demo", NULL};
  char *project_argv[] = {
    "living_canvas", "--project-demo",
    "https://github.com/open-vela/contest2026_482_xingguangyinli", NULL
  };
  char *missing_url_argv[] = {"living_canvas", "--competition-demo", NULL};
  char *diagnostic_argv[] = {"living_canvas", "--ui-preview", NULL};
  char *invalid_argv[] = {"living_canvas", "--unknown", NULL};

  assert(living_canvas_main(1, argv) == 0);
  assert(living_canvas_main(2, image_argv) == 32);
  assert(living_canvas_main(2, greeting_argv) == 37);
  assert(living_canvas_main(3, presence_mmwave_argv) == 37);
  assert(living_canvas_main(3, presence_camera_argv) == 37);
  assert(living_canvas_main(3, presence_invalid_argv) == 2);
  assert(living_canvas_main(3, accessory_presence_argv) == 37);
  assert(living_canvas_main(3, accessory_absent_argv) == 0);
  assert(living_canvas_main(3, accessory_camera_argv) == 37);
  assert(living_canvas_main(3, accessory_invalid_argv) == 2);
  assert(living_canvas_main(2, choice_argv) == 33);
  assert(living_canvas_main(2, product_argv) == 36);
  assert(living_canvas_main(4, product_accessory_argv) == 38);
  assert(living_canvas_main(4, product_accessory_invalid_argv) == 2);
  assert(living_canvas_main(5, product_inputs_argv) == 39);
  assert(living_canvas_main(5, product_inputs_invalid_argv) == 2);
  assert(living_canvas_main(3, competition_argv) == 34);
  assert(living_canvas_main(3, project_argv) == 35);
  assert(living_canvas_main(2, missing_url_argv) == 2);
  assert(living_canvas_main(2, diagnostic_argv) == 31);
  assert(living_canvas_main(2, invalid_argv) == 2);
  puts("PASS: lc_main");
  return 0;
}
