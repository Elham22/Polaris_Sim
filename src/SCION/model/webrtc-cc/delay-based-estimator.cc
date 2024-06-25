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

#ifndef WEBRTC_CC_DELAY_BASED_ESTIMATOR_H
#define WEBRTC_CC_DELAY_BASED_ESTIMATOR_H

#include <deque>
#include "src/core/model/simulator.h"
#include "src/SCION/model/webrtc-cc/types.h"
#include "src/SCION/model/scion-packet.h"
#include "src/SCION/model/apps/controller-state.h"
#include "src/SCION/model/externs.h"

namespace ns3 {

/**
 * A delay-based bandwidth estimator based on GCC
 * Sources:
 * https://c3lab.poliba.it/images/6/65/Gcc-analysis.pdf
 * https://datatracker.ietf.org/doc/html/draft-ietf-rmcat-gcc-02
 * https://webrtc.googlesource.com/src
*/
class DelayBasedController
{

  struct FrameRecord
  {
    uint32_t frame_no;

    app_packet_id_t first_pkt_seq_no; // seq number of the first packet we received for this frame
    Time first_pkt_send_time;
    Time first_pkt_rcv_time;

    app_packet_id_t last_pkt_seq_no; // seq number of the last packet we received for this frame
    Time last_pkt_send_time;
    Time last_pkt_rcv_time;
  };

protected:
  // Parameters
  // https://datatracker.ietf.org/doc/html/draft-ietf-rmcat-gcc-02#section-5.6
  const double BURST_TIME = 5; // in ms
  const double Q = 1e-3; // State noise covariance matrix
  const double e_0 = 0.1; // Initial value of the  system error covariance
  const double chi = 0.05; // Coefficient used  for the measured noise variance
  const double del_var_th_0 = 0.5; // Initial value for the adaptive threshold, in ms
  const double overuse_time_th = 10; // Time required to trigger an overuse signal, in ms
  const double K_u = 0.01; // Coefficient for the adaptive threshold
  const double K_d = 0.0018; // Coefficient for the adaptive threshold
  const Time T = Seconds (1); // Time window for measuring the received bitrate
  const double beta = 0.95; // Smoothing factor for the measurement noise variance

  const double INCREASE_FACTOR_MULT =
      1.05; // Factor to increase rate per second during multiplicative increase
  const double DECREASE_FACTOR_MULT =
      0.85; // Factor to decrease rate per second during multiplicative decrease

  // Time window accross which to measure incoming traffic rate
  const Time receive_rate_window = MilliSeconds (500);

  // State variables
  DetectorSignal signal = DetectorSignal::NORMAL;
  ControllerState state = ControllerState::INCREASE;
  Time overuse_detected_since = Seconds (0);
  bool overuse_detected = false;

  // Initial values
  double adaptive_treshold = del_var_th_0;
  double overuse_treshold = adaptive_treshold;
  double underuse_treshold = -adaptive_treshold;
  double e = e_0; // System error covariance
  double d_m = 0; // Measured one way delay gradient
  double m = 0; // Estimate of the one way delay gradient
  double z = 0; // Residual
  double kalman_gain = 0; // Kalman gain
  double treshold_gain = 0; // Treshold gain
  double measurement_noise_variance = 0; // Variance of the one way delay gradient

  // Record information about frames to compute inter arrival times
  std::map<uint32_t, FrameRecord> frames = std::map<uint32_t, FrameRecord> ();
  FrameRecord *frame_current = nullptr;
  FrameRecord *frame_previous = nullptr;

  std::deque<double> trend_history; // Records one way delays of packets
  std::size_t trend_packets = 60; // How many packets to keep in the history
  double slope_tresh_high = 0.1;
  double slope_tresh_low = 0;

  // This map is used to measure the receiver rate
  std::map<Time, u_int16_t> received_bytes;
  double receive_rate; // Rate of traffic arriving in the last receive_rate_window, in Bytes/s

  double A_r = 0; // Rate estimate computed by the controller, in Bytes/s

  Time last_rate_update = Seconds (0);
  int64_t last_treshold_update_ms = -1;

  CongestionControlPhase phase = CongestionControlPhase::STARTUP;
  Time round_trip_time = CC_INITIAL_RTT;

  /**
   * Returns the measured one way delay gradient in ms
   * d_m (t_i ) = (t_i − t_i−1 ) − (T_i − T_i−1 )
   * Where T_i is the time at which the first packet of the i-th video
   * frame has been sent and t_i is the time at which the last packet that
   * forms the video frame has been received.
  */
  double
  MeasuredOneWayDelayGradient ()
  {
    auto delay_grad = (frame_current->last_pkt_rcv_time - frame_previous->last_pkt_rcv_time) -
                      (frame_current->first_pkt_send_time - frame_previous->first_pkt_send_time);

    double delay_grad_ms = delay_grad.GetSeconds () * 1000.0;
    if (std::abs (delay_grad_ms) > 16)
      {
        std::cout << "High MeasuredOneWayDelayGradient detected:" << std::endl;
        std::cout << "  frame_previous: " << frame_previous->frame_no << std::endl;
        std::cout << "  frame_prev_send_time: " << frame_previous->first_pkt_send_time << std::endl;
        std::cout << "  frame_prev_rcv_time: " << frame_previous->last_pkt_rcv_time << std::endl;
        std::cout << "  frame_current: " << frame_current->frame_no << std::endl;
        std::cout << "  frame_curr_send_time: " << frame_current->first_pkt_send_time << std::endl;
        std::cout << "  frame_curr_rcv_time: " << frame_current->last_pkt_rcv_time << std::endl;
        std::cout << "  t_i - t_i-1: "
                  << (frame_current->last_pkt_rcv_time - frame_previous->last_pkt_rcv_time)
                         .GetMilliSeconds ()
                  << std::endl;
        std::cout << "  T_i - T_i-1: "
                  << (frame_current->first_pkt_send_time - frame_previous->first_pkt_send_time)
                         .GetMilliSeconds ()
                  << std::endl;
        std::cout << "  delay_grad_ms: " << delay_grad_ms << std::endl;
      }

    // return in ms as double
    return delay_grad_ms;
  }

  /**
   * Clean up frame data older than T
  */
  void
  ClearOldFrames ()
  {
    Time now = Simulator::Now ();
    for (auto it = frames.begin (); it != frames.end ();)
      {
        if (now - it->second.last_pkt_rcv_time > T)
          {
            it = frames.erase (it);
          }
        else
          {
            ++it;
          }
      }
  }

  /**
   * Update the gradient estimate along with the adaptive treshold and Kalman gain
  */
  void
  UpdateGradientEstimate ()
  {
    auto m_prev = m;
    auto e_prev = e;

    // Compute measured one way delay gradient
    d_m = MeasuredOneWayDelayGradient ();

    // Compute the residual z(t_i) = d_m (t_i) − m (t_i−1)
    z = d_m - m_prev;

    // Paper (16)
    // Estimate the variance as exp moving average of the squared residuals
    // σ̂^2 (t_i) = β · σ̂^2(t_i−1 ) + (1 − β) · z(t_i)^2
    measurement_noise_variance = beta * measurement_noise_variance + (1 - beta) * z * z;

    // In the draft, they additionally make it at least 1.0
    // var_v_hat(i) = max(alpha * var_v_hat(i-1) + (1-alpha) * z(i)^2, 1)
    // alpha = (1-chi)^(30/(1000 * f_max))
    // We're also just using a fixed factor beta right now, instead of the dynamic alpha
    // measurement_noise_variance = std::max (measurement_noise_variance, 1.0);

    // TODO: limiting the variance seems to help, but needs explaining. Initial
    // idea was just to prevent it from making m almost stuck by decreasing the
    // kalman gain too drastically.
    // measurement_noise_variance = std::min (measurement_noise_variance, 1.0);

    // Update the Kalman gain
    //                    e(i-1) + q(i)
    //  k(i) = ----------------------------------------
    //              var_v_hat(i) + (e(i-1) + q(i))
    kalman_gain = (e_prev + Q) / (measurement_noise_variance + e_prev + Q);

    // Update the system error covariance
    // e(i) = (1 - k(i)) * (e(i-1) + q(i))
    e = (1 - kalman_gain) * (e_prev + Q);

    // Update the gradient estimate
    // m(t_i) = (1 − K(t_i)) · m(t_i−1) + K(t_i) · (d_m (t_i))
    // kalman_gain = 0.6; // TODO: remove this line (for testing only)
    // kalman_gain = std::min (0.90, kalman_gain); // TODO: remove this line (for testing only)
    m = (1 - kalman_gain) * m_prev + kalman_gain * d_m;

    // Update the treshold gain
    // kγ (t_i ) = k_d if |m(t_i )| < γ(t_i−1 )
    // kγ (t_i ) = k_u otherwise
    if (std::abs (m) < adaptive_treshold)
      {
        treshold_gain = K_d;
      }
    else
      {
        treshold_gain = K_u;
      }

    // From the draft:
    // del_var_th(i) SHOULD NOT be updated if this condition holds:
    //  |m(i)| - del_var_th(i) > 15
    // if (std::abs (m) - adaptive_treshold > 15)
    //   {
    //     return;
    //   }

    // Update the adaptive treshold
    // γ(t_i ) = γ(t_i−1 ) + ∆T · kγ (t_i )(|m(t_i )| − γ(t_i−1 ))
    // ∆T = t_i − t_i−1
    double delta_t =
        (frame_current->last_pkt_rcv_time - frame_previous->last_pkt_rcv_time).GetMilliSeconds ();
    adaptive_treshold =
        adaptive_treshold + delta_t * treshold_gain * (std::abs (m) - adaptive_treshold);

    // adaptive_treshold = 0.2; // TODO: still deteriorates if treshold is dynamic
    // From the draft:
    // It is also RECOMMENDED to clamp del_var_th(i) to the range [6, 600],
    // since a too small del_var_th(i) can cause the detector to become overly
    // sensitive.
    // adaptive_treshold = std::max (6.0, std::min (adaptive_treshold, 600.0));

    overuse_treshold = adaptive_treshold;
    underuse_treshold = -adaptive_treshold;
  }

  /**
   * Compute the rate at which we've received packets in the last window and
   * clean up old values
   *
   * @return the current receive rate in Bytes/s over the last receive_rate_window
  */
  void
  UpdateReceiveRate ()
  {
    uint64_t total_bytes = 0;
    Time localtime = Simulator::Now ();
    for (auto it = received_bytes.begin (); it != received_bytes.end ();)
      {
        if (localtime - it->first > receive_rate_window)
          {
            it = received_bytes.erase (it);
          }
        else
          {
            total_bytes += it->second;
            ++it;
          }
      }
    receive_rate = total_bytes / receive_rate_window.GetSeconds ();
  }

  /**
   * Update the signal based on the current gradient estimate and treshold
  */
  void
  UpdateSignal ()
  {
    if (m > overuse_treshold)
      {
        // Only signal overuse if we have been above the threshold for a certain time
        if (overuse_detected)
          {
            if (Simulator::Now () - overuse_detected_since > MilliSeconds (overuse_time_th))
              {
                signal = DetectorSignal::OVERUSE;
                phase = CongestionControlPhase::CONGESTION_AVOIDANCE;
              }
            else
              {
                // Hasn't been long enough yet
              }
          }
        else
          {
            overuse_detected = true;
            overuse_detected_since = Simulator::Now ();
            signal = DetectorSignal::NORMAL;
          }
      }
    else if (m < underuse_treshold)
      {
        overuse_detected = false;
        signal = DetectorSignal::UNDERUSE;
      }
    else // underuse_treshold <= m <= overuse_treshold
      {
        overuse_detected = false;
        signal = DetectorSignal::NORMAL;
      }
  }

  /**
   * Run the FSM
  */
  void
  UpdateStateMachine ()
  {
    switch (state)
      {
      case ControllerState::DECREASE:
        switch (signal)
          {
          case DetectorSignal::OVERUSE:
            // Delay still increasing, so keep lowering rate
            break;
          default: // NORMAL or UNDERUSE
            // Queue is draining, keep rate steady
            state = ControllerState::HOLD;
            break;
          }
        break;
      case ControllerState::HOLD:
        switch (signal)
          {
          case DetectorSignal::OVERUSE:
            // Queue is filling up, so decrease rate
            state = ControllerState::DECREASE;
            break;
          case DetectorSignal::NORMAL:
            // Queue is drained, can start increasing again
            state = ControllerState::INCREASE;
            break;
          case DetectorSignal::UNDERUSE:
            // Queue is draining, keep rate steady
            break;
          }
        break;
      case ControllerState::INCREASE:
        switch (signal)
          {
          case DetectorSignal::OVERUSE:
            // Queue is filling up, so decrease rate
            state = ControllerState::DECREASE;
            break;
          case DetectorSignal::NORMAL:
            // Delay steady, can keep increasing
            break;
          case DetectorSignal::UNDERUSE:
            // Queue is draining, keep rate steady
            state = ControllerState::HOLD;
            break;
          }
        break;
      }
  }

  /**
   * Update the rate estimate according to the current state
  */
  void
  UpdateRate ()
  {
    double time_since_last_update = (Simulator::Now () - last_rate_update).GetSeconds ();
    double eta;
    switch (state)
      {
      case ControllerState::DECREASE:
        // Decrease rate by at most 15% per second
        eta = std::pow (DECREASE_FACTOR_MULT, std::min (time_since_last_update, 1.0));

        // If the received rate is very high it can actually be that the new
        // rate is higher than the the previous one
        A_r = std::min (A_r, eta * receive_rate);
        break;
      case ControllerState::HOLD:
        break;
      case ControllerState::INCREASE:

        // // Increase rate by at most 8% per second
        // eta = std::pow (INCREASE_FACTOR_MULT,
        //                 std::min (double, 1.0));
        // A_r = eta * A_r + 1e4;

        // In startup phase, increase rate by up to CC_MULTI_INCREASE per round trip time
        if (phase == CongestionControlPhase::STARTUP)
          {
            eta = std::pow (CC_MULTI_INCREASE,
                            std::min (time_since_last_update / round_trip_time.GetSeconds (), 1.0));
            A_r = eta * A_r + CC_ADDITIVE_TERM;
          }
        else // Otherwise, do additive increase
          {
            A_r = A_r + CC_ADDITIVE_TERM;
          }

        // Cap at 1.5x the receive_rate, to prevent increasing too quickly
        A_r = std::min (A_r, 1.5 * receive_rate);

        // If the receive rate is somehow still higher than our estimate, use
        // this as our new estimate. This can happen during startup phase.
        A_r = std::max (A_r, receive_rate);
        break;
      }
    last_rate_update = Simulator::Now ();
  }

  void
  ComputeTrendlineSlope ()
  {
    double sum_x = 0;
    double sum_y = 0;
    for (std::size_t i = 0; i < trend_history.size (); i++)
      {
        sum_x += i;
        sum_y += trend_history[i];
      }
    double x_avg = sum_x / trend_history.size ();
    double y_avg = sum_y / trend_history.size ();
    double numerator = 0;
    double denominator = 0;
    for (std::size_t i = 0; i < trend_history.size (); i++)
      {
        numerator += (i - x_avg) * (trend_history[i] - y_avg);
        denominator += (i - x_avg) * (i - x_avg);
      }
    m = numerator / denominator;
  }

  std::string
  GetLogPrefix ()
  {
    return "[DelayBasedEstimator] ";
  }

public:
  /**
   * @return Current recommended send rate computed by the controller
  */
  double
  GetRate ()
  {
    return A_r;
  }

  void
  SetRoundTripTime (Time rtt)
  {
    round_trip_time = rtt;
  }

  void
  SetPhase (CongestionControlPhase phase)
  {
    this->phase = phase;
  }

  /**
   * @return True if congestion is detected
  */
  bool
  CongestionDetected ()
  {
    return state == ControllerState::DECREASE;
  }

  /**
   * Get a snapshot of the controllers state at the current time
  */
  ControllerStateSnapshot
  GetStateSnapshot ()
  {
    return ControllerStateSnapshot{
        .signal = signal,
        .state = state,
        .A_r = A_r,
        .kalman_gain = kalman_gain,
        .treshold_hi = overuse_treshold,
        .treshold_lo = underuse_treshold,
        .m = m,
        .d_m = d_m,
        .z = z,
        .variance = measurement_noise_variance,
        .error = e,
    };
  }

  /**
   * Feed a PacketRecord into the controller and compute trendline slope
   *
   * The trendline and slope are computed once enough packet data is recorded.
   *
   * @param packet A packet record
  */
  void
  FeedPacketTrendLine (PacketRecord *packet)
  {
    received_bytes[packet->time_received] = packet->size;
    UpdateReceiveRate ();

    // HACK: Set initial tresholds, we need a constructor...
    if (trend_history.empty ())
      {
        overuse_treshold = 0.02;
        underuse_treshold = 0.0;
      }

    auto latency_ms = (packet->time_received - packet->time_sent).GetMilliSeconds ();

    trend_history.push_back (latency_ms);

    if (trend_history.size () > trend_packets)
      {
        trend_history.pop_front ();
      }

    if (trend_history.size () == trend_packets)
      {
        ComputeTrendlineSlope ();
        UpdateSignal ();
        UpdateStateMachine ();
        UpdateRate ();
      }
  }

  /**
   * Feed a PacketRecord into the controller and compute the bwe using a Kalman Filter
   *
   * The rate is updated whenever a new frame is received and we already have at
   * least two frames.
   *
   * @return true if an updated rate is available
   *
   * @param packet A packet record
  */
  bool
  FeedPacketKalman (PacketRecord *packet)
  {
    bool updated = false;
    received_bytes[packet->time_received] = packet->size;

    // Check if this packet belongs to a new frame
    if (frames.find (packet->frame_no) == frames.end ())
      {
        std::cout << "Recording new frame: " << packet->frame_no << std::endl;

        // We're receiving a new frame, so assume that the previous one is complete
        // This means we can trigger an update if we have received at least two frames.
        if (frame_previous != nullptr)
          {
            std::cout << "Updating gradient based on frames " << frame_previous->frame_no << " and "
                      << frame_current->frame_no << std::endl;
            UpdateGradientEstimate ();
            UpdateReceiveRate ();
            UpdateSignal ();
            UpdateStateMachine ();
            UpdateRate ();
            ClearOldFrames ();
            updated = true;
          }

        // Add new frame
        frames.emplace (packet->frame_no, FrameRecord{
                                              .frame_no = packet->frame_no,
                                              .first_pkt_seq_no = packet->seq_no,
                                              .first_pkt_send_time = packet->time_sent,
                                              .first_pkt_rcv_time = packet->time_received,
                                              .last_pkt_seq_no = packet->seq_no,
                                              .last_pkt_send_time = packet->time_sent,
                                              .last_pkt_rcv_time = packet->time_received,
                                          });

        // Update frame pointers
        frame_previous = frame_current;
        frame_current = &frames[packet->frame_no];
      }
    else
      {
        FrameRecord &frame = frames[packet->frame_no];

        // ignore out-of-order packets
        if (packet->seq_no > frame.last_pkt_seq_no)
          {
            frame.last_pkt_seq_no = packet->seq_no;
            frame.last_pkt_send_time = packet->time_sent;
            frame.last_pkt_rcv_time = packet->time_received;
          }
      }
    return updated;
  }
};

} // namespace ns3

#endif // WEBRTC_CC_DELAY_BASED_ESTIMATOR_H
