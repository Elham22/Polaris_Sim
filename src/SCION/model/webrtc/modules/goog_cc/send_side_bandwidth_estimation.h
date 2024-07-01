/*
 *  Copyright (c) 2012 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 *
 *  FEC and NACK added bitrate is handled outside class
 */

#ifndef MODULES_CONGESTION_CONTROLLER_GOOG_CC_SEND_SIDE_BANDWIDTH_ESTIMATION_H_
#define MODULES_CONGESTION_CONTROLLER_GOOG_CC_SEND_SIDE_BANDWIDTH_ESTIMATION_H_

#include <stdint.h>

#include <deque>
#include <memory>
#include <utility>
#include <vector>

#include "absl/types/optional.h"
// #include "api/field_trials_view.h"
// #include "api/network_state_predictor.h"
// #include "api/transport/network_types.h"
// #include "api/units/data_rate.h"
// #include "api/units/time_delta.h"
// #include "api/units/timestamp.h"
// #include "modules/congestion_controller/goog_cc/loss_based_bandwidth_estimation.h"
// #include "modules/congestion_controller/goog_cc/loss_based_bwe_v2.h"
// #include "rtc_base/experiments/field_trial_parser.h"

#include "src/SCION/model/webrtc/api/network_state_predictor.h"
#include "src/SCION/model/webrtc/modules/goog_cc/loss_based_bandwidth_estimation.h"

namespace ns3 {

class RtcEventLog;

class LinkCapacityTracker {
 public:
  LinkCapacityTracker();
  ~LinkCapacityTracker();
  // Call when a new delay-based estimate is available.
  void UpdateDelayBasedEstimate(Timestamp at_time,
                                BitRate delay_based_bitrate);
  void OnStartingRate(BitRate start_rate);
  void OnRateUpdate(absl::optional<BitRate> acknowledged,
                    BitRate target,
                    Timestamp at_time);
  void OnRttBackoff(BitRate backoff_rate, Timestamp at_time);
  BitRate estimate() const;

 private:
  // FieldTrialParameter<TimeDelta> tracking_rate;
  TimeDelta tracking_rate_ = TimeDelta::Seconds(10); // From LinkCapacityTracker constructor
  double capacity_estimate_bps_ = 0;
  Timestamp last_link_capacity_update_ = Timestamp::MinusInfinity();
  BitRate last_delay_based_estimate_ = BitRate::PlusInfinity();
};

class RttBasedBackoff {
 public:
  // explicit RttBasedBackoff(const FieldTrialsView* key_value_config);
  RttBasedBackoff();
  ~RttBasedBackoff();
  void UpdatePropagationRtt(Timestamp at_time, TimeDelta propagation_rtt);
  bool IsRttAboveLimit() const;

  // FieldTrialFlag disabled_;
  // FieldTrialParameter<TimeDelta> configured_limit_;
  // FieldTrialParameter<double> drop_fraction_;
  // FieldTrialParameter<TimeDelta> drop_interval_;
  // FieldTrialParameter<BitRate> bandwidth_floor_;

  bool disabled_ = false;
  TimeDelta configured_limit_;
  double drop_fraction_;
  TimeDelta drop_interval_;
  BitRate bandwidth_floor_;

 public:
  TimeDelta rtt_limit_;
  Timestamp last_propagation_rtt_update_;
  TimeDelta last_propagation_rtt_;
  Timestamp last_packet_sent_;

 private:
  TimeDelta CorrectedRtt() const;
};

class SendSideBandwidthEstimation {
 public:
  SendSideBandwidthEstimation() = delete;
  // SendSideBandwidthEstimation(const FieldTrialsView* key_value_config,
  //                             RtcEventLog* event_log);
  SendSideBandwidthEstimation();
  ~SendSideBandwidthEstimation();

  void OnRouteChange();

  BitRate target_rate() const;
  // LossBasedState loss_based_state() const; // <- Only used for V2
  // Return whether the current rtt is higher than the rtt limited configured in
  // RttBasedBackoff.
  bool IsRttAboveLimit() const;
  uint8_t fraction_loss() const { return last_fraction_loss_; }
  TimeDelta round_trip_time() const { return last_round_trip_time_; }

  BitRate GetEstimatedLinkCapacity() const;
  // Call periodically to update estimate.
  void UpdateEstimate(Timestamp at_time);
  void OnSentPacket(const SentPacket& sent_packet);
  void UpdatePropagationRtt(Timestamp at_time, TimeDelta propagation_rtt);

  // Call when we receive a RTCP message with TMMBR or REMB.
  void UpdateReceiverEstimate(Timestamp at_time, BitRate bandwidth);

  // Call when a new delay-based estimate is available.
  void UpdateDelayBasedEstimate(Timestamp at_time, BitRate bitrate);

  // Call when we receive a RTCP message with a ReceiveBlock.
  void UpdatePacketsLost(int64_t packets_lost,
                         int64_t number_of_packets,
                         Timestamp at_time);

  // Call when we receive a RTCP message with a ReceiveBlock.
  void UpdateRtt(TimeDelta rtt, Timestamp at_time);

  void SetBitrates(absl::optional<BitRate> send_bitrate,
                   BitRate min_bitrate,
                   BitRate max_bitrate,
                   Timestamp at_time);
  void SetSendBitrate(BitRate bitrate, Timestamp at_time);
  void SetMinMaxBitrate(BitRate min_bitrate, BitRate max_bitrate);
  int GetMinBitrate() const;
  void SetAcknowledgedRate(absl::optional<BitRate> acknowledged_rate,
                           Timestamp at_time);
  void UpdateLossBasedEstimator(const TransportPacketsFeedback& report,
                                BandwidthUsage delay_detector_state,
                                absl::optional<BitRate> probe_bitrate,
                                bool in_alr);
  bool PaceAtLossBasedEstimate() const;

 private:
  friend class GoogCcStatePrinter;

  enum UmaState { kNoUpdate, kFirstDone, kDone };

  bool IsInStartPhase(Timestamp at_time) const;

  void UpdateUmaStatsPacketsLost(Timestamp at_time, int packets_lost);

  // Updates history of min bitrates.
  // After this method returns min_bitrate_history_.front().second contains the
  // min bitrate used during last kBweIncreaseIntervalMs.
  void UpdateMinHistory(Timestamp at_time);

  // Gets the upper limit for the target bitrate. This is the minimum of the
  // delay based limit, the receiver limit and the loss based controller limit.
  BitRate GetUpperLimit() const;
  // Prints a warning if `bitrate` if sufficiently long time has past since last
  // warning.
  void MaybeLogLowBitrateWarning(BitRate bitrate, Timestamp at_time);
  // Stores an update to the event log if the loss rate has changed, the target
  // has changed, or sufficient time has passed since last stored event.
  void MaybeLogLossBasedEvent(Timestamp at_time);

  // Cap `bitrate` to [min_bitrate_configured_, max_bitrate_configured_] and
  // set `current_bitrate_` to the capped value and updates the event log.
  void UpdateTargetBitrate(BitRate bitrate, Timestamp at_time);
  // Applies lower and upper bounds to the current target rate.
  // TODO(srte): This seems to be called even when limits haven't changed, that
  // should be cleaned up.
  void ApplyTargetLimits(Timestamp at_time);

  bool LossBasedBandwidthEstimatorV1Enabled() const;
  bool LossBasedBandwidthEstimatorV2Enabled() const;

  bool LossBasedBandwidthEstimatorV1ReadyForUse() const;
  bool LossBasedBandwidthEstimatorV2ReadyForUse() const;

  // const FieldTrialsView* key_value_config_;
  RttBasedBackoff rtt_backoff_;
  LinkCapacityTracker link_capacity_;

  std::deque<std::pair<Timestamp, BitRate> > min_bitrate_history_;

  // incoming filters
  int lost_packets_since_last_loss_update_;
  int expected_packets_since_last_loss_update_;

  absl::optional<BitRate> acknowledged_rate_;
  BitRate current_target_;
  BitRate last_logged_target_;
  BitRate min_bitrate_configured_;
  BitRate max_bitrate_configured_;
  Timestamp last_low_bitrate_log_;

  bool has_decreased_since_last_fraction_loss_;
  Timestamp last_loss_feedback_;
  Timestamp last_loss_packet_report_;
  uint8_t last_fraction_loss_;
  uint8_t last_logged_fraction_loss_;
  TimeDelta last_round_trip_time_;

  // The max bitrate as set by the receiver in the call. This is typically
  // signalled using the REMB RTCP message and is used when we don't have any
  // send side delay based estimate.
  BitRate receiver_limit_;
  BitRate delay_based_limit_;
  Timestamp time_last_decrease_;
  Timestamp first_report_time_;
  int initially_lost_packets_;
  BitRate bitrate_at_2_seconds_;
  UmaState uma_update_state_;
  UmaState uma_rtt_state_;
  std::vector<bool> rampup_uma_stats_updated_;
  // RtcEventLog* const event_log_;
  Timestamp last_rtc_event_log_;
  float low_loss_threshold_;
  float high_loss_threshold_;
  BitRate bitrate_threshold_;
  LossBasedBandwidthEstimation loss_based_bandwidth_estimator_v1_;
  // std::unique_ptr<LossBasedBweV2> loss_based_bandwidth_estimator_v2_;
  // LossBasedState loss_based_state_;
  // FieldTrialFlag disable_receiver_limit_caps_only_;
  bool disable_receiver_limit_caps_only_;
};
}  // namespace ns3
#endif  // MODULES_CONGESTION_CONTROLLER_GOOG_CC_SEND_SIDE_BANDWIDTH_ESTIMATION_H_
