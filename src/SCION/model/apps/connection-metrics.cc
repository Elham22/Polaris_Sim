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

#include "connection-metrics.h"

#include <algorithm>
#include <numeric>
#include <cmath>

#include "src/SCION/model/logging.h"

namespace ns3 {

ConnectionMetrics::ConnectionMetrics (bool logging) : logging (logging)
{
}

void
ConnectionMetrics::OnSentPacket (const PacketRecord &record)
{
  total_bytes_sent += record.size;
}

void
ConnectionMetrics::OnReceivedPackets (const std::vector<PacketRecord> &packets)
{
  if (packets.empty ())
    {
      return;
    }

  // We maintain the delivered packets vector sorted by sequence number. In most
  // cases, the packets and the delivery feedback are received in order and we
  // can just append without sorting
  bool sorted = true;

  // Reserve enough space before-hand, to avoid reallocations (which would make the prev pointer invalid)
  delivered_packets.reserve (delivered_packets.size () + packets.size ());

  // Accumulate total bytes and at the same time check if we maintain sorted order
  PacketRecord *prev = delivered_packets.empty () ? nullptr : &delivered_packets.back ();
  for (const auto &packet : packets)
    {
      total_bytes_received += packet.size;

      LOG ("Adding packet with seq no " << packet.seq_no << " to delivered packets" << std::endl);
      delivered_packets.push_back (packet);

      if (prev && prev->seq_no > packet.seq_no)
        {
          sorted = false;
        }

      prev = &delivered_packets.back ();
    }

  // If at any point, we broke the order, sort the vector again
  if (!sorted)
    {
      std::sort (delivered_packets.begin (), delivered_packets.end (),
                 [] (PacketRecord a, PacketRecord b) { return a.seq_no < b.seq_no; });
    }
}

void
ConnectionMetrics::BufferPackets (const std::vector<PacketRecord> &packets)
{
  if (packets.empty ())
    {
      return;
    }

  // Play back all packets that had to have left the buffer by this point
  EmitFromBuffer ();

  // We maintain the packets in the jitter buffer sorted by sequence number. In most
  // cases, the packets and the delivery feedback are received in order and we
  // can just append without sorting
  bool sorted = true;

  // Reserve enough space before-hand, to avoid reallocations (which would make the prev pointer invalid)
  jitter_buffer.reserve (jitter_buffer.size () + packets.size ());

  // Accumulate total bytes and at the same time check if we maintain sorted order
  PacketRecord *prev = jitter_buffer.empty () ? nullptr : &jitter_buffer.back ();
  for (const auto &packet : packets)
    {
      total_bytes_received += packet.size;

      // If seq no is lower than highest in delivered, drop this one
      if (!delivered_packets.empty () && packet.seq_no < delivered_packets.back ().seq_no)
        {
          LOG ("Ignoring packet with seq no "
               << packet.seq_no << ", arrived too late / too far out of order" << std::endl);
          continue;
        }

      LOG ("Adding packet with seq no " << packet.seq_no << " to buffer" << std::endl);
      jitter_buffer.push_back (packet);

      if (prev && prev->seq_no > packet.seq_no)
        {
          sorted = false;
        }

      prev = &jitter_buffer.back ();
    }

  // If at any point, we broke the order, sort the vector again
  if (!sorted)
    {
      std::sort (jitter_buffer.begin (), jitter_buffer.end (),
                 [] (PacketRecord a, PacketRecord b) { return a.seq_no < b.seq_no; });
    }
}

// Move packets from buffer to delivered packets
void
ConnectionMetrics::EmitFromBuffer ()
{

  // Find the last element in the buffer which is older than the buffer window
  auto rev_it = std::find_if (jitter_buffer.rbegin (), jitter_buffer.rend (),
                              [this] (const PacketRecord &packet) {
                                return Simulator::Now () - packet.time_received > buffer_window;
                              });

  if (rev_it == jitter_buffer.rend ())
    {
      return;
    }

  // Convert reverse iterator to normal iterator (Note: it now points to the
  // element after the last one that satisfies the condition)
  auto it = rev_it.base ();

  // Process packets up to `it`
  for (auto iter = jitter_buffer.begin (); iter != it; ++iter)
    {
      delivered_packets.push_back (*iter);
      LOG ("Emitting packet with seq no " << iter->seq_no << " from buffer" << std::endl);
    }

  // Erase processed packets from the jitter buffer
  jitter_buffer.erase (jitter_buffer.begin (), it);
}

double
ConnectionMetrics::GetJitterMs ()
{
  if (delivered_packets.size () < 2)
    {
      return 0.0;
    }

  // Arrival time deltas in ms
  std::vector<double> arrival_deltas;

  // Delivered packets are sorted by sequence number
  auto prev_it = delivered_packets.begin ();
  auto it = std::next (delivered_packets.begin ());
  for (; it != delivered_packets.end (); ++it, ++prev_it)
    {
      arrival_deltas.push_back ((it->time_received - prev_it->time_received).GetMilliSeconds ());
    }

  double average_delta = std::accumulate (arrival_deltas.begin (), arrival_deltas.end (), 0.0) /
                         arrival_deltas.size ();

  double jitter = 0.0;
  for (const auto &diff : arrival_deltas)
    {
      jitter += std::abs (diff - average_delta);
    }

  jitter /= arrival_deltas.size ();

  // For debugging: If jitter is more than 150 ms, print a warning and print all sequence numbers + arrival time deltas for debugging
  if (jitter > 150)
    {
      std::string seq_nos = "";
      std::string arrival_times = "";
      std::string deltas = "";
      logging = true;
      for (size_t i = 0; i < delivered_packets.size (); ++i)
        {
          seq_nos += std::to_string (delivered_packets[i].seq_no) + ", ";
          arrival_times +=
              std::to_string (delivered_packets[i].time_received.GetMilliSeconds ()) + ", ";
          if (i > 0)
            {
              deltas += std::to_string (arrival_deltas[i - 1]) + ", ";
            }
        }

      LOG ("High jitter detected: " << jitter << " ms" << std::endl);
      LOG ("Sequence numbers: " << seq_nos << std::endl);
      LOG ("Arrival times: " << arrival_times << std::endl);
      LOG ("Arrival time deltas: " << deltas << std::endl);
    }

  // Print time now in ms
  LOG ("Time now: " << Simulator::Now ().GetMilliSeconds () << std::endl);

  return jitter;
}

double
ConnectionMetrics::GetLatencyMs ()
{
  if (delivered_packets.empty ())
    {
      return 0.0;
    }

  // Compute average latency
  double sum_latency = 0.0;
  for (const auto &packet : delivered_packets)
    {
      sum_latency += (packet.time_received - packet.time_sent).GetMilliSeconds ();
    }

  return sum_latency / delivered_packets.size ();
}

void
ConnectionMetrics::ClearWindow ()
{
  delivered_packets.clear ();
}

/**
   * Update the statistics based on the recorded history
   * 
   * @return true if the statistics were updated successfully
   */
bool
ConnectionMetrics::UpdateStatistics ()
{
  Time now = Simulator::Now ();

  EmitFromBuffer ();

  LOG (":: Updating... " << std::endl);

  auto it = std::find_if (delivered_packets.begin (), delivered_packets.end (),
                          [now, this] (const PacketRecord &packet) {
                            return now - packet.time_received <= tracking_window;
                          });

  if (it != delivered_packets.end ())
    {
      LOG ("Deleting from sequence number " << delivered_packets.begin ()->seq_no
                                            << " up to and excluding " << it->seq_no << std::endl);

      // Erase packets up to the first one that is still within the tracking window
      delivered_packets.erase (delivered_packets.begin (), it);
    }
  else
    {
      LOG ("No recent packet in window" << std::endl);
      delivered_packets.clear ();
    }

  // Make sure we have at least some statistics to work with
  if (delivered_packets.size () < 2)
    {
      return false;
    }

  int32_t seq_no_min = delivered_packets.begin ()->seq_no;
  int32_t seq_no_max = delivered_packets.rbegin ()->seq_no;

  uint32_t packets_received_in_window = delivered_packets.size ();
  uint32_t packets_expected_in_window = seq_no_max - seq_no_min + 1;

  LOG ("Window from seq no " << seq_no_min << " to " << seq_no_max << ", have "
                             << packets_received_in_window << " packets out of "
                             << packets_expected_in_window << std::endl);

  if (packets_received_in_window < packets_expected_in_window)
    {
      loss = 1.0 - (double) packets_received_in_window / packets_expected_in_window;

      if (logging)
        {
          // Find all missing sequence numbers
          std::string lost_seq_nos = "";
          for (size_t i = 1; i < delivered_packets.size (); ++i)
            {
              int32_t expected_seq_no = delivered_packets[i - 1].seq_no + 1;
              int32_t actual_seq_no = delivered_packets[i].seq_no;
              while (expected_seq_no < actual_seq_no)
                {
                  lost_seq_nos += std::to_string (expected_seq_no) + ", ";
                  ++expected_seq_no;
                }
            }

          LOG ("Loss: " << loss << std::endl);
          LOG ("Lost " << packets_expected_in_window - packets_received_in_window
                       << " packets in window from seq no " << seq_no_min << " to " << seq_no_max
                       << ": " << lost_seq_nos << std::endl);
        }
    }
  else
    {
      loss = 0;
    }

  return true;
}

double
ConnectionMetrics::GetFractionLoss ()
{
  return loss;
}

double
ConnectionMetrics::GetVmafScore ()
{
  /*
  VMAF (0-100) scores for loss (in %) with 50% correlation, Threema low bandwidth profile:
  0%: 59.83, 10%: 57.96, 15%: 57.53, 20%: 58.53, 30%: 53.55, 35%: 6.82, 40%: 6.13

  VMAF scores (0-100) for varying levels of uncorrelated, normally distributed jitter (in ms) with 150 ms added network latency under the Threema high bandwidth profile:
  0ms: 68.34, 5ms: 64.91, 10ms: 62.58, 15ms: 46.91, 20ms: 32.78, 30ms: 5.545542

  VMAF scores (0-100) for bandwidth in Mbps, with Threema Balanced Setting defaulting to the Low Bandwidth Profile in a Cellular Network:
  0.2Mbps: 16.76, 0.3Mbps: 37.07, 0.5Mbps: 48.75, 1Mbps: 58.49, 2Mbps: 59.30, 3Mbps: 59.51
  */

  // Loss
  double loss_score = 0.0;
  if (loss < 0.1)
    {
      loss_score = 59.83;
    }
  else if (loss < 0.15)
    {
      loss_score = 57.96;
    }
  else if (loss < 0.2)
    {
      loss_score = 57.53;
    }
  else if (loss < 0.3)
    {
      loss_score = 58.53;
    }
  else if (loss < 0.35)
    {
      loss_score = 53.55;
    }
  else if (loss < 0.4)
    {
      loss_score = 6.82;
    }
  else
    {
      loss_score = 6.13;
    }

  // Jitter
  double jitter_score = 0.0;
  double jitter_ms = jitter;
  if (jitter_ms < 5)
    {
      jitter_score = 68.34;
    }
  else if (jitter_ms < 10)
    {
      jitter_score = 64.91;
    }
  else if (jitter_ms < 15)
    {
      jitter_score = 62.58;
    }
  else if (jitter_ms < 20)
    {
      jitter_score = 46.91;
    }
  else if (jitter_ms < 30)
    {
      jitter_score = 32.78;
    }
  else
    {
      jitter_score = 5.545542;
    }

  // Bandwidth
  double bandwidth_score = 0.0;
  double bandwidth_mbps = sendrate / 1e6;
  if (bandwidth_mbps < 0.3)
    {
      bandwidth_score = 16.76;
    }
  else if (bandwidth_mbps < 0.5)
    {
      bandwidth_score = 37.07;
    }
  else if (bandwidth_mbps < 1)
    {
      bandwidth_score = 48.75;
    }
  else if (bandwidth_mbps < 2)
    {
      bandwidth_score = 58.49;
    }
  else if (bandwidth_mbps < 3)
    {
      bandwidth_score = 59.30;
    }
  else
    {
      bandwidth_score = 59.51;
    }

  // Combine scores evenly
  double score = 0.0;
  score = (loss_score + jitter_score + bandwidth_score) / 3.0;

  return score;
}

} // namespace ns3