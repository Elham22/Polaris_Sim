/*
 *  Copyright (c) 2014 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef MODULES_REMOTE_BITRATE_ESTIMATOR_AIMD_RATE_CONTROL_H_
#define MODULES_REMOTE_BITRATE_ESTIMATOR_AIMD_RATE_CONTROL_H_

#include <stdint.h>

#include "absl/types/optional.h"

#include "src/core/model/nstime.h"

// #include "src/network/utils/data-rate.h"

#include "src/SCION/model/webrtc-cc/types.h"
#include "src/SCION/model/webrtc-cc/link_capacity_estimator.h"

namespace ns3 {

const BitRate kCongestionControllerMinBitrate = BitRate::BitsPerSec(5'000);

// A rate control implementation based on additive increases of
// bitrate when no over-use is detected and multiplicative decreases when
// over-uses are detected. When we think the available bandwidth has changes or
// is unknown, we will switch to a "slow-start mode" where we increase
// multiplicatively.
class AimdRateControl
{
public:
  // constructor
  AimdRateControl ();

  // Returns true if the target bitrate has been initialized. This happens
  // either if it has been explicitly set via SetStartBitrate/SetEstimate, or if
  // we have measured a throughput.
  bool ValidEstimate () const;
  void SetStartBitrate (BitRate start_bitrate);
  void SetMinBitrate (BitRate min_bitrate);
  // Time GetFeedbackInterval() const; // Unused

  // Returns true if the bitrate estimate hasn't been changed for more than
  // an RTT, or if the estimated_throughput is less than half of the current
  // estimate. Should be used to decide if we should reduce the rate further
  // when over-using.
  bool TimeToReduceFurther (Time at_time, BitRate estimated_throughput) const;
  // As above. To be used if overusing before we have measured a throughput.
  bool InitialTimeToReduceFurther (Time at_time) const;

  BitRate LatestEstimate () const;
  void SetRtt (Time rtt);
  BitRate Update (BandwidthUsage input, BitRate estimated_throughput, Time at_time);
  void SetInApplicationLimitedRegion (bool in_alr);
  void SetEstimate (BitRate bitrate, Time at_time);
  // void SetNetworkStateEstimate(
  //     const absl::optional<NetworkStateEstimate>& estimate); // Unused

  // Returns the increase rate when used bandwidth is near the link capacity.
  double GetNearMaxIncreaseRateBpsPerSecond () const;
  // Returns the expected time between overuse signals (assuming steady state).
  Time GetExpectedBandwidthPeriod () const;

private:
  enum class RateControlState { kRcHold, kRcIncrease, kRcDecrease };

  // Update the target bitrate based on, among other things, the current rate
  // control state, the current target bitrate and the estimated throughput.
  // When in the "increase" state the bitrate will be increased either
  // additively or multiplicatively depending on the rate control region. When
  // in the "decrease" state the bitrate will be decreased to slightly below the
  // current throughput. When in the "hold" state the bitrate will be kept
  // constant to allow built up queues to drain.
  void ChangeBitrate (BandwidthUsage input, BitRate estimated_throughput, Time at_time);

  BitRate ClampBitrate (BitRate new_bitrate) const;
  BitRate MultiplicativeRateIncrease (Time at_time, Time last_ms, BitRate current_bitrate) const;
  BitRate AdditiveRateIncrease (Time at_time, Time last_time) const;
  void UpdateChangePeriod (Time at_time);
  void ChangeState (BandwidthUsage input, Time at_time);

  BitRate min_configured_bitrate_;
  BitRate max_configured_bitrate_;
  BitRate current_bitrate_;
  BitRate latest_estimated_throughput_;
  LinkCapacityEstimator link_capacity_;
  RateControlState rate_control_state_;
  Time time_last_bitrate_change_;
  Time time_last_bitrate_decrease_;
  Time time_first_throughput_estimate_;
  bool bitrate_is_initialized_;
  double beta_;
  bool in_alr_;
  Time rtt_;
  const bool send_side_;
  // Allow the delay based estimate to only increase as long as application
  // limited region (alr) is not detected.
  const bool no_bitrate_increase_in_alr_;
  absl::optional<BitRate> last_decrease_;
};
} // namespace ns3

#endif // MODULES_REMOTE_BITRATE_ESTIMATOR_AIMD_RATE_CONTROL_H_
