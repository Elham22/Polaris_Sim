# Polaris

Overview of the Polaris implementation for the thesis "Traffic Engineering in SCION".

For general information on this SCION simulator, refer to the main
[README](README.md).

## Polaris

The most important sources are the Polaris classes in `src/SCION/model/apps`.
The contain the Polaris sender implementation. The TCP sender/receiver class
that wraps around the ns-3 native TCP implementation can also be found in there.

The adaptation to the router for the probes is contained in the border router classes.

The receiver logic to record incoming data packets and send receiver reports is simple enough for it to be contained in the `ScionHost` class, see for example `ScionCapableNode::ProcessReceivedPacket()`.

## Congestion control / WebRTC

Bits of the initial implementation of WebRTC congestion control are in
`src/SCION/model/webrtc-cc/`. Along with some types still used in the current code.

In `src/SCION/model/webrtc/` is the main congestion controller interface by
WebRTC. It further contains documentation on how to statically build and link
the WebRTC library into ns-3, which is required to build Polaris. See the
[README](src/SCION/model/webrtc/README.md).

## Utilities / Evaluation

Classes used for generating and running evaluation scenarios are in the directory `pan/` (path-aware networking).

## Build instructions

The tedious part is the integration of WebRTC. For that, refer to the
[WebRTC README](src/SCION/model/webrtc/README.md).

In general, the flow is roughly as follows:

``` sh

# Setup and build WebRTC...

# Export WebRTC src dir
export WEBRTC_SRC_DIR=/home/patrick/git/webrtc_checkout/src

# NOTE: Use --build-profile=optimized for larger simulations, especially TCP gets very slow due to all the debug code.
CXX="clang++" ./waf configure --build-profile=debug

# The included waf does not work with Python 3.12+
python3.11 ./waf --run "scion configs/traffic-engineering/demo/demo.yaml"

# Plot the results
pan/plot.py configs/traffic-engineering/demo/demo.out.txt
```

## Debugging

VSCode can be used for debugging, by using the `(gdb) Waf Pipe Launch` configuration specified in `launch.json`. The simulation YAML file needs to be specified in there as well.
