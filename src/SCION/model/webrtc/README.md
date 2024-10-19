# WebRTC

This directory containers some copies of headers and sources of the
WebRTC source code and is thus intended to be used when linking against a static
build of WebRTC. `fetch` and `gclient` commands are provided by depot_tools:
<https://commondatastorage.googleapis.com/chrome-infra-docs/flat/depot_tools/docs/html/depot_tools_tutorial.html#_setting_up>

## Static build

``` sh

# Fetch
mkdir webrtc_checkout
cd webrtc_checkout
fetch --nohooks webrtc
gclient sync

# Gen build files
cd src
git checkout 5e49544a76ad38e5f000cdf8b20a25278fb78475 # branch-heads/6576
gn gen out/Static
gn args out/Static # See below for args

# Build
ninja -C out/Static
```

Build args:

```
is_component_build = false
rtc_include_tests = false
rtc_use_h264 = false
use_rtti = true
rtc_enable_symbol_export = true
symbol_level = 2
use_custom_libcxx = false
is_debug = true
```

Build NS-3 with clang so we don't end up with two different standard libraries
used (libstdc++ vs libc++):

``` sh
export WEBRTC_SRC_DIR=$(HOME)/git/webrtc_checkout/src
CXX="clang++" ./waf configure
```

