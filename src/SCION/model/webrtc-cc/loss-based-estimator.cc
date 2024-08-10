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

#include <algorithm>
#include <memory>
#include <vector>
#include "src/core/model/simulator.h"
#include "src/SCION/model/webrtc-cc/cc-units.h"
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

  std::vector<PacketRecord> packet_history;
  Time history_window = MilliSeconds (100);
  uint32_t seq_no_min = 0; // smallest seq_no in recent history
  uint32_t seq_no_max = 0; // largest seq_no in recent history
  uint32_t packets_received_in_window = 0;
  uint32_t packets_total_in_window = 0;
  double loss = 0.0;

  Time time_last_rate_update = Seconds (0);
  double rate_estimate = 0.0;

  CongestionControlPhase phase = CongestionControlPhase::STARTUP;
  Time round_trip_time = CC_INITIAL_RTT;

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
    packet_history.erase (std::remove_if (packet_history.begin (), packet_history.end (),
                                          [now, this] (PacketRecord packet) {
                                            return now - packet.time_received > history_window;
                                          }),
                          packet_history.end ());

    // Sort the packets by seq_no
    std::sort (packet_history.begin (), packet_history.end (),
               [] (PacketRecord a, PacketRecord b) { return a.seq_no < b.seq_no; });

    if (packet_history.empty ())
      {
        return false;
      }

    seq_no_min = packet_history.front ().seq_no;
    seq_no_max = packet_history.back ().seq_no;

    packets_received_in_window = packet_history.size ();
    packets_total_in_window = seq_no_max - seq_no_min + 1;

    loss = 1.0 - (double) packets_received_in_window / packets_total_in_window;

    if (loss > 0)
      {
        // std::cout << GetLogPrefix () << "Loss: " << loss << std::endl;
        // std::cout << GetLogPrefix () << "Should have packets from seq no " << seq_no_min << " to "
                  // << seq_no_max << std::endl;
        // Log missing packets
        for (std::size_t i = 1; i < packet_history.size (); i++)
          {
            auto delta = packet_history[i].seq_no - packet_history[i - 1].seq_no;
            if (delta > 1)
              {
                // std::cout << GetLogPrefix () << "Missing packets between "
                          // << packet_history[i - 1].seq_no << " and " << packet_history[i].seq_no
                          // << std::endl;
              }
          }
      }

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
        eta = std::pow (CC_MULTI_INCREASE,
                        std::min (time_since_last_update / round_trip_time.GetSeconds (), 1.0));
        new_estimate = eta * rate_estimate;
        // new_estimate = eta * rate_estimate + CC_ADDITIVE_TERM;
      }
    else if (loss > LOSS_TRESHOLD_HIGH)
      {
        // eta = std::pow ((1 - 1 * loss), std::min (time_since_last_update, 1.0));
        new_estimate = (1 - 0.5 * loss) * rate_estimate;
      }
    else
      {
        // do nothing
      }
    rate_estimate = new_estimate;
    time_last_rate_update = Simulator::Now ();
  }

public:
  void
  SetWindow (Time window)
  {
    history_window = window + MilliSeconds (100);
    // std::cout << GetLogPrefix () << "Set window to " << history_window.GetSeconds () << std::endl;
  }

  void
  SetRate (double rate)
  {
    rate_estimate = rate;
  }

  void
  LimitRate (double rate_limit)
  {
    rate_estimate = std::min (rate_estimate, rate_limit);
  }

  void
  FeedReport (std::shared_ptr<PacketsReport> report)
  {
    packet_history.clear (); // TODO: hack to react quicker
    for (auto packet : report->packets)
      {
        packet_history.push_back (packet);
      }
    UpdateStatistics ();
    UpdateRate ();
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

  void
  Reset ()
  {
    packet_history.clear ();
    loss = 0.0;
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
