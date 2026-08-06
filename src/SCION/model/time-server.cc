/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright (c) 2022 ETH Zuerich
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
 * Author: Seyedali Tabaeiaghdaei seyedali.tabaeiaghdaei@inf.ethz.ch
 */

#include <omp.h>

#include "ns3/log.h"

#include "externs.h"
#include "json.hpp"
#include "run-parallel-events.h"
#include "schedule-periodic-events.h"
#include "scion-core-as.h"
#include "time-server.h"
#include "utils.h"

namespace ns3 {
NS_LOG_COMPONENT_DEFINE ("TimeServer");

void
TimeServer::RequestSetOfAllCoreAsesFromPathServer ()
{
  AdvanceLocalTime ();

  NS_LOG_FUNCTION ("TimeSrv at " << isd_number << ":" << as_number
                                 << " sent req for all core ASes to PthSrv");

  PayloadType payload_type = PayloadType::REQ_FOR_LIST_OF_ALL_CORE_ASES;
  Payload payload;
  ScionPacket *packet = CreateScionPacket (payload, payload_type, ia_addr, 1, 0);

  SendScionPacket (packet);
}

void
TimeServer::ProcessReceivedPacket (uint16_t local_if, ScionPacket *packet, Time receive_time)
{
  ScionHost::ProcessReceivedPacket (local_if, packet, receive_time);

  if (packet->payload_type == PayloadType::LIST_OF_ALL_CORE_ASES)
    {
      NS_LOG_FUNCTION ("TimeSrv at " << isd_number << ":" << as_number
                                     << " rcv all core ASes from PthSrv");
      ReceiveSetOfAllCoreAsesFromPathServer (packet);
      packet->packet_originator->DestroyScionPacket (packet);
      return;
    }

  if (packet->payload_type == PayloadType::BROADCAST_LIST_OF_ALL_CORE_ASES)
    {
      NS_LOG_FUNCTION ("TimeSrv at "
                       << isd_number << ":" << as_number << " rcv all core ASes from other TimeSrv "
                       << GET_ISDN (packet->src_ia) << ":" << GET_ASN (packet->src_ia));
      ReceiveSetOfAllCoreAsesFromOtherTimeServer (packet);
      packet->packet_originator->DestroyScionPacket (packet);
      return;
    }

  if (packet->payload_type == PayloadType::NTP_REQ)
    {
      NS_LOG_FUNCTION ("TimeSrv at " << isd_number << ":" << as_number << " rcv ntp req from "
                                     << GET_ISDN (packet->src_ia) << ":"
                                     << GET_ASN (packet->src_ia));
      ReceiveNtpReqFromPeer (packet, receive_time);
      return;
    }

  if (packet->payload_type == PayloadType::NTP_RESP)
    {
      NS_LOG_FUNCTION ("TimeSrv at " << isd_number << ":" << as_number << " rcv ntp resp from "
                                     << GET_ISDN (packet->src_ia) << ":"
                                     << GET_ASN (packet->src_ia));
      ReceiveNtpResFromPeer (packet, receive_time);
      DestroyScionPacket (packet);
      return;
    }
}

void
TimeServer::ReceiveSetOfAllCoreAsesFromPathServer (ScionPacket *packet)
{
  if (*packet->payload.list_of_all_ases.set_of_all_ases != set_of_all_core_ases)
    {
      std::vector<ia_t> v1 (set_of_all_core_ases.begin (), set_of_all_core_ases.end ());
      std::vector<ia_t> v2 (packet->payload.list_of_all_ases.set_of_all_ases->begin (),
                            packet->payload.list_of_all_ases.set_of_all_ases->end ());
      std::vector<ia_t> result (v1.size () + v2.size ());
      std::vector<ia_t>::iterator it =
          std::set_union (v1.begin (), v1.end (), v2.begin (), v2.end (), result.begin ());
      std::set<ia_t> result_set (result.begin (), it);
      set_of_all_core_ases = result_set;

      RequestForPathsToAllCoreAses ();
      //Simulator::Schedule(MilliSeconds(350), &TimeServer::SendSetOfAllCoreAsesToNeighbors, this);

      if (parallel_scheduler)
        {
          if (read_disjoint_paths == ReadOrWriteDisjointPaths::W ||
              read_disjoint_paths == ReadOrWriteDisjointPaths::NO_R_NO_W)
            {
              Simulator::Schedule (MilliSeconds (300),
                                   &RunParallelEvents<void (TimeServer::*) (), TimeServer *>,
                                   local_address, &TimeServer::ConstructSetOfSelectedPaths);
              if (read_disjoint_paths == ReadOrWriteDisjointPaths::W)
                {
                  Simulator::Schedule (MilliSeconds (310),
                                       &RunParallelEvents<void (TimeServer::*) (), TimeServer *>,
                                       local_address, &TimeServer::WriteSetOfDisjointPaths);
                }
            }
          else if (read_disjoint_paths == ReadOrWriteDisjointPaths::R)
            {
              if (set_of_selected_paths.empty ())
                {
                  Simulator::Schedule (MilliSeconds (310),
                                       &RunParallelEvents<void (TimeServer::*) (), TimeServer *>,
                                       local_address, &TimeServer::ReadSetOfDisjointPaths);
                }
            }
        }
    }
}

void
TimeServer::ConstructSetOfSelectedPaths ()
{
  set_of_selected_paths.clear ();
  if (path_selection == "disjoint")
    {
      ConstructSetOfMostDisjointPaths ();
      return;
    }

  if (path_selection == "short")
    {
      ConstructSetOfShortestPaths ();
      return;
    }

  if (path_selection == "random")
    {
      ConstructSetOfRandomPaths ();
      return;
    }
}

void
TimeServer::ConstructSetOfMostDisjointPaths ()
{
  NS_LOG_FUNCTION ("TimeSrv at " << isd_number << ":" << as_number
                                 << " constructing disjoint paths");
  for (auto const &dst_ia : set_of_all_core_ases)
    {
      if (dst_ia == ia_addr)
        {
          continue;
        }

      set_of_selected_paths.insert (
          std::make_pair (dst_ia, std::unordered_set<const PathSegment *> ()));
      auto &set_of_selected_paths_per_dst_ia = set_of_selected_paths.at (dst_ia);

      std::unordered_map<ia_t, uint32_t> number_of_paths_per_hop_ia;
      auto const &path_segments = *cached_core_path_segments.at (dst_ia)->at (ia_addr);

      while (set_of_selected_paths_per_dst_ia.size () < number_of_paths_to_use_for_global_sync &&
             set_of_selected_paths_per_dst_ia.size () < path_segments.size ())
        {
          const PathSegment *best_path = NULL;
          uint64_t best_path_score = std::numeric_limits<uint64_t>::max ();
          uint32_t best_path_len = std::numeric_limits<uint32_t>::max ();

          uint32_t min_len = path_segments.begin ()->first;
          for (auto const &[path_len, path_seg] : path_segments)
            {
              if (min_len == 2 && path_len > 2)
                {
                  break;
                }
              if (set_of_selected_paths_per_dst_ia.find (path_seg) !=
                  set_of_selected_paths_per_dst_ia.end ())
                {
                  continue;
                }

              uint64_t path_seg_score = 1;
              for (uint32_t j = 1; j < (uint32_t) path_len - 1; ++j)
                {
                  uint64_t hop = path_seg->hops.at (j);
                  ia_t hop_ia = GET_HOP_IA (hop);

                  if (number_of_paths_per_hop_ia.find (hop_ia) != number_of_paths_per_hop_ia.end ())
                    {
                      path_seg_score *= (number_of_paths_per_hop_ia.at (hop_ia) + 1);
                    }
                }

              if (path_seg_score == 1)
                {
                  best_path = path_seg;
                  best_path_len = path_len;
                  break;
                }

              if (path_seg_score < best_path_score)
                {
                  best_path = path_seg;
                  best_path_score = path_seg_score;
                  best_path_len = path_len;
                }
              else if (path_seg_score == best_path_score && path_len < best_path_len)
                {
                  best_path = path_seg;
                  best_path_score = path_seg_score;
                  best_path_len = path_len;
                }
            }

          if (best_path == NULL)
            {
              break;
            }

          set_of_selected_paths_per_dst_ia.insert (best_path);

          for (uint32_t j = 1; j < best_path_len - 1; ++j)
            {
              uint64_t hop = best_path->hops.at (j);
              ia_t hop_ia = GET_HOP_AS_ING (hop);
              if (number_of_paths_per_hop_ia.find (hop_ia) == number_of_paths_per_hop_ia.end ())
                {
                  number_of_paths_per_hop_ia.insert (std::make_pair (hop_ia, 0));
                }
              number_of_paths_per_hop_ia.at (hop_ia)++;
            }
        }
    }

  NS_LOG_FUNCTION ("TimeSrv at " << isd_number << ":" << as_number
                                 << " FINISHED constructing disjoint paths");
}

void
TimeServer::ConstructSetOfShortestPaths ()
{
  NS_LOG_FUNCTION ("TimeSrv at " << isd_number << ":" << as_number
                                 << " constructing shortest paths");
  for (auto const &dst_ia : set_of_all_core_ases)
    {
      if (dst_ia == ia_addr)
        {
          continue;
        }

      set_of_selected_paths.insert (
          std::make_pair (dst_ia, std::unordered_set<const PathSegment *> ()));
      auto &set_of_selected_paths_per_dst_ia = set_of_selected_paths.at (dst_ia);

      auto const &path_segments = *cached_core_path_segments.at (dst_ia)->at (ia_addr);
      uint32_t min_len = path_segments.begin ()->first;
      for (auto const &[path_len, path_seg] : path_segments)
        {
          if (min_len == 2 && path_len > 2)
            {
              break;
            }
          if (set_of_selected_paths_per_dst_ia.size () >= number_of_paths_to_use_for_global_sync)
            {
              break;
            }
          set_of_selected_paths_per_dst_ia.insert (path_seg);
        }
    }
}

void
TimeServer::ConstructSetOfRandomPaths ()
{
  NS_LOG_FUNCTION ("TimeSrv at " << isd_number << ":" << as_number
                                 << " constructing shortest paths");
  for (auto const &dst_ia : set_of_all_core_ases)
    {
      if (dst_ia == ia_addr)
        {
          continue;
        }

      set_of_selected_paths.insert (
          std::make_pair (dst_ia, std::unordered_set<const PathSegment *> ()));
      auto &set_of_selected_paths_per_dst_ia = set_of_selected_paths.at (dst_ia);
      auto const &path_segments = *cached_core_path_segments.at (dst_ia)->at (ia_addr);
      uint32_t min_len = path_segments.begin ()->first;

      std::vector<uint32_t> selected_indices;
      for (uint32_t i = 0; i < path_segments.size (); ++i)
        {
          selected_indices.push_back (i);
        }

      std::shuffle (selected_indices.begin (), selected_indices.end (), std::random_device{});

      uint32_t j = 0;
      while (set_of_selected_paths_per_dst_ia.size () < number_of_paths_to_use_for_global_sync &&
             set_of_selected_paths_per_dst_ia.size () < path_segments.size ())
        {
          auto iter = path_segments.cbegin ();
          std::advance (iter, selected_indices.at (j));

          if (!(iter->second->hops.size () > 2 && min_len == 2))
            {
              set_of_selected_paths_per_dst_ia.insert (iter->second);
            }

          j++;
          if (j >= path_segments.size ())
            {
              break;
            }
        }
    }
}

void
TimeServer::ReadSetOfDisjointPaths ()
{
  nlohmann::json set_of_disjoint_paths_json;
  std::ifstream disjoint_paths_file (set_of_disjoint_paths_file);
  disjoint_paths_file >> set_of_disjoint_paths_json;
  disjoint_paths_file.close ();

  for (auto const &[dst_ia_str, path_segs_json] : set_of_disjoint_paths_json.items ())
    {
      ia_t dst_ia = std::stoi (dst_ia_str);

      set_of_selected_paths.insert (
          std::make_pair (dst_ia, std::unordered_set<const PathSegment *> ()));

      for (auto const &path_seg_json : path_segs_json)
        {
          PathSegment *path_seg = new PathSegment ();
          path_seg->initiation_time = path_seg_json["initiation_time"];
          path_seg->expiration_time = path_seg_json["expiration_time"];
          path_seg->originator = path_seg_json["originator"];
          path_seg->reverse = path_seg_json["reverse"];
          path_seg->hops = path_seg_json["hops"].get<std::vector<uint64_t>> ();
          set_of_selected_paths.at (dst_ia).insert (path_seg);
        }
    }
}

void
TimeServer::WriteSetOfDisjointPaths ()
{
  nlohmann::json set_of_disjoint_paths_json;

  for (auto const &[dst_ia, path_segs] : set_of_selected_paths)
    {
      nlohmann::json path_segs_json;
      for (auto const &path_seg : path_segs)
        {
          nlohmann::json path_seg_json;
          path_seg_json["initiation_time"] = 0;
          path_seg_json["expiration_time"] = 0xFFFF;
          path_seg_json["originator"] = path_seg->originator;
          path_seg_json["reverse"] = 0;
          path_seg_json["hops"] = nlohmann::json (path_seg->hops);
          path_segs_json.push_back (path_seg_json);
        }
      set_of_disjoint_paths_json[std::to_string (dst_ia)] = path_segs_json;
    }

  std::ofstream disjoint_paths_file (set_of_disjoint_paths_file);
  disjoint_paths_file << set_of_disjoint_paths_json.dump ();
  disjoint_paths_file.close ();
}

void
TimeServer::ReceiveSetOfAllCoreAsesFromOtherTimeServer (ScionPacket *packet)
{
  if (*packet->payload.list_of_all_ases.set_of_all_ases != set_of_all_core_ases)
    {
      std::vector<ia_t> v1 (set_of_all_core_ases.begin (), set_of_all_core_ases.end ());
      std::vector<ia_t> v2 (packet->payload.list_of_all_ases.set_of_all_ases->begin (),
                            packet->payload.list_of_all_ases.set_of_all_ases->end ());
      std::vector<ia_t> result (v1.size () + v2.size ());
      std::vector<ia_t>::iterator it =
          std::set_union (v1.begin (), v1.end (), v2.begin (), v2.end (), result.begin ());
      std::set<ia_t> result_set (result.begin (), it);
      set_of_all_core_ases = result_set;

      SendSetOfAllCoreAsesToNeighbors ();
    }
}

void
TimeServer::RequestForPathsToAllCoreAses ()
{
  std::set<uint16_t> all_isds;

  for (auto const &isd_as : set_of_all_core_ases)
    {
      if (isd_as == ia_addr)
        {
          continue;
        }

      all_isds.insert (GET_ISDN (isd_as));
    }

  for (auto const &isd : all_isds)
    {
      NS_LOG_FUNCTION ("TimeSrv at " << isd_number << ":" << as_number
                                     << " send req for paths to isd " << isd);
      if (isd == isd_number)
        {
          SendRequestForPathSegments (PathSegmentType::CORE_SEG, 0, 0);
        }
      else
        {
          SendRequestForPathSegments (PathSegmentType::CORE_SEG, 0, MAKE_IA (isd, 0));
        }
    }
}

void
TimeServer::SendSetOfAllCoreAsesToNeighbors ()
{
  std::set<const PathSegment *> paths_to_neighbor_ases;

  for (auto const &dst_ia_cached_paths_pair : cached_core_path_segments)
    {
      for (auto const &[exp_time, path_seg] : *dst_ia_cached_paths_pair.second->at (ia_addr))
        {
          if (exp_time > local_time.GetMinutes ())
            {
              if (path_seg->hops.size () == 2)
                {
                  paths_to_neighbor_ases.insert (path_seg);
                }
            }
        }
    }

  for (auto const &path : paths_to_neighbor_ases)
    {
      NS_LOG_FUNCTION ("TimeSrv at " << isd_number << ":" << as_number
                                     << " sent list of all ases to "
                                     << GET_HOP_ISD (path->hops.back ()) << ":"
                                     << GET_HOP_AS (path->hops.back ()));
      PayloadType payload_type = PayloadType::BROADCAST_LIST_OF_ALL_CORE_ASES;

      Payload payload;
      payload.list_of_all_ases.set_of_all_ases = &set_of_all_core_ases;

      std::vector<const PathSegment *> the_path;
      the_path.push_back (path);

      ScionPacket *packet =
          CreateScionPacket (payload, payload_type, GET_HOP_IA (path->hops.back ()), 2,
                             set_of_all_core_ases.size () * 8, the_path);

      SendScionPacket (packet);
    }
}

void
TimeServer::AdvanceLocalTime ()
{
  Time advance = Simulator::Now () - real_time_of_last_time_advance;

  if (advance == 0)
    {
      return;
    }

  int64_t drift_int = GetDrift (advance);

  Time tmp_local_time = local_time;

  local_time += advance;
  if (drift_int < 0)
    {
      local_time -= TimeStep (std::abs (drift_int));
      NS_LOG_FUNCTION ("ia_addr: " << isd_number << "-" << as_number << ", local_time: "
                                   << tmp_local_time << ", updated_local_time: " << local_time
                                   << ", last update: " << real_time_of_last_time_advance
                                   << ", advance: " << advance << ", random_drift: -"
                                   << TimeStep (std::abs (drift_int)));
    }
  else
    {
      local_time += TimeStep (std::abs (drift_int));
      NS_LOG_FUNCTION ("ia_addr: " << isd_number << "-" << as_number << ", local_time: "
                                   << tmp_local_time << ", updated_local_time: " << local_time
                                   << ", last update: " << real_time_of_last_time_advance
                                   << ", advance: " << advance << ", random_drift: +"
                                   << TimeStep (std::abs (drift_int)));
    }

  real_time_of_last_time_advance = Simulator::Now ();
}

Time
TimeServer::GetReferenceTime ()
{
  if (reference_time_type == ReferenceTimeType::OFF)
    {
      return local_time;
    }

  if (reference_time_type == ReferenceTimeType::MALICIOUS_REF)
    {
      if (malicious_offset_in_ps > 0)
        {
          return Simulator::Now () + TimeStep (malicious_offset_in_ps);
        }
      return Simulator::Now () - TimeStep (malicious_offset_in_ps);
    }

  return Simulator::Now ();
}

int64_t
TimeServer::GetDrift (Time duration)
{
  if (jitter_in_drift)
    {
      Time max_drift = GetMaxDrift (duration);
      std::random_device rd;
      std::uniform_int_distribution<int64_t> dist (-std::abs (max_drift.GetTimeStep ()),
                                                   std::abs (max_drift.GetTimeStep ()));
      int64_t random_drift_int = dist (rd);
      return random_drift_int;
    }

  double drift = (((double) constant_drift_per_day) * ((double) duration.GetTimeStep ())) /
                 ((double) Days (1).GetTimeStep ());

  return (int64_t) std::round (drift);
}

Time
TimeServer::GetMaxDrift (Time duration)
{
  double max_drift =
      (((double) max_drift_per_day.GetTimeStep ()) * ((double) duration.GetTimeStep ())) /
      ((double) Days (1).GetTimeStep ());
  return TimeStep ((uint64_t) std::round (max_drift));
}

void
TimeServer::TriggerCoreTimeSyncAlgo ()
{
  AdvanceLocalTime ();

  if (alg_v == AlgV::V4 || synchronization_round != 0 || alg_v == AlgV::LOCAL_SYNC)
    {
      int64_t loff = GetReferenceTime ().GetTimeStep () - local_time.GetTimeStep ();
      double coefficient = (alg_v == AlgV::V4) ? 1.25 : 1.0;
      CorrectLocalTime (loff, time_sync_period, coefficient);
    }

  if (synchronization_round == 0 && alg_v != AlgV::LOCAL_SYNC)
    {
      SendNtpReqToPeers ();
      if (parallel_scheduler)
        {
          Simulator::Schedule (Time (NTP_REQ_GLOBAL_SYNC_DIFF),
                               &RunParallelEvents<void (TimeServer::*) (), TimeServer *>,
                               local_address, &TimeServer::ContinueGlobalTimeSync);
        }
      // ********************************* Debug: To print goffsets **********************************************************
      //            Simulator::Schedule(Time(NTP_REQ_GLOBAL_SYNC_DIFF), &TimeServer::ContinueGlobalTimeSync, this);
      // *********************************************************************************************************************
    }
  synchronization_round = (synchronization_round + 1) % g;
}

void
TimeServer::ContinueGlobalTimeSync ()
{
  AdvanceLocalTime ();

  int32_t n = set_of_all_core_ases.size ();
  int32_t f = std::floor ((n - 1) / 3);

  int64_t loff;
  loff =
      (alg_v == AlgV::V4) ? 0 : (GetReferenceTime ().GetTimeStep () - local_time.GetTimeStep ());
  int64_t corr = loff;

  std::multiset<int64_t> off;
  off.insert (loff);

  for (auto const &peer_ia : set_of_all_core_ases)
    {
      if (peer_ia == ia_addr)
        {
          continue;
        }

      if (poff.find (peer_ia) == poff.end ())
        {
          off.insert (loff);
        }
      else
        {
          int64_t median_off = (int64_t) std::round (GetMedian (poff.at (peer_ia)));
          off.insert (median_off);
        }
    }

  auto iter1 = off.cbegin ();
  auto iter2 = off.cbegin ();
  std::advance (iter1, f);
  std::advance (iter2, n - 1 - f);

  int64_t goff = std::floor ((*iter1 + *iter2) / 2);
  int64_t doff = loff - goff;

  //***************************** Debug: To print goffsets ***************************************************************
  //        std::cout << " ************************************* " << std::endl;
  //        std::cout << "goff: " << goff / 1000000000.0 <<
  //                   ", real_time_diff: " << (Simulator::Now().GetTimeStep() - local_time.GetTimeStep()) / 1000000000.0 << std::endl;
  //        for (auto const & an_off : off) {
  //            std::cout << an_off / 1000000000.0 << std::endl;
  //        }
  //**********************************************************************************************************************

  if (std::abs (doff) > std::abs (global_cut_off.GetTimeStep ()))
    {
      if (alg_v == AlgV::V1 || alg_v == AlgV::V2)
        {
          doff = doff > 0 ? std::abs (global_cut_off.GetTimeStep ())
                          : -std::abs (global_cut_off.GetTimeStep ());
          corr = goff + doff;
        }
      else if (alg_v == AlgV::V3 || alg_v == AlgV::V4)
        {
          corr = goff;
        }
    }

  double coefficient = (alg_v == AlgV::V4) ? 2.5 : 1.0;
  Time duration = (alg_v == AlgV::V1) ? time_sync_period : (g * time_sync_period);
  CorrectLocalTime (corr, duration, coefficient);

  poff.clear ();

  if (path_selection == "random")
    {
      ConstructSetOfSelectedPaths ();
    }
}

void
TimeServer::CorrectLocalTime (int64_t corr, Time duration, double coefficient)
{
  Time max_drift = GetMaxDrift (duration);
  int64_t max_corr = std::abs ((int64_t) std::round (coefficient * max_drift.GetTimeStep ()));
  int64_t final_corr_abs = (std::abs (corr) < max_corr) ? std::abs (corr) : max_corr;

  Time tmp_local_time = local_time;

  if (corr > 0)
    {
      local_time += TimeStep (final_corr_abs);
      NS_LOG_FUNCTION ("ia_addr: " << isd_number << "-" << as_number
                                   << ", local_time: " << tmp_local_time
                                   << ", updated_local_time: " << local_time << ", final_corr: +"
                                   << TimeStep (final_corr_abs) << ", max_drift: " << max_drift
                                   << ", corr: +" << TimeStep (std::abs (corr)));
    }
  else
    {
      local_time -= TimeStep (final_corr_abs);
      NS_LOG_FUNCTION ("ia_addr: " << isd_number << "-" << as_number
                                   << ", local_time: " << tmp_local_time
                                   << ", updated_local_time: " << local_time << ", final_corr: -"
                                   << TimeStep (final_corr_abs) << ", max_drift: " << max_drift
                                   << ", corr: -" << TimeStep (std::abs (corr)));
    }
}

void
TimeServer::SendNtpReqToPeers ()
{
  NS_LOG_FUNCTION ("ia_addr: " << isd_number << "-" << as_number << ", local_time: " << local_time);

  for (auto const &peer_ia : set_of_all_core_ases)
    {
      if (peer_ia == ia_addr)
        {
          continue;
        }

      for (auto const &path_seg : set_of_selected_paths.at (peer_ia))
        {
          NS_LOG_FUNCTION ("TimeSrv at " << isd_number << ":" << as_number << " sent ntp req to "
                                         << GET_ISDN (peer_ia) << ":" << GET_ASN (peer_ia));
          PayloadType payload_type = PayloadType::NTP_REQ;

          Payload payload;
          payload.ntp_req_or_resp.t0 = local_time.GetTimeStep ();

          std::vector<const PathSegment *> the_path;
          the_path.push_back (path_seg);

          ScionPacket *packet = CreateScionPacket (payload, payload_type, peer_ia, 2,
                                                   8 + 48 /* udp + ntp*/, the_path);

          SendScionPacket (packet);
        }
    }
}

void
TimeServer::ModifyPktUponSend (ScionPacket *packet)
{
  if (packet->payload_type == PayloadType::NTP_REQ)
    {
      packet->payload.ntp_req_or_resp.t0 = local_time.GetTimeStep ();
      return;
    }

  if (packet->payload_type == PayloadType::NTP_RESP)
    {
      packet->payload.ntp_req_or_resp.t2 = local_time.GetTimeStep ();
      return;
    }
}

void
TimeServer::ReceiveNtpReqFromPeer (ScionPacket *packet, Time receive_time)
{
  int64_t t0 = packet->payload.ntp_req_or_resp.t0;
  NS_LOG_FUNCTION ("ia_addr: " << isd_number << "-" << as_number << ", local_time: " << local_time
                               << ", sender_ia: " << GET_ISDN (packet->src_ia) << "-"
                               << GET_ASN (packet->src_ia) << ", receive_time: " << receive_time
                               << ", t0: " << (t0 < 0 ? "-" : "+") << TimeStep (std::abs (t0)));

  packet->payload_type = PayloadType::NTP_RESP;
  packet->payload.ntp_req_or_resp.t1 = receive_time.GetTimeStep ();
  packet->payload.ntp_req_or_resp.t2 = local_time.GetTimeStep ();
  ReturnScionPacket (packet);
}

void
TimeServer::ReceiveNtpResFromPeer (ScionPacket *packet, Time receive_time)
{
  int64_t t0 = packet->payload.ntp_req_or_resp.t0;
  int64_t t1 = packet->payload.ntp_req_or_resp.t1;
  int64_t t2 = packet->payload.ntp_req_or_resp.t2;

  NS_LOG_FUNCTION ("TimeServ at " << isd_number << ":" << as_number << " RCV NTP resp from peer "
                                  << GET_ISDN (packet->src_ia) << ":" << GET_ASN (packet->src_ia)
                                  << ", local_time: " << local_time
                                  << ", t0: " << (t0 < 0 ? "-" : "+") << TimeStep (std::abs (t0))
                                  << ", t1: " << (t1 < 0 ? "-" : "+") << TimeStep (std::abs (t1))
                                  << ", t2: " << (t2 < 0 ? "-" : "+") << TimeStep (std::abs (t2))
                                  << ", t3: " << receive_time);

  int64_t poff_tmp = ((packet->payload.ntp_req_or_resp.t1 - packet->payload.ntp_req_or_resp.t0) +
                      (packet->payload.ntp_req_or_resp.t2 - receive_time.GetTimeStep ())) /
                     2;

  if (poff.find (packet->src_ia) == poff.end ())
    {
      poff.insert (std::make_pair (packet->src_ia, std::multiset<int64_t> ()));
    }

  poff.at (packet->src_ia).insert (poff_tmp);
}

void
TimeServer::ResetTime ()
{
  real_time_of_last_time_advance = Simulator::Now ();

  local_time = Simulator::Now ();

  if (max_initial_drift.GetTimeStep () == 0)
    {
      return;
    }

  std::random_device rd;
  std::uniform_int_distribution<int64_t> dist (-std::abs (max_initial_drift.GetTimeStep ()),
                                               std::abs (max_initial_drift.GetTimeStep ()));
  int64_t random_drift_int = dist (rd);

  if (random_drift_int < 0)
    {
      local_time -= TimeStep (std::abs (random_drift_int));
    }
  else
    {
      local_time += TimeStep (std::abs (random_drift_int));
    }
}

void
TimeServer::CaptureLocalSnapshot ()
{
  AdvanceLocalTime ();

  if (parallel_scheduler)
    {
      std::cout << "##################################### Snapshot at "
                << Simulator::Now ().GetTimeStep () << " ##################################"
                << std::endl;
    }

  if (reference_time_type == ReferenceTimeType::MALICIOUS_REF)
    {
      std::cout << "AS m " << isd_number << "-" << as_number << ": " << local_time << std::endl;
    }
  else
    {
      std::cout << "AS " << isd_number << "-" << as_number << ", degree "
                << as->interfaces_per_neighbor_as.size () << ": " << local_time << std::endl;
    }
}

void
TimeServer::CaptureOffsetDiff ()
{
  AdvanceLocalTime ();

  std::cout << "##################################### Snapshot at "
            << Simulator::Now ().GetTimeStep () << " ##################################"
            << std::endl;
  std::cout << "poff_size: " << poff.size () << std::endl;

  int32_t n = nodes.GetN ();
  int32_t f = std::floor ((n - 1) / 3);

  int64_t loff = GetReferenceTime ().GetTimeStep () - local_time.GetTimeStep ();

  std::multiset<int64_t> off;
  off.insert (loff);

  for (uint32_t i = 0; i < nodes.GetN (); ++i)
    {
      ScionAs *node = dynamic_cast<ScionAs *> (PeekPointer (nodes.Get (i)));
      if (node->ia_addr == ia_addr)
        {
          continue;
        }

      if (poff.find (node->ia_addr) == poff.end ())
        {
          std::cout << "NOT FOUND" << std::endl;
          continue;
        }

      dynamic_cast<TimeServer *> (node->GetHost (local_address))->AdvanceLocalTime ();
      Time remote_local_time =
          dynamic_cast<TimeServer *> (node->GetHost (local_address))->GetLocalTime ();
      int64_t time_diff = (remote_local_time - local_time).GetTimeStep ();

      int64_t median_off = (int64_t) std::round (GetMedian (poff.at (node->ia_addr)));
      off.insert (median_off);

      for (auto const &offset : poff.at (node->ia_addr))
        {
          int64_t diff = offset - time_diff;
          std::cout << diff << "\t";
        }

      std::cout << std::endl;
    }

  auto iter1 = off.cbegin ();
  auto iter2 = off.cbegin ();
  std::advance (iter1, f);
  std::advance (iter2, n - 1 - f);

  int64_t goff = std::floor ((*iter1 + *iter2) / 2);

  std::cout << "goff: " << goff << ", loff: " << loff << std::endl;
}

void
TimeServer::CompareOffsWithRealOffs ()
{
  AdvanceLocalTime ();
  for (uint32_t i = 0; i < nodes.GetN (); ++i)
    {
      ScionAs *node = dynamic_cast<ScionAs *> (PeekPointer (nodes.Get (i)));
      if (node->ia_addr == ia_addr)
        {
          continue;
        }

      if (poff.find (node->ia_addr) == poff.end ())
        {
          std::cout << "NOT FOUND" << std::endl;
          continue;
        }

      dynamic_cast<TimeServer *> (node->GetHost (local_address))->AdvanceLocalTime ();
      Time remote_local_time =
          dynamic_cast<TimeServer *> (node->GetHost (local_address))->GetLocalTime ();
      int64_t time_diff = (remote_local_time - local_time).GetTimeStep ();

      int64_t remote_drift = dynamic_cast<TimeServer *> (node->GetHost (local_address))
                                 ->GetDrift (Time (NTP_REQ_GLOBAL_SYNC_DIFF));
      int64_t local_drift = GetDrift (Time (NTP_REQ_GLOBAL_SYNC_DIFF));

      int64_t offset = *poff.at (node->ia_addr).begin ();

      int64_t upper_bound = std::abs (remote_drift) + std::abs (local_drift);

      NS_ASSERT_MSG (std::abs (offset - time_diff) < upper_bound,
                     "offset: " << TimeStep (offset) << ", real time diff: " << TimeStep (time_diff)
                                << ", upper bound: " << TimeStep (upper_bound));
    }
}

void
TimeServer::ScheduleListOfAllASesRequest ()
{
  if (read_disjoint_paths == ReadOrWriteDisjointPaths::W)
    {
      Simulator::Schedule (first_event - Time (PATH_RQ_TIME_SYNC_DIFF),
                           &TimeServer::RequestSetOfAllCoreAsesFromPathServer, this);
    }
  else
    {
      for (Time t = first_event; t < last_event; t += list_of_ases_req_period)
        {
          Time diff_with_first_event = t - first_event;
          if (diff_with_first_event.GetTimeStep () % time_sync_period.GetTimeStep () == 0)
            {
              Simulator::Schedule (t - Time (PATH_RQ_TIME_SYNC_DIFF),
                                   &TimeServer::RequestSetOfAllCoreAsesFromPathServer,
                                   this);
            }
          else
            {
              Simulator::Schedule (t, &TimeServer::RequestSetOfAllCoreAsesFromPathServer,
                                   this);
            }
        }
    }
}

void
TimeServer::ScheduleTimeSync ()
{
  if (read_disjoint_paths == ReadOrWriteDisjointPaths::R ||
      read_disjoint_paths == ReadOrWriteDisjointPaths::NO_R_NO_W)
    {
      Simulator::Schedule (first_event, &TimeServer::ResetTime, this);
      for (Time t = first_event; t < last_event; t += time_sync_period)
        {
          Simulator::Schedule (t, &TimeServer::TriggerCoreTimeSyncAlgo, this);
        }
    }
}

void
TimeServer::ScheduleSnapShots ()
{
  if (snapshot_type == SnapshotType::SNAPSHOT_OFF)
    {
      return;
    }

  if (snapshot_type == SnapshotType::LOCAL_SNAPSHOT)
    {
      Simulator::Schedule (first_event + TimeStep (1), &TimeServer::CaptureLocalSnapshot, this);
    }

  for (Time t = first_event; t < last_event; t += snapshot_period)
    {
      Time diff_with_first_event = t - first_event;
      if (diff_with_first_event.GetTimeStep () % time_sync_period.GetTimeStep () == 0)
        {
          if ((diff_with_first_event.GetTimeStep () / time_sync_period.GetTimeStep ()) % g == 0)
            {
              if (snapshot_type == SnapshotType::LOCAL_SNAPSHOT)
                {
                  Simulator::Schedule (t + Time (NTP_REQ_GLOBAL_SYNC_DIFF) + TimeStep (1),
                                       &TimeServer::CaptureLocalSnapshot, this);
                }
              else if (snapshot_type == SnapshotType::PRINT_OFFSET_DIFF)
                {
                  Simulator::Schedule (t + Time (NTP_REQ_GLOBAL_SYNC_DIFF),
                                       &TimeServer::CaptureOffsetDiff, this);
                }
              else if (snapshot_type == SnapshotType::ASSERT_OFFSET_DIFF)
                {
                  Simulator::Schedule (t + Time (NTP_REQ_GLOBAL_SYNC_DIFF),
                                       &TimeServer::CompareOffsWithRealOffs, this);
                }
            }
          else
            {
              if (snapshot_type == SnapshotType::LOCAL_SNAPSHOT)
                {
                  Simulator::Schedule (t + TimeStep (1), &TimeServer::CaptureLocalSnapshot, this);
                }
            }
        }
      else
        {
          if (snapshot_type == SnapshotType::LOCAL_SNAPSHOT)
            {
              Simulator::Schedule (t, &TimeServer::CaptureLocalSnapshot, this);
            }
        }
    }
}

int64_t
TimeServer::GetConstantDriftPerDay ()
{
  return constant_drift_per_day;
}
} // namespace ns3
