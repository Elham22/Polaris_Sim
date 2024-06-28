/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright (c) 2024 ETH Zuerich
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation;
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * Author: Patrick Wicki <patrick.wicki@inf.ethz.ch>
 */

#ifndef WEBRTC_CC_TYPES_H
#define WEBRTC_CC_TYPES_H

#include "src/core/model/simulator.h"
#include <vector>

namespace ns3 {

enum class BandwidthUsage { kBwNormal = 0, kBwUnderusing = 1, kBwOverusing = 2 };
enum class ControllerState { DECREASE, HOLD, INCREASE };
const char *BandwidthUsageToString (BandwidthUsage usage);

/**
 * Congestion control related constants
 */
const Time CC_INITIAL_RTT = Time ("100ms");
const double CC_INITIAL_SEND_RATE = 500'000; // 300 kbps is the default, but that's too slow without probing
const double CC_ADDITIVE_TERM = 1e3; // KB, per frame or at least every 50ms
const double CC_MULTI_INCREASE = 1.05; // factor, per RTT

/**
 * Record of a packet with all information relevant for congestion control
 */
struct PacketRecord
{
  Time time_sent;
  Time time_received;
  uint32_t seq_no;
  uint32_t frame_no;
  uint16_t size; // size in bytes
};

/**
 * BitRate class, with a similar interface to the DataRate class used in WebRTC code
 * ns-3 already has a DataRate class, but that one doesn't have enough functionality and
 * has quite a different interface.
 */
class BitRate
{
private:
  int64_t bps_; // bits per second

public:
  constexpr BitRate () : bps_ (0) {}
  constexpr BitRate (int64_t rate) : bps_ (rate) {}
  constexpr BitRate (const BitRate &other) : bps_ (other.bps_) {}

  constexpr BitRate &operator= (const BitRate &other)
  {
    bps_ = other.bps_;
    return *this;
  }
  ~BitRate () {}

  static BitRate Zero () { return BitRate (0); }
  static BitRate Infinity () { return BitRate (std::numeric_limits<int64_t>::max ()); }
  static BitRate MinusInfinity () { return BitRate (std::numeric_limits<int64_t>::min ()); }
  static BitRate BitsPerSec (int64_t rate) { return BitRate (rate); }
  static BitRate KilobitsPerSec (int64_t rate) { return BitRate (rate * 1'000); }

  constexpr bool IsZero () const { return bps_ == 0; }

  constexpr int64_t bps() const { return bps_; }
  constexpr double kbps() const { return bps_ / 1'000.0; }
  constexpr int64_t BytesPerSec () const { return bps_ / 8; }

  friend std::ostream& operator<<(std::ostream& os, const BitRate& br) {
    os << br.bps_ << " bps";
    return os;
  }

  BitRate operator+ (const BitRate &other) const { return BitRate (bps_ + other.bps_); }
  BitRate operator- (const BitRate &other) const { return BitRate (bps_ - other.bps_); }
  BitRate operator* (const BitRate &other) const { return BitRate (bps_ * other.bps_); }
  BitRate operator/ (const BitRate &other) const { return BitRate (bps_ / other.bps_); }

  BitRate operator-= (const BitRate &other) { bps_ -= other.bps_; return *this; }
  BitRate operator+= (const BitRate &other) { bps_ += other.bps_; return *this; }

  // Template operators to work with any numeric type
  template<typename T>
  constexpr BitRate operator* (T factor) const {
    static_assert(std::is_arithmetic<T>::value, "Factor must be a numeric type");
    return BitRate(bps_ * static_cast<double>(factor));
  }

  // Template operator/ to work with any numeric type
  template<typename T>
  constexpr BitRate operator/ (T divisor) const {
    static_assert(std::is_arithmetic<T>::value, "Divisor must be a numeric type");
    if (divisor == 0) {
      throw std::invalid_argument("Division by zero");
    }
    return BitRate(bps_ / static_cast<double>(divisor));
  }

  // Double cast
  constexpr operator double () const { return bps_; }

  // Allow double as LHS for multiplication
  friend BitRate operator* (double factor, const BitRate &rate) { return rate * factor; }

  constexpr bool operator== (const BitRate &other) const { return bps_ == other.bps_; }
  constexpr bool operator!= (const BitRate &other) const { return bps_ != other.bps_; }
  constexpr bool operator< (const BitRate &other) const { return bps_ < other.bps_; }
  constexpr bool operator> (const BitRate &other) const { return bps_ > other.bps_; }
  constexpr bool operator<= (const BitRate &other) const { return bps_ <= other.bps_; }
  constexpr bool operator>= (const BitRate &other) const { return bps_ >= other.bps_; }
};

/**
 * Report of packets received by the receiver
 */
struct PacketsReport
{
  Time time_created = Simulator::Now ();
  std::vector<PacketRecord> packets;

  void
  AddPacket (PacketRecord packet)
  {
    packets.push_back (packet);
  }

  // Frame is assumed complete when the newest packet is part of a new frame
  bool
  IsFrameComplete ()
  {
    if (packets.size () < 2)
      {
        return false;
      }
    return packets[packets.size () - 1].frame_no != packets[packets.size () - 2].frame_no;
  }

  Time
  Age ()
  {
    return Simulator::Now () - time_created;
  }

  double
  Loss ()
  {
    if (packets.size () < 2)
      {
        return 0;
      }
    uint32_t expected = packets[packets.size () - 1].seq_no - packets[0].seq_no + 1;
    uint32_t received = packets.size ();
    return 1.0 - (double) received / expected;
  }

  double
  AverageOneWayDelay ()
  {
    if (packets.size () < 1)
      {
        return 0;
      }
    Time sum = Time (0);
    for (size_t i = 0; i < packets.size (); i++)
      {
        auto delay = packets[i].time_received - packets[i].time_sent;
        if (delay < Time (0))
          {
            NS_FATAL_ERROR ("Negative one way delay detected: "
                            << delay << " for packet " << i << " with seq_no " << packets[i].seq_no
                            << " and frame_no " << packets[i].frame_no << " sent at "
                            << packets[i].time_sent << " and received at "
                            << packets[i].time_received << " with size " << packets[i].size
                            << " bytes.");
          }
        sum += delay;
      }
    return sum.GetSeconds () / (packets.size ());
  }
};

/**
 * Enum for the congestion control phase
 */
enum class CongestionControlPhase : uint8_t {
  STARTUP,
  CONGESTION_AVOIDANCE,
};

} // namespace ns3

#endif // WEBRTC_CC_TYPES_H