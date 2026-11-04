#include <cassert>
#include <cstdint>
#include <cstring>

#include "living_canvas_network_policy.h"

int main() {
  LivingCanvasNetworkPolicy policy(10000u);

  policy.begin(100u);
  assert(policy.activeSlot() == 0u);
  assert(!policy.connected());
  assert(!policy.shouldRotate(10099u));
  assert(policy.shouldRotate(10100u));
  assert(std::strcmp(policy.select("home-host", "hotspot-host"),
                     "home-host") == 0);

  policy.rotate(10100u);
  assert(policy.activeSlot() == 1u);
  assert(!policy.shouldRotate(20099u));
  assert(policy.shouldRotate(20100u));
  assert(std::strcmp(policy.select("home-host", "hotspot-host"),
                     "hotspot-host") == 0);

  policy.observeConnected();
  assert(policy.connected());
  assert(!policy.shouldRotate(UINT32_MAX));

  policy.observeDisconnected(30000u);
  assert(!policy.connected());
  assert(!policy.shouldRotate(39999u));
  assert(policy.shouldRotate(40000u));

  policy.rotate(40000u);
  assert(policy.activeSlot() == 0u);
  return 0;
}
