/*
 *  Copyright 2018 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */
#ifndef MODULES_CONGESTION_CONTROLLER_GOOG_CC_LINK_CAPACITY_ESTIMATOR_H_
#define MODULES_CONGESTION_CONTROLLER_GOOG_CC_LINK_CAPACITY_ESTIMATOR_H_

#include "absl/types/optional.h"

// #include "src/network/utils/data-rate.h"
#include "src/SCION/model/webrtc/types.h"

namespace ns3 {
class LinkCapacityEstimator
{
public:
  LinkCapacityEstimator ();

  // In kbps.
  BitRate UpperBound () const;

  BitRate LowerBound () const;
  void Reset ();
  void OnOveruseDetected (BitRate acknowledged_rate);
  void OnProbeRate (BitRate probe_rate);
  bool has_estimate () const;
  BitRate estimate () const;

private:
  void Update (BitRate capacity_sample, double alpha);

  double deviation_estimate_kbps () const;
  absl::optional<double> estimate_kbps_;
  double deviation_kbps_ = 0.4;
};

} // namespace ns3

#endif // MODULES_CONGESTION_CONTROLLER_GOOG_CC_LINK_CAPACITY_ESTIMATOR_H_
