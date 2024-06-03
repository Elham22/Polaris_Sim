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

#ifndef SCION_SIMULATOR_DELAY_BASED_CONTROLLER_H
#define SCION_SIMULATOR_DELAY_BASED_CONTROLLER_H

#include "src/core/model/simulator.h"
#include "src/SCION/model/scion-packet.h"
#include "src/SCION/model/apps/controller-state.h"
#include "src/SCION/model/externs.h"

namespace ns3 {

/**
 * A delay-based congestion controller based on the GCC algorithm
 * Sources:
 * https://c3lab.poliba.it/images/6/65/Gcc-analysis.pdf
 * https://datatracker.ietf.org/doc/html/draft-ietf-rmcat-gcc-02
*/
class DelayBasedController
{

  struct VideoFrameInfo
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
  const double del_var_th_0 = 12.5; // Initial value for the adaptive threshold, in ms
  const double overuse_time_th = 10; // Time required to trigger an overuse signal, in ms
  const double K_u = 0.01; // Coefficient for the adaptive threshold
  const double K_d = 0.00018; // Coefficient for the adaptive threshold
  const Time T = Seconds (1); // Time window for measuring the received bitrate
  const double beta = 0.85; // Decrease rate factor

  const double INCREASE_FACTOR_MULT =
      1.08; // Factor to increase rate per second during multiplicative increase
  const double DECREASE_FACTOR_MULT =
      0.85; // Factor to decrease rate per second during multiplicative decrease

  // Time window accross which to measure incoming traffic rate
  const Time receive_rate_window = MilliSeconds (500);

  // State variables
  DetectorSignal signal = DetectorSignal::NORMAL;
  ControllerState state = ControllerState::INCREASE;
  Time overuse_detected_since = Seconds (0);
  bool overuse_detected = false;

  double adaptive_treshold = del_var_th_0;
  double e = e_0;

  double d_m = 0; // Measured one way delay gradient
  double m = 0; // Estimate of the one way delay gradient
  double m_prev = 0;
  double z = 0; // Residual

  double kalman_gain = 0; // Kalman gain
  double measurement_noise_variance = 0; // Variance of the one way delay gradient

  // Records send and arrival times resp. for first and last packet for each frame
  std::map<uint32_t, VideoFrameInfo> frames;
  // Maintain pointers to the current and previous frames
  VideoFrameInfo *frame_current = nullptr;
  VideoFrameInfo *frame_previous = nullptr;

  // This map is used to measure the receiver rate
  std::map<Time, u_int16_t> received_bytes;
  double receive_rate; // Rate of traffic arriving in the last receive_rate_window, in Bytes/s

  double A_r = 0; // Rate estimate computed by the controller, in Bytes/s

  Time last_rate_update = Seconds (0);

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

    // return in ms as double
    return delay_grad.GetMilliSeconds ();
  }

  /**
   * Clean up frame data older than T
  */
  void
  ClearOldFrames ()
  {
    for (auto it = frames.begin (); it != frames.end ();)
      {
        if (Simulator::Now () - it->second.last_pkt_rcv_time > T)
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
    m_prev = m;
    // Compute measured one way delay gradient
    d_m = MeasuredOneWayDelayGradient ();

    // Compute the residual z(t_i) = d_m (t_i) − m (t_i−1)
    z = d_m - m_prev;

    // Update the variance σ̂ 2n (ti ) = β · σ̂ 2v(t_i−1 ) + (1 − β) · z(t_i)^2
    double beta = 0.95;
    measurement_noise_variance = beta * measurement_noise_variance + (1 - beta) * z * z;
    // TODO: ^ not really used right now, where does this come into play?

    // Compute the Kalman gain
    // kγ (t_i ) = k_d if |m(t_i )| < γ(t_i−1 )
    // kγ (t_i ) = k_u otherwise
    if (std::abs (m) < adaptive_treshold)
      {
        kalman_gain = K_d;
      }
    else
      {
        kalman_gain = K_u;
      }

    // Use the Kalman gain K(t_i) which provides the correction to the estimation:
    // m(ti ) = (1 − K(ti )) · m(ti−1 ) + K(ti ) · (dm (ti ))
    m = (1 - kalman_gain) * m_prev + kalman_gain * d_m;

    // Update the adaptive treshold
    // γ(t_i ) = γ(t_i−1 ) + ∆T · kγ (t_i )(|m(t_i )| − γ(t_i−1 ))
    // ∆T = t_i − t_i−1
    double delta_t =
        (frame_current->last_pkt_rcv_time - frame_previous->last_pkt_rcv_time).GetMilliSeconds ();
    adaptive_treshold =
        adaptive_treshold + delta_t * kalman_gain * (std::abs (m) - adaptive_treshold);
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
    if (m > adaptive_treshold)
      {
        // Only signal overuse if we have been above the threshold for a certain time
        if (overuse_detected)
          {
            if (Simulator::Now () - overuse_detected_since > Seconds (overuse_time_th))
              {
                signal = DetectorSignal::OVERUSE;
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
    else if (m < -adaptive_treshold)
      {
        overuse_detected = false;
        signal = DetectorSignal::UNDERUSE;
      }
    else // -adaptive_treshold <= m <= adaptive_treshold
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
    Time time_since_last_rate_update = Simulator::Now () - last_rate_update;
    double eta;
    switch (state)
      {
      case ControllerState::DECREASE:
        // Decrease rate by at most 15% per second
        eta = std::pow (DECREASE_FACTOR_MULT,
                        std::min (time_since_last_rate_update.GetSeconds (), 1.0));
        A_r = eta * receive_rate;
        break;
      case ControllerState::HOLD:
        // If A_r does not have any previous value, start with the current receive_rate
        if (A_r == 0)
          {
            A_r = receive_rate;
          }
        break;
      case ControllerState::INCREASE:

        // Increase rate by at most 8% per second
        eta = std::pow (INCREASE_FACTOR_MULT,
                        std::min (time_since_last_rate_update.GetSeconds (), 1.0));
        A_r = eta * A_r;

        // Cap at 1.5x the receive_rate
        if (A_r > 1.5 * receive_rate)
          {
            A_r = 1.5 * receive_rate;
          }
        break;
      }
    last_rate_update = Simulator::Now ();
  }

public:
  /**
   * @return Current recommended send rate computed by the controller
  */
  double
  GetCurrentRate ()
  {
    return A_r;
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
        .adaptive_treshold = adaptive_treshold,
        .m = m,
        .d_m = d_m,
        .z = z,
        .measurement_noise_variance = measurement_noise_variance,
    };
  }

  /**
   * Feed a packet into the controller
   *
   * The rate is updated whenever a new frame is received and we already have at
   * least two frames.
   *
   * @return true if an updated rate is available
   *
   * @param app_data the application data of the packet
   * @param size the size of the packet in Bytes
   * @param time_tx the time the packet departed at the sender
   * @param time_rx the time the packet arrived at the receiver
  */
  bool
  RecordPacket (AppData app_data, uint16_t size, Time time_tx, Time time_rx)
  {
    received_bytes[time_rx] = size;
    // Check if this packet belongs to a new frame
    if (frames.find (app_data.frame_no) == frames.end ())
      {
        frames[app_data.frame_no] = VideoFrameInfo{
            .frame_no = app_data.frame_no,
            .first_pkt_seq_no = app_data.seq_no,
            .first_pkt_send_time = time_tx,
            .first_pkt_rcv_time = time_rx,
            .last_pkt_seq_no = app_data.seq_no,
            .last_pkt_send_time = time_tx,
            .last_pkt_rcv_time = time_rx,
        };

        frame_previous = frame_current;
        frame_current = &frames[app_data.frame_no];

        if (frame_previous != nullptr)
          {
            UpdateGradientEstimate ();
            UpdateReceiveRate ();
            UpdateSignal ();
            UpdateStateMachine ();
            UpdateRate ();
            ClearOldFrames ();
            return true;
          }
      }
    else
      {
        VideoFrameInfo &frame = frames[app_data.frame_no];

        // ignore out-of-order packets (should never be the case in the simulation)
        if (app_data.seq_no <= frame.last_pkt_seq_no)
          {
            return false;
          }
        frame.last_pkt_seq_no = app_data.seq_no;
        frame.last_pkt_send_time = time_tx;
        frame.last_pkt_rcv_time = time_rx;
      }
    return false;
  }

  /**
   * Copy constructor
  */
  DelayBasedController &
  operator= (const DelayBasedController &other)
  {
    if (this != &other)
      {
        signal = other.signal;
        state = other.state;
        overuse_detected_since = other.overuse_detected_since;
        overuse_detected = other.overuse_detected;
        adaptive_treshold = other.adaptive_treshold;
        e = other.e;
        d_m = other.d_m;
        m = other.m;
        m_prev = other.m_prev;
        z = other.z;
        kalman_gain = other.kalman_gain;
        measurement_noise_variance = other.measurement_noise_variance;
        frames = other.frames;
        frame_current = other.frame_current;
        frame_previous = other.frame_previous;
        received_bytes = other.received_bytes;
        receive_rate = other.receive_rate;
        A_r = other.A_r;
        last_rate_update = other.last_rate_update;
      }
    return *this;
  }
};

} // namespace ns3

#endif // SCION_SIMULATOR_DELAY_BASED_CONTROLLER_H
