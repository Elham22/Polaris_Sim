/*
 *  Copyright (c) 2018 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef MODULES_CONGESTION_CONTROLLER_GOOG_CC_LOSS_BASED_BANDWIDTH_ESTIMATION_H_
#define MODULES_CONGESTION_CONTROLLER_GOOG_CC_LOSS_BASED_BANDWIDTH_ESTIMATION_H_

#include <vector>

// #include "api/field_trials_view.h"
// #include "api/transport/network_types.h"
// #include "api/units/data_rate.h"
// #include "api/units/time_delta.h"
// #include "api/units/timestamp.h"
// #include "rtc_base/experiments/field_trial_parser.h"


#include "src/core/model/nstime.h"
// #include "src/network/utils/data-rate.h"
#include "src/SCION/model/webrtc/api/transport/network_types.h"
#include "src/SCION/model/webrtc/types.h"

namespace ns3 {


struct LossBasedControlConfig {
  // explicit LossBasedControlConfig(const FieldTrialsView* key_value_config);
  LossBasedControlConfig();
  LossBasedControlConfig(const LossBasedControlConfig&);
  LossBasedControlConfig& operator=(const LossBasedControlConfig&) = default;
  ~LossBasedControlConfig();
  bool enabled;
  // FieldTrialParameter<double> min_increase_factor;
  // FieldTrialParameter<double> max_increase_factor;
  // FieldTrialParameter<TimeDelta> increase_low_rtt;
  // FieldTrialParameter<TimeDelta> increase_high_rtt;
  // FieldTrialParameter<double> decrease_factor;
  // FieldTrialParameter<TimeDelta> loss_window;
  // FieldTrialParameter<TimeDelta> loss_max_window;
  // FieldTrialParameter<TimeDelta> acknowledged_rate_max_window;
  // FieldTrialParameter<DataRate> increase_offset;
  // FieldTrialParameter<DataRate> loss_bandwidth_balance_increase;
  // FieldTrialParameter<DataRate> loss_bandwidth_balance_decrease;
  // FieldTrialParameter<DataRate> loss_bandwidth_balance_reset;
  // FieldTrialParameter<double> loss_bandwidth_balance_exponent;
  // FieldTrialParameter<bool> allow_resets;
  // FieldTrialParameter<TimeDelta> decrease_interval;
  // FieldTrialParameter<TimeDelta> loss_report_timeout;


  // Set default values taken from constructor in loss_based_bandwidth_estimation.cc:

      // min_increase_factor("min_incr", 1.02),
      // max_increase_factor("max_incr", 1.08),
      // increase_low_rtt("incr_low_rtt", TimeDelta::Millis(200)),
      // increase_high_rtt("incr_high_rtt", TimeDelta::Millis(800)),
      // decrease_factor("decr", 0.99),
      // loss_window("loss_win", TimeDelta::Millis(800)),
      // loss_max_window("loss_max_win", TimeDelta::Millis(800)),
      // acknowledged_rate_max_window("ackrate_max_win", TimeDelta::Millis(800)),
      // increase_offset("incr_offset", DataRate::BitsPerSec(1000)),
      // loss_bandwidth_balance_increase("balance_incr",
      //                                 DataRate::KilobitsPerSec(0.5)),
      // loss_bandwidth_balance_decrease("balance_decr",
      //                                 DataRate::KilobitsPerSec(4)),
      // loss_bandwidth_balance_reset("balance_reset",
      //                              DataRate::KilobitsPerSec(0.1)),
      // loss_bandwidth_balance_exponent("exponent", 0.5),
      // allow_resets("resets", false),
      // decrease_interval("decr_intvl", TimeDelta::Millis(300)),
      // loss_report_timeout("timeout", TimeDelta::Millis(6000)) {
      double min_increase_factor = 1.02;
      double max_increase_factor = 1.08;
      TimeDelta increase_low_rtt = TimeDelta::Millis(200);
      TimeDelta increase_high_rtt = TimeDelta::Millis(800);
      double decrease_factor = 0.99;
      TimeDelta loss_window = TimeDelta::Millis(800);
      TimeDelta loss_max_window = TimeDelta::Millis(800);
      TimeDelta acknowledged_rate_max_window = TimeDelta::Millis(800);
      BitRate increase_offset = BitRate::BitsPerSec(1000);
      BitRate loss_bandwidth_balance_increase = BitRate::KilobitsPerSec(0.5);
      BitRate loss_bandwidth_balance_decrease = BitRate::KilobitsPerSec(4);
      BitRate loss_bandwidth_balance_reset = BitRate::KilobitsPerSec(0.1);
      double loss_bandwidth_balance_exponent = 0.5;
      bool allow_resets = false;
      TimeDelta decrease_interval = TimeDelta::Millis(300);
      TimeDelta loss_report_timeout = TimeDelta::Millis(6000);
};

// Estimates an upper BWE limit based on loss.
// It requires knowledge about lost packets and acknowledged bitrate.
// Ie, this class require transport feedback.
class LossBasedBandwidthEstimation {
 public:
  // explicit LossBasedBandwidthEstimation(
  //     const FieldTrialsView* key_value_config);
  LossBasedBandwidthEstimation();

  // Returns the new estimate.
  BitRate Update(Timestamp at_time,
                  BitRate min_bitrate,
                  BitRate wanted_bitrate,
                  TimeDelta last_round_trip_time);
  void UpdateAcknowledgedBitrate(BitRate acknowledged_bitrate,
                                 Timestamp at_time);
  void Initialize(BitRate bitrate);
  bool Enabled() const { return config_.enabled; }
  // Returns true if LossBasedBandwidthEstimation is enabled and have
  // received loss statistics. Ie, this class require transport feedback.
  bool InUse() const {
    return Enabled() && last_loss_packet_report_.IsFinite();
    // return Enabled() && last_loss_packet_report_ != Seconds(0);
  }
  void UpdateLossStatistics(const std::vector<PacketResult>& packet_results,
                            Timestamp at_time);
  BitRate GetEstimate() const { return loss_based_bitrate_; }

 private:
  friend class GoogCcStatePrinter;
  void Reset(BitRate bitrate);
  double loss_increase_threshold() const;
  double loss_decrease_threshold() const;
  double loss_reset_threshold() const;

  BitRate decreased_bitrate() const;

  const LossBasedControlConfig config_;
  double average_loss_;
  double average_loss_max_;
  BitRate loss_based_bitrate_;
  BitRate acknowledged_bitrate_max_;
  Timestamp acknowledged_bitrate_last_update_;
  Timestamp time_last_decrease_;
  bool has_decreased_since_last_loss_report_;
  Timestamp last_loss_packet_report_;
  double last_loss_ratio_;
};

}  // namespace ns3

#endif  // MODULES_CONGESTION_CONTROLLER_GOOG_CC_LOSS_BASED_BANDWIDTH_ESTIMATION_H_
