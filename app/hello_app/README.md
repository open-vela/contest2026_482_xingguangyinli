# Living Canvas Gemini S1 application

比赛 manifest 将本目录映射到 openvela
`packages/demos/contest2026_482_hello_app/`。

## Manual display modes

The current software candidate keeps all display launches manual so a renderer
failure cannot take over startup or hide the recovery channel.

```text
living_canvas --ui-preview
living_canvas --image-preview
living_canvas --greeting-preview
living_canvas --presence-preview mmwave
living_canvas --accessory-preview "LC1,mmwave_bridge,presence_demo,1,presence,1"
living_canvas --choice-preview
living_canvas --product
living_canvas --product-accessory /dev/confirmed-event-node mmwave
living_canvas --product-inputs /dev/confirmed-presence-node mmwave /dev/confirmed-checkin-node
```

- `--ui-preview` is the original text-only display diagnostic.
- `--image-preview` shows the approved full-screen artwork without controls.
- `--greeting-preview` plays the sensor-triggered greeting reaction without
  requiring physical sensor hardware.
- `--presence-preview <accessory|mmwave|camera>` enters through the normalized
  confidence/state/cooldown gate before playing the same greeting reaction.
- `--accessory-preview <LC1-event>` additionally validates the bounded serial
  accessory wire format and sequence cursor through the same byte-stream
  decoder intended for UART/USB reads. The decoder supports fragmented input,
  LF/CRLF framing, overlong-line drop/recovery, and ASCII validation. A
  `presence,0` event exits without a greeting; a valid `presence,1` event enters
  the greeting path.
- Camera bridges may send a privacy-minimal confidence value without any image
  data, for example
  `LC1,camera_bridge,presence_demo,1,presence_confidence,82`. Continuous runtime
  state keeps replay protection, leave grace, and greeting cooldown active
  across frames; greeting completion explicitly returns the ambient state to
  idle so a later arrival can be handled.
- `--choice-preview` shows the artwork plus takeout, mystery-box, and eat-at-home
  cards. Takeout is highlighted by default.
- `--product` runs the touch, voice, backend, QR handoff, and bounded two-agent
  dialogue overlay. The beagle coordinates while the cat explains memory and
  safety constraints. Their bubbles appear in a short chat rhythm rather than
  as one unexplained result card.
- `--product-accessory <event-path> <accessory|mmwave|camera>` keeps the normal
  product UI running while a background reader consumes bounded `LC1` events.
  A qualifying arrival raises the greeting overlay and returns to the live
  product flow after 1.8 seconds. This path expects a confirmed device node or
  a thin vendor-protocol bridge; it does not guess sensor baud, voltage, pinout,
  or parse raw camera frames.
- `--product-inputs` keeps anonymous presence and touch-checkin inputs separate,
  so a sensor event cannot acquire the confirmation authority of a cat accessory.

`tools/presence_bridge.py` is the reference seam for the still-unknown vendor
driver. It accepts only privacy-minimal JSON lines and emits LC1 events:

```sh
printf '%s\n' '{"detected":true,"confidence":82}' | \
  python3 tools/presence_bridge.py \
    --source camera --session-id bench_01 --output /dev/confirmed-event-node
```

The bridge deliberately rejects image/frame/face/identity/embedding fields. It
does not configure voltage, pins, baud, or a camera SDK; those stay blocked
until the exact physical module is confirmed.

The source PNG files are verified and converted by
`tools/convert_choice_assets.py`. Device firmware contains RGB565 arrays and
does not require a PNG decoder, video decoder, SD card, or writable storage.

## Living Canvas backend boundary

`lc_product_transport`, `lc_private_http` and `lc_strict_https` implement the
bounded session, dialogue, confirmation and handoff transport. The transport
starts only with an explicit runtime backend URL and owner token; private HTTP
is restricted to numeric loopback/RFC1918 targets, while public targets require
strict HTTPS. Successful host tests do not claim current Gemini S1 network or
voice hardware acceptance.

Restaurant selection and payment are never silently submitted. Real ordering
still requires an authorized provider/account and a separate user confirmation
and payment handoff.
