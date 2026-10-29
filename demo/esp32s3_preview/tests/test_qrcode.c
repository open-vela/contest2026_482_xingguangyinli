#include <assert.h>
#include <stdbool.h>
#include <stdint.h>

#include "qrcode/qrcodegen.h"

int main(void) {
  uint8_t temp[qrcodegen_BUFFER_LEN_FOR_VERSION(8)];
  uint8_t qr[qrcodegen_BUFFER_LEN_FOR_VERSION(8)];
  bool ok = qrcodegen_encodeText(
      "https://github.com/open-vela/contest2026_482_xingguangyinli", temp, qr,
      qrcodegen_Ecc_LOW, 1, 8, qrcodegen_Mask_AUTO, true);
  assert(ok);
  int size = qrcodegen_getSize(qr);
  assert(size >= 21 && size <= 49);
  int dark = 0;
  for (int y = 0; y < size; ++y) {
    for (int x = 0; x < size; ++x) dark += qrcodegen_getModule(qr, x, y) ? 1 : 0;
  }
  assert(dark > 100);
  return 0;
}
