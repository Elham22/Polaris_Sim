# WebRTC Congestion Control

This directory contains some types used for the receiver feedback, in particular
the `PacketRecord` and `PacketsReport` structs.

It further contains the sources for the initial, simple implementation of Webrtc
GCC that we later replaced by integrated the statically linked WebRTC directly.
The implementation is based on the following sources:

 * https://c3lab.poliba.it/images/6/65/Gcc-analysis.pdf
 * https://datatracker.ietf.org/doc/html/draft-ietf-rmcat-gcc-02
 * https://webrtc.googlesource.com/src