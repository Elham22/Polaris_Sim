#include <deque>
#include <algorithm>
#include "aimd_rate_control.h"

namespace ns3 {

Time kDefaultRtt = MilliSeconds (200);
double kDefaultBackoffFactor = 0.85;

AimdRateControl::AimdRateControl ()
    : min_configured_bitrate_ (kCongestionControllerMinBitrate),
      max_configured_bitrate_ (BitRate::KilobitsPerSec(30000)),
      current_bitrate_ (max_configured_bitrate_),
      latest_estimated_throughput_ (current_bitrate_),
      link_capacity_ (),
      rate_control_state_ (RateControlState::kRcHold),
      time_last_bitrate_change_ (Seconds (0)),
      time_last_bitrate_decrease_ (Seconds (0)),
      time_first_throughput_estimate_ (Seconds (0)),
      bitrate_is_initialized_ (false),
      beta_ (kDefaultBackoffFactor),
      in_alr_ (false),
      rtt_ (kDefaultRtt),
      send_side_ (false),
      no_bitrate_increase_in_alr_ (false)
{
}

void
AimdRateControl::SetStartBitrate (BitRate start_bitrate)
{
  current_bitrate_ = start_bitrate;
  latest_estimated_throughput_ = current_bitrate_;
  bitrate_is_initialized_ = true;
}

void
AimdRateControl::SetMinBitrate (BitRate min_bitrate)
{
  min_configured_bitrate_ = min_bitrate;
  current_bitrate_ = std::max (min_bitrate, current_bitrate_);
}

bool
AimdRateControl::ValidEstimate () const
{
  return bitrate_is_initialized_;
}

bool
AimdRateControl::TimeToReduceFurther (Time at_time, BitRate estimated_throughput) const
{
  // const Time bitrate_reduction_interval =
  //     rtt_.Clamped (TimeDelta::Millis (10), TimeDelta::Millis (200));
  Time bitrate_reduction_interval = std::clamp (rtt_, MilliSeconds (10), MilliSeconds (200));

  if (at_time - time_last_bitrate_change_ >= bitrate_reduction_interval)
    {
      return true;
    }
  if (ValidEstimate ())
    {
      // TODO(terelius/holmer): Investigate consequences of increasing
      // the threshold to 0.95 * LatestEstimate().
      const BitRate threshold = LatestEstimate () * 0.5;
      return estimated_throughput < threshold;
    }
  return false;
}

bool
AimdRateControl::InitialTimeToReduceFurther (Time at_time) const
{
  // return ValidEstimate () &&
  //        TimeToReduceFurther (at_time, LatestEstimate () / 2 - double ::BitsPerSec (1));
  return ValidEstimate () &&
         TimeToReduceFurther (at_time, BitRate (LatestEstimate () / 2 - BitRate::BitsPerSec(1)));
}

BitRate
AimdRateControl::LatestEstimate () const
{
  return current_bitrate_;
}

void
AimdRateControl::SetRtt (Time rtt)
{
  rtt_ = rtt;
}

BitRate
AimdRateControl::Update (BandwidthUsage bw_state, BitRate estimated_throughput, Time at_time)
{
  // Set the initial bit rate value to what we're receiving the first half
  // second.
  // TODO(bugs.webrtc.org/9379): The comment above doesn't match to the code.
  if (!bitrate_is_initialized_)
    {
      const Time kInitializationTime = Seconds (5);
      if (time_first_throughput_estimate_ == Seconds (0))
        {
          time_first_throughput_estimate_ = at_time;
        }
      else if (at_time - time_first_throughput_estimate_ > kInitializationTime)
        {
          current_bitrate_ = estimated_throughput;
          bitrate_is_initialized_ = true;
        }
    }

  ChangeBitrate (bw_state, estimated_throughput, at_time);
  return current_bitrate_;
}

void
AimdRateControl::SetInApplicationLimitedRegion (bool in_alr)
{
  in_alr_ = in_alr;
}

void
AimdRateControl::SetEstimate (BitRate bitrate, Time at_time)
{
  bitrate_is_initialized_ = true;
  BitRate prev_bitrate = current_bitrate_;
  current_bitrate_ = ClampBitrate (bitrate);
  time_last_bitrate_change_ = at_time;
  if (current_bitrate_ < prev_bitrate)
    {
      time_last_bitrate_decrease_ = at_time;
    }
}

double
AimdRateControl::GetNearMaxIncreaseRateBpsPerSecond () const
{
  // const Time kFrameInterval = Seconds (1) / 30;
  // DataSize frame_size = current_bitrate_ * kFrameInterval;
  // const DataSize kPacketSize = DataSize::Bytes (1200);
  // double packets_per_frame = std::ceil (frame_size / kPacketSize);
  // DataSize avg_packet_size = frame_size / packets_per_frame;

  // Approximate the over-use estimator delay to 100 ms.
  Time response_time = rtt_ + MilliSeconds (100);

  response_time = response_time * 2;
  // double increase_rate_bps_per_second = (avg_packet_size / response_time).bps<double> ();
  double increase_rate_bps_per_second = (1500 * 8 / response_time.GetSeconds ());
  double kMinIncreaseRateBpsPerSecond = 4000;
  return std::max (kMinIncreaseRateBpsPerSecond, increase_rate_bps_per_second);
}

Time
AimdRateControl::GetExpectedBandwidthPeriod () const
{
  const Time kMinPeriod = Seconds (2);
  const Time kDefaultPeriod = Seconds (3);
  const Time kMaxPeriod = Seconds (50);

  double increase_rate_bps_per_second = GetNearMaxIncreaseRateBpsPerSecond ();
  if (!last_decrease_.has_value ())
    return kDefaultPeriod;

  double time_to_recover_decrease_seconds =
      last_decrease_->bps() / increase_rate_bps_per_second;
  Time period = Seconds (time_to_recover_decrease_seconds);
  // return period.Clamped (kMinPeriod, kMaxPeriod);
  return std::clamp (period, kMinPeriod, kMaxPeriod);
}

void
AimdRateControl::ChangeBitrate (BandwidthUsage bw_state, BitRate estimated_throughput,
                                Time at_time)
{

  // Print current_bitrate_ and new_bitrate
  std::cout << "AIMD Rate Control:" << std::endl;
  std::cout << "  Current Bitrate: " << current_bitrate_ << std::endl;
  std::cout << "  Est. throughput: " << estimated_throughput << std::endl;
  if (link_capacity_.has_estimate ())
    {
      std::cout << "  link capacity  : " << link_capacity_.estimate () << std::endl;
    }
  else
    {
      std::cout << "  link capacity  : None" << std::endl;
    }
  std::cout << "  Upper Bound    : " << link_capacity_.UpperBound () << std::endl;
  std::cout << "  Lower Bound    : " << link_capacity_.LowerBound () << std::endl;

  absl::optional<BitRate> new_bitrate;
  // BitRate estimated_throughput =
  //     input.estimated_throughput.value_or(latest_estimated_throughput_);
  // if (input.estimated_throughput)
  //   latest_estimated_throughput_ = *input.estimated_throughput;

  // TODO: ^ not sure what they are trying to do here. Is the estimated_throughput param optional?
  BitRate estimated_throughput_ = estimated_throughput;

  // An over-use should always trigger us to reduce the bitrate, even though
  // we have not yet established our first estimate. By acting on the over-use,
  // we will end up with a valid estimate.
  if (!bitrate_is_initialized_ && bw_state != BandwidthUsage::kBwOverusing)
    return;

  ChangeState (bw_state, at_time);

  switch (rate_control_state_)
    {
    case RateControlState::kRcHold:
      break;

      case RateControlState::kRcIncrease: {
        if (estimated_throughput_ > link_capacity_.UpperBound ())
          {
            std::cout << "Estimated throughput is greater than link capacity upper bound: "
                      << estimated_throughput_ << " > " << link_capacity_.UpperBound ()
                      << std::endl;
            link_capacity_.Reset ();
          }

        // We limit the new bitrate based on the troughput to avoid unlimited
        // bitrate increases. We allow a bit more lag at very low rates to not too
        // easily get stuck if the encoder produces uneven outputs.

        BitRate increase_limit = 1.5 * estimated_throughput + BitRate ::KilobitsPerSec (10);
        // BitRate increase_limit = estimated_throughput_ * 1.5 + BitRate ("10kbps");

        if (send_side_ && in_alr_ && no_bitrate_increase_in_alr_)
          {
            // Do not increase the delay based estimate in alr since the estimator
            // will not be able to get transport feedback necessary to detect if
            // the new estimate is correct.
            // If we have previously increased above the limit (for instance due to
            // probing), we don't allow further changes.
            increase_limit = current_bitrate_;
          }

        if (current_bitrate_ < increase_limit)
          {
            BitRate increased_bitrate = BitRate::MinusInfinity ();
            // BitRate increased_bitrate = BitRate ("0kbps");
            if (link_capacity_.has_estimate () && current_bitrate_ > link_capacity_.LowerBound ())
              {
                // The link_capacity estimate is reset if the measured throughput
                // is too far from the estimate. We can therefore assume that our
                // target rate is reasonably close to link capacity and use additive
                // increase.
                // TODO(wickip): ^ This is not true after the very first reset.
                BitRate additive_increase =
                    AdditiveRateIncrease (at_time, time_last_bitrate_change_);
                increased_bitrate = current_bitrate_ + additive_increase;
                std::cout << "Additive Increase: " << additive_increase << std::endl;
              }
            else
              {
                // If we don't have an estimate of the link capacity, use faster ramp
                // up to discover the capacity.
                std::cout << "Calling mult increase with params " << at_time << ", "
                          << time_last_bitrate_change_ << ", " << current_bitrate_ << std::endl;
                BitRate multiplicative_increase = MultiplicativeRateIncrease (
                    at_time, time_last_bitrate_change_, current_bitrate_);

                increased_bitrate = current_bitrate_ + multiplicative_increase;
                std::cout << "Multiplicative Increase: " << multiplicative_increase << std::endl;
              }
            new_bitrate = std::min (increased_bitrate, increase_limit);
          }
        time_last_bitrate_change_ = at_time;
        break;
      }

      case RateControlState::kRcDecrease: {
        BitRate decreased_bitrate = std::numeric_limits<uint64_t>::max ();

        // Set bit rate to something slightly lower than the measured throughput
        // to get rid of any self-induced delay.
        decreased_bitrate = estimated_throughput_ * beta_;
        // if (decreased_bitrate > BitRate ("5kbps"))
        //   {
        //     decreased_bitrate -= BitRate ("5kbps");
        //   }
        if (decreased_bitrate > BitRate::KilobitsPerSec(5)) {
          decreased_bitrate -= BitRate::KilobitsPerSec(5);
        }

        if (decreased_bitrate > current_bitrate_)
          {
            // TODO(terelius): The link_capacity estimate may be based on old
            // throughput measurements. Relying on them may lead to unnecessary
            // BWE drops.
            if (link_capacity_.has_estimate ())
              {
                decreased_bitrate = link_capacity_.estimate () * beta_;
              }
          }
        // Avoid increasing the rate when over-using.
        if (decreased_bitrate < current_bitrate_)
          {
            new_bitrate = decreased_bitrate;
          }

        if (bitrate_is_initialized_ && estimated_throughput_ < current_bitrate_)
          {
            if (!new_bitrate.has_value ())
              {
                last_decrease_ = BitRate::Zero();
              }
            else
              {
                last_decrease_ = current_bitrate_ - *new_bitrate;
              }
          }
        if (estimated_throughput_ < link_capacity_.LowerBound ())
          {
            // The current throughput is far from the estimated link capacity. Clear
            // the estimate to allow an immediate update in OnOveruseDetected.
            link_capacity_.Reset ();
            std::cout << "Estimated throughput less than lower bound: "
                      << estimated_throughput_ << " < " << link_capacity_.LowerBound ()
                      << std::endl;
          }
        else
          {
            std::cout << "Estimated throughput NOT less than lower bound: "
                      << estimated_throughput_ << " >= " << link_capacity_.LowerBound ()
                      << std::endl;
          }

        bitrate_is_initialized_ = true;
        link_capacity_.OnOveruseDetected (estimated_throughput_);
        // Stay on hold until the pipes are cleared.
        rate_control_state_ = RateControlState::kRcHold;
        time_last_bitrate_change_ = at_time;
        time_last_bitrate_decrease_ = at_time;
        break;
      }
    }
  std::cout << "  Bandwidth Usage: " << BandwidthUsageToString (bw_state) << std::endl;
  current_bitrate_ = ClampBitrate (new_bitrate.value_or (current_bitrate_));
  std::cout << "  New Bitrate    : " << current_bitrate_ << std::endl;
}

BitRate
AimdRateControl::ClampBitrate (BitRate new_bitrate) const
{
  // if (!disable_estimate_bounded_increase_ && network_estimate_ &&
  //     network_estimate_->link_capacity_upper.IsFinite()) {
  //   BitRate upper_bound =
  //       use_current_estimate_as_min_upper_bound_
  //           ? std::max(network_estimate_->link_capacity_upper, current_bitrate_)
  //           : network_estimate_->link_capacity_upper;
  //   new_bitrate = std::min(upper_bound, new_bitrate);
  // }
  // if (network_estimate_ && network_estimate_->link_capacity_lower.IsFinite() &&
  //     new_bitrate < current_bitrate_) {
  //   new_bitrate = std::min(
  //       current_bitrate_,
  //       std::max(new_bitrate, network_estimate_->link_capacity_lower * beta_));
  // }
  new_bitrate = std::max (new_bitrate, min_configured_bitrate_);
  return new_bitrate;
}

BitRate
AimdRateControl::MultiplicativeRateIncrease (Time at_time, Time last_time,
                                             BitRate current_bitrate) const
{
  double alpha = 1.08;
  if (last_time.GetSeconds () != 0)
    {
      auto time_since_last_update = at_time - last_time;
      std::cout << "Time Since Last Update: " << time_since_last_update.GetSeconds () << std::endl;

      // if 0, print both components
      if (time_since_last_update.GetSeconds () == 0)
        {
          std::cout << "at_time: " << at_time << std::endl;
          std::cout << "last_time: " << last_time << std::endl;
        }

      alpha = pow (alpha, std::min (time_since_last_update.GetSeconds (), 1.0));
    }
  std::cout << "Alpha: " << alpha << std::endl;
  BitRate multiplicative_increase =
      std::max(current_bitrate * (alpha - 1.0), BitRate::BitsPerSec(1000));
  return multiplicative_increase;
}

BitRate
AimdRateControl::AdditiveRateIncrease (Time at_time, Time last_time) const
{
  double time_period_seconds = (at_time - last_time).GetSeconds ();
  BitRate data_rate_increase_bps =
      BitRate (GetNearMaxIncreaseRateBpsPerSecond () * time_period_seconds);
  return data_rate_increase_bps;
}

void
AimdRateControl::ChangeState (BandwidthUsage bw_state, Time at_time)
{
  switch (bw_state)
    {
    case BandwidthUsage::kBwNormal:
      if (rate_control_state_ == RateControlState::kRcHold)
        {
          time_last_bitrate_change_ = at_time;
          rate_control_state_ = RateControlState::kRcIncrease;
        }
      break;
    case BandwidthUsage::kBwOverusing:
      if (rate_control_state_ != RateControlState::kRcDecrease)
        {
          rate_control_state_ = RateControlState::kRcDecrease;
        }
      break;
    case BandwidthUsage::kBwUnderusing:
      rate_control_state_ = RateControlState::kRcHold;
      break;
    }
}

} // namespace ns3
