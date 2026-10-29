#!/bin/sh
set -eu

project_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
build_dir="$project_dir/build/host-tests"
mkdir -p "$build_dir"

c++ -std=c++17 -Wall -Wextra -Werror \
  -I"$project_dir/src" \
  "$project_dir/tests/test_state.cpp" \
  "$project_dir/src/living_canvas_state.cpp" \
  -o "$build_dir/test_state"

"$build_dir/test_state"

c++ -std=c++17 -Wall -Wextra -Werror \
  -I"$project_dir/src" \
  "$project_dir/tests/test_input_policy.cpp" \
  "$project_dir/src/living_canvas_input_policy.cpp" \
  -o "$build_dir/test_input_policy"

"$build_dir/test_input_policy"

c++ -std=c++17 -Wall -Wextra -Werror \
  -I"$project_dir/src" \
  "$project_dir/tests/test_cat_ui.cpp" \
  "$project_dir/src/living_canvas_cat_ui.cpp" \
  -o "$build_dir/test_cat_ui"

"$build_dir/test_cat_ui"

c++ -std=c++17 -Wall -Wextra -Werror \
  -I"$project_dir/src" \
  "$project_dir/tests/test_render_policy.cpp" \
  "$project_dir/src/living_canvas_render_policy.cpp" \
  -o "$build_dir/test_render_policy"

"$build_dir/test_render_policy"

c++ -std=c++17 -Wall -Wextra -Werror \
  -I"$project_dir/src" \
  "$project_dir/tests/test_local_link.cpp" \
  "$project_dir/src/living_canvas_local_link.cpp" \
  -o "$build_dir/test_local_link"

"$build_dir/test_local_link"

s1_app_dir="$project_dir/../../app/hello_app"
cc -std=c11 -Wall -Wextra -Werror \
  -I"$s1_app_dir/include" \
  -c "$s1_app_dir/src/lc_local_accessory.c" \
  -o "$build_dir/lc_local_accessory_interop.o"

c++ -std=c++17 -Wall -Wextra -Werror \
  -I"$project_dir/src" \
  -I"$s1_app_dir/include" \
  "$project_dir/tests/test_local_protocol_interop.cpp" \
  "$project_dir/src/living_canvas_local_link.cpp" \
  "$build_dir/lc_local_accessory_interop.o" \
  -o "$build_dir/test_local_protocol_interop"

"$build_dir/test_local_protocol_interop"

cc -std=c11 -Wall -Wextra -Werror \
  -I"$project_dir/src" \
  "$project_dir/tests/test_qrcode.c" \
  "$project_dir/src/qrcode/qrcodegen.c" \
  -o "$build_dir/test_qrcode"
"$build_dir/test_qrcode"

python_bin="${PYTHON_BIN:-python3}"
"$python_bin" "$project_dir/tests/test_build_assets.py"

if [ -f "$project_dir/tests/test_source_boundaries.py" ]; then
  "$python_bin" "$project_dir/tests/test_source_boundaries.py"
fi

if [ -f "$project_dir/tests/test_private_config.py" ]; then
  "$python_bin" "$project_dir/tests/test_private_config.py"
fi

if [ -f "$project_dir/tests/test_pairing_bundle.py" ]; then
  "$python_bin" "$project_dir/tests/test_pairing_bundle.py"
fi

if [ -f "$project_dir/tests/test_device_binding.py" ]; then
  "$python_bin" "$project_dir/tests/test_device_binding.py"
fi

if [ -f "$project_dir/tests/test_flash_guard.py" ]; then
  "$python_bin" "$project_dir/tests/test_flash_guard.py"
fi

if [ -f "$project_dir/tests/test_delivery_verifier.py" ]; then
  "$python_bin" "$project_dir/tests/test_delivery_verifier.py"
fi

if [ -f "$project_dir/tests/test_restore_guard.py" ]; then
  "$python_bin" "$project_dir/tests/test_restore_guard.py"
fi

if [ -f "$project_dir/tests/test_verify_port_identity.py" ]; then
  "$python_bin" "$project_dir/tests/test_verify_port_identity.py"
fi
