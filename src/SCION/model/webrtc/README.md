# WebRTC

This directory containers some (adapted) copies of headers and sources of the
WebRTC source code and is thus intended to be used when linking against a static
build of WebRTC.

## Static build

``` sh

# Fetch
mkdir webrtc_checkout
cd webrtc_checkout
fetch --nohooks webrtc
gclient sync

# Gen build files
cd src
gn gen out/Static
gn args out/Static

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

