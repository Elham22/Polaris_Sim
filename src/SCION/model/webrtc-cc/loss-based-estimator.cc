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

#ifndef WEBRTC_CC_LOSS_BASED_ESTIMATOR_H
#define WEBRTC_CC_LOSS_BASED_ESTIMATOR_H

#include "src/core/model/simulator.h"
#include "src/SCION/model/webrtc-cc/types.h"

namespace ns3 {

/**
 * A loss-based bandwidth estimator based on GCC
 * Sources:
 * https://c3lab.poliba.it/images/6/65/Gcc-analysis.pdf
 * https://datatracker.ietf.org/doc/html/draft-ietf-rmcat-gcc-02
 * https://webrtc.googlesource.com/src
*/
class LossBasedEstimator
{

protected:
  const double LOSS_TRESHOLD_LOW = 0.02;
  const double LOSS_TRESHOLD_HIGH = 0.10;

  std::list<PacketRecord> packet_history;
  Time history_window = MilliSeconds (100);
  uint32_t seq_no_min = 0; // smallest seq_no in recent history
  uint32_t seq_no_max = 0; // largest seq_no in recent history
  uint32_t packets_received_in_window = 0;
  uint32_t packets_total_in_window = 0;
  double loss = 0.0;

  Time time_last_rate_update = Seconds (0);
  double rate_estimate = 0.0;

  std::string
  GetLogPrefix ()
  {
    return "[LossBasedEstimator] ";
  }

  /**
   * Update the statistics based on the recorded history
   * 
   * @return true if the statistics were updated successfully
   */
  bool
  UpdateStatistics ()
  {
    // Clear old packets from the history
    Time now = Simulator::Now ();
    while (!packet_history.empty ())
      {
        if (packet_history.front ().time_received + history_window < now)
          {
            packet_history.erase (packet_history.begin ());
          }
        else
          {
            break;
          }
      }

    if (packet_history.empty ())
      {
        return false;
      }

    // iterate through history to find min and max seq_no
    seq_no_min = packet_history.front ().seq_no;
    seq_no_max = packet_history.front ().seq_no;
    for (auto packet : packet_history)
      {
        if (packet.seq_no < seq_no_min)
          {
            seq_no_min = packet.seq_no;
          }
        if (packet.seq_no > seq_no_max)
          {
            seq_no_max = packet.seq_no;
          }
      }

    packets_received_in_window = packet_history.size ();
    packets_total_in_window = seq_no_max - seq_no_min + 1;

    loss = 1.0 - (double) packets_received_in_window / packets_total_in_window;

    if (packets_total_in_window < 1 || packets_total_in_window < packets_received_in_window)
      {
        for (auto packet : packet_history)
          {
            std::cout << GetLogPrefix () << "Packet: " << packet.seq_no << std::endl;
          }
        NS_FATAL_ERROR ("Expected number of packets less than the number received");
      }
    return true;
  }

  void
  UpdateRate ()
  {
    double time_since_last_update = (Simulator::Now () - time_last_rate_update).GetSeconds ();
    double eta;
    double new_estimate = rate_estimate;
    if (loss < LOSS_TRESHOLD_LOW)
      {
        eta = std::pow (1.05, std::min (time_since_last_update, 1.0));
        new_estimate = eta * rate_estimate;

        // Add a constant term that has no effect at higher rates but prevents getting stuck at low rates
        new_estimate += 1e4;
      }
    else if (loss > LOSS_TRESHOLD_HIGH)
      {
        eta = std::pow ((1 - 1 * loss), std::min (time_since_last_update, 1.0));
        new_estimate = eta * rate_estimate;
      }
    else
      {
        // do nothing
      }
    rate_estimate = new_estimate;
    time_last_rate_update = Simulator::Now ();
  }

public:
  LossBasedEstimator (double initial_rate_estimate)
  {
    rate_estimate = initial_rate_estimate;
  }

  void
  SetWindow (Time window)
  {
    history_window = window + MilliSeconds (100);
    std::cout << GetLogPrefix () << "Set window to " << history_window.GetSeconds () << std::endl;
  }

  void
  LimitRate (double rate_limit)
  {
    rate_estimate = std::min (rate_estimate, rate_limit);
  }

  void
  FeedReport (PacketsReport *report)
  {
    for (auto packet : report->packets)
      {
        packet_history.push_back (packet);
      }
    UpdateStatistics ();
    UpdateRate ();
  }

  double
  GetRate ()
  {
    return rate_estimate;
  }

  double
  GetLoss ()
  {
    return loss;
  }
};

} // namespace ns3

#endif // WEBRTC_CC_LOSS_BASED_ESTIMATOR_H
