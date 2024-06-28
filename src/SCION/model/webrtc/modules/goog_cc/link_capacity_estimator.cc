/*
 *  Copyright 2018 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */
#include "link_capacity_estimator.h"

#include <algorithm>
#include <cmath>

namespace ns3 {
LinkCapacityEstimator::LinkCapacityEstimator() {}

BitRate LinkCapacityEstimator::UpperBound() const {
  if (estimate_kbps_.has_value())
    return BitRate::KilobitsPerSec(estimate_kbps_.value() +
                                    3 * deviation_estimate_kbps());
  return BitRate::Infinity();
}

BitRate LinkCapacityEstimator::LowerBound() const {
  if (estimate_kbps_.has_value())
    return BitRate::KilobitsPerSec(
        std::max(0.0, estimate_kbps_.value() - 3 * deviation_estimate_kbps()));
  return BitRate::Zero();
}

void
LinkCapacityEstimator::Reset ()
{
  estimate_kbps_.reset ();
}

void
LinkCapacityEstimator::OnOveruseDetected (BitRate acknowledged_rate)
{
  std::cout << "LinkCapacityEstimator::OnOveruseDetected" << std::endl;
  Update (acknowledged_rate, 0.05);
}

void LinkCapacityEstimator::OnProbeRate(BitRate probe_rate) {
  Update(probe_rate, 0.5);
}

void
LinkCapacityEstimator::Update (BitRate capacity_sample, double alpha)
{
  // // Convert sample from B/s to kbit/s.
  // double sample_kbps = capacity_sample.GetBitRate () / 1000.0;
  // if (!estimate_kbps_.has_value ())
  //   {
  //     estimate_kbps_ = sample_kbps;
  //   }
  // else
  //   {
  //     estimate_kbps_ = (1 - alpha) * estimate_kbps_.value () + alpha * sample_kbps;
  //   }

  double sample_kbps = capacity_sample.kbps();
  if (!estimate_kbps_.has_value()) {
    estimate_kbps_ = sample_kbps;
  } else {
    estimate_kbps_ = (1 - alpha) * estimate_kbps_.value() + alpha * sample_kbps;
  }
  // Estimate the variance of the link capacity estimate and normalize the
  // variance with the link capacity estimate.
  const double norm = std::max(estimate_kbps_.value(), 1.0);
  double error_kbps = estimate_kbps_.value() - sample_kbps;
  deviation_kbps_ =
      (1 - alpha) * deviation_kbps_ + alpha * error_kbps * error_kbps / norm;
  // 0.4 ~= 14 kbit/s at 500 kbit/s
  // 2.5f ~= 35 kbit/s at 500 kbit/s
  // deviation_kbps_ = rtc::SafeClamp (deviation_kbps_, 0.4f, 2.5f);
  deviation_kbps_ = std::min (2.5, std::max (0.4, deviation_kbps_));
}

bool LinkCapacityEstimator::has_estimate() const {
  return estimate_kbps_.has_value();
}

BitRate LinkCapacityEstimator::estimate() const {
  return BitRate::KilobitsPerSec(*estimate_kbps_);
}

double LinkCapacityEstimator::deviation_estimate_kbps() const {
  // Calculate the max bit rate std dev given the normalized
  // variance and the current throughput bitrate. The standard deviation will
  // only be used if estimate_kbps_ has a value.
  return sqrt(deviation_kbps_ * estimate_kbps_.value());
}

} // namespace ns3