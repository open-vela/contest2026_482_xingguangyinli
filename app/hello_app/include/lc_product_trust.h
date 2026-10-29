#ifndef LIVING_CANVAS_LC_PRODUCT_TRUST_H
#define LIVING_CANVAS_LC_PRODUCT_TRUST_H

#include <stddef.h>

/* Public trust anchor only. This is not a credential or application secret. */
extern const char lc_product_trust_anchor_pem[];
extern const size_t lc_product_trust_anchor_pem_size;

#endif
