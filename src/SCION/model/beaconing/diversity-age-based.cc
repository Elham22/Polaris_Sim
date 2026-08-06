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
 * Author: Seyedali Tabaeiaghdaei seyedali.tabaeiaghdaei@inf.ethz.ch,
 *         Christelle Gloor  christelle.gloor@inf.ethz.ch
 */

#include <omp.h>

#include "ns3/point-to-point-channel.h"

#include "diversity-age-based.h"
#include "src/SCION/model/scion-as.h"
#include "src/SCION/model/utils.h"

namespace ns3 {

void
DiversityAgeBased::DoInitializations (uint32_t num_ases, rapidxml::xml_node<> *xml_node,
                                      const YAML::Node &config)
{
  BeaconServer::DoInitializations (num_ases, xml_node, config);

  sent_beacons.resize (as->GetNDevices ());
  sent_beacons_cnt.resize (num_ases);

  for (uint32_t i = 0; i < as->GetNDevices (); ++i)
    {
      sent_beacons.at (i) = new std::unordered_map<Beacon *, std::pair<float, uint16_t>> ();
    }

  for (uint16_t i = 0; i < (uint16_t) num_ases; ++i)
    {
      sent_beacons_cnt.at (i) = new std::unordered_map<uint16_t, uint16_t> ();
      for (auto const &neighbor_ifaces_pair : as->interfaces_per_neighbor_as)
        {
          uint16_t neighbor = neighbor_ifaces_pair.first;
          sent_beacons_cnt.at (i)->insert (std::make_pair (neighbor, 0));
        }
    }

  for (uint32_t i = 0; i < as->neighbors.size (); ++i)
    {
      uint16_t neighbor_as_no = as->neighbors.at (i).first;
      links_jointnesses_on_sent_paths.insert (std::make_pair (
          neighbor_as_no, std::vector<std::unordered_map<uint32_t, uint32_t> *> ()));
      links_jointnesses_on_sent_paths.at (neighbor_as_no).resize (num_ases);
      links_jointnesses_on_received_paths.resize (num_ases);
      for (uint32_t j = 0; j < num_ases; ++j)
        {
          links_jointnesses_on_sent_paths.at (neighbor_as_no).at (j) =
              new std::unordered_map<uint32_t, uint32_t> ();
          links_jointnesses_on_received_paths.at (j) =
              new std::unordered_map<uint32_t, uint32_t> ();
        }
    }
}

void
DiversityAgeBased::CreateInitialStaticInfoExtension (
    static_info_extension_t &static_info_extension, uint16_t self_egress_if_no,
    const OptimizationTarget *optimization_target)
{
  static_info_extension.insert (std::make_pair (StaticInfoType::LATENCY, 0));
}

void
DiversityAgeBased::DisseminateBeacons (NeighbourRelation relation)
{
  uint32_t neighbors_cnt = as->neighbors.size ();
  omp_set_num_threads (num_core);
#pragma omp parallel for
  for (uint32_t i = 0; i < neighbors_cnt; ++i)
    { // Per neighbor AS

      if (as->neighbors.at (i).second != relation)
        {
          continue;
        }

      uint16_t remote_as_no = as->neighbors.at (i).first;
      for (auto const &dst_as_beacons_pair : beacon_store)
        { // Per destination AS
          uint16_t dst_as_no = dst_as_beacons_pair.first;
          const beacons_with_same_dst_as &beacons_to_the_dst_as = dst_as_beacons_pair.second;

          if (remote_as_no == dst_as_no)
            {
              continue;
            }

          std::multimap<
              ld, std::tuple<Beacon *, uint16_t, uint16_t, ScionAs *, static_info_extension_t>>
              selected_beacons = SelectBeaconsToDisseminatePerDstPerNbr (remote_as_no, dst_as_no,
                                                                         beacons_to_the_dst_as);

          for (auto const &the_tuple_pair : selected_beacons)
            {
              Beacon *the_beacon;
              uint16_t remote_ingress_if_no;
              uint16_t self_egress_if_no;
              ScionAs *remote_as;
              static_info_extension_t static_info_extension;

              std::tie (the_beacon, self_egress_if_no, remote_ingress_if_no, remote_as,
                        static_info_extension) = the_tuple_pair.second;

              GenerateBeaconAndSend (the_beacon, self_egress_if_no, remote_ingress_if_no, remote_as,
                                     static_info_extension);
            }
        }
    }
}

std::tuple<bool, bool, bool, Beacon *, ld>
DiversityAgeBased::AlgSpecificImportPolicy (Beacon &the_beacon, uint16_t sender_as,
                                               uint16_t remote_egress_if_no,
                                               uint16_t self_ingress_if_no, uint16_t now)
{
  uint16_t dst_as = UPPER_16_BITS (the_beacon.the_path.at (0));

  if (next_round_valid_beacons_count_per_dst_as.find (dst_as) ==
      next_round_valid_beacons_count_per_dst_as.end ())
    {
      return std::tuple<bool, bool, bool, Beacon *, ld> (true, false, false, NULL, 0);
    }

  if (next_round_valid_beacons_count_per_dst_as.at (dst_as) < MAX_BEACONS_TO_STORE)
    {
      return std::tuple<bool, bool, bool, Beacon *, ld> (true, false, false, NULL, 0);
    }

  ld raw_score = CalculateImportRawScore (the_beacon);
  ld beacon_age = (ld) (now - the_beacon.next_initiation_time);
  ld beacon_exp_period = (ld) (the_beacon.next_expiration_time - the_beacon.next_initiation_time);
  ld score = std::pow (raw_score, ALPHA * (beacon_age / beacon_exp_period));

  if (next_round_valid_beacons_count_per_dst_as.at (dst_as) < MAX_BEACONS_TO_STORE && score > 0.9)
    {
      return std::tuple<bool, bool, bool, Beacon *, ld> (true, false, false, NULL, 0);
    }

  return std::tuple<bool, bool, bool, Beacon *, ld> (false, false, false, NULL, 0);
}

void
DiversityAgeBased::DeleteFromAlgorithmDataStructures (Beacon *the_beacon, ld replacement_key)
{
  DecLinksJointnessesOnReceivedPaths (the_beacon, UPPER_16_BITS (the_beacon->the_path.at (0)));
}

void
DiversityAgeBased::InsertToAlgorithmDataStructures (Beacon *the_beacon, uint16_t sender_as,
                                                        uint16_t remote_egress_if_no,
                                                        uint16_t self_ingress_if_no)
{
  IncLinksJointnessOnReceivedPaths (UPPER_16_BITS (the_beacon->the_path.at (0)), the_beacon);
}

void
DiversityAgeBased::UpdateAlgorithmDataStructuresPeriodic (Beacon *the_beacon, bool invalidated)
{
  uint16_t dst_as = UPPER_16_BITS (the_beacon->the_path.at (0));
  RemoveInvalidSentBeacons (the_beacon, dst_as);

  if (invalidated)
    {
      DecLinksJointnessesOnReceivedPaths (the_beacon, dst_as);
    }
}

std::multimap<ld, std::tuple<Beacon *, uint16_t, uint16_t, ScionAs *, static_info_extension_t>>
DiversityAgeBased::SelectBeaconsToDisseminatePerDstPerNbr (
    uint16_t remote_as_no, uint16_t dst_as_no,
    const beacons_with_same_dst_as &beacons_to_the_dst_as)
{
  std::multimap<ld, std::tuple<Beacon *, uint16_t, uint16_t, ScionAs *, static_info_extension_t>>
      score_map_to_beacon_and_metadata;
  std::map<std::pair<Beacon *, uint16_t>, std::pair<ld, ld>> valid_candidates;

  ld max_score = 0.0;
  ld max_score_raw_score = 0.0;
  Beacon *max_score_beacon = NULL;
  uint16_t max_score_iface = 0;

  uint16_t remote_ingress_if_no;
  ScionAs *remote_as =
      as->GetRemoteAsInfo (as->interfaces_per_neighbor_as.at (remote_as_no).at (0)).second;

  uint32_t min_no_paths_to_send = (20 * as->interfaces_per_neighbor_as.at (remote_as_no).size ()) /
                                  remote_as->interfaces_coordinates.size ();

  for (auto const &len_beacons_pair : beacons_to_the_dst_as)
    {
      auto const &beacons = len_beacons_pair.second;
      for (auto const &the_beacon : beacons)
        {
          if (!the_beacon->is_valid)
            {
              continue;
            }

          bool generates_loop = false;
          for (auto const &link_info : the_beacon->the_path)
            { // remove loops
              if (UPPER_16_BITS (link_info) == remote_as_no)
                {
                  generates_loop = true;
                  break;
                }
            }

          if (generates_loop)
            {
              continue;
            }

          auto const &interfaces = as->interfaces_per_neighbor_as.at (remote_as_no);
          for (auto const &self_egress_if_no : interfaces)
            {
              ld raw_score = 0.0;
              ld score = 0.0;
              bool must_be_added = false;
              if (PathNotSentBefore (remote_as_no, self_egress_if_no, the_beacon))
                {
                  raw_score =
                      CalculateRawScore (the_beacon, dst_as_no, self_egress_if_no, remote_as);
                  ld beacon_age = (ld) (now - the_beacon->initiation_time);
                  ld beacon_exp_period =
                      (ld) (the_beacon->expiration_time - the_beacon->initiation_time);
                  score = std::pow (raw_score, ALPHA * (beacon_age / beacon_exp_period));

                  if (sent_beacons_cnt.at (dst_as_no)->at (remote_as_no) < min_no_paths_to_send &&
                      valid_candidates.size () < min_no_paths_to_send)
                    {
                      must_be_added = true;
                    }
                }
              else
                {
                  raw_score = sent_beacons.at (self_egress_if_no)->at (the_beacon).first;
                  ld sent_beacon_time_to_expiration =
                      (ld) (sent_beacons.at (self_egress_if_no)->at (the_beacon).second - now);
                  ld current_beacon_time_to_expiration = (ld) (the_beacon->expiration_time - now);
                  score = std::pow (raw_score, std::pow (BETA * (sent_beacon_time_to_expiration /
                                                                 current_beacon_time_to_expiration),
                                                         GAMMA));
                }

              if (!must_be_added && score < SCORE_THRESHOLD)
                {
                  continue;
                }

              if (score > max_score)
                {
                  max_score = score;
                  max_score_raw_score = raw_score;
                  max_score_beacon = the_beacon;
                  max_score_iface = self_egress_if_no;
                }

              valid_candidates.insert (
                  std::make_pair (std::make_pair (the_beacon, self_egress_if_no),
                                  std::make_pair (raw_score, score)));
            }
        }
    }

  bool counters_changed = false;

  for (uint32_t i = 0; i < MAX_BEACONS_TO_SEND; ++i)
    {
      if (max_score_beacon == NULL)
        {
          break;
        }
      else
        {
          valid_candidates.erase (std::make_pair (max_score_beacon, max_score_iface));

          remote_ingress_if_no = as->GetRemoteAsInfo (max_score_iface).first;

          ld latency = max_score_beacon->static_info_extension.at (StaticInfoType::LATENCY) +
                       as->latencies_between_interfaces
                           .at (LOWER_16_BITS (max_score_beacon->the_path.back ()))
                           .at (max_score_iface);

          static_info_extension_t static_info_extension;
          static_info_extension.insert (std::make_pair (StaticInfoType::LATENCY, latency));

          score_map_to_beacon_and_metadata.insert (std::make_pair (
              max_score,
              std::tuple<Beacon *, uint16_t, uint16_t, ScionAs *, static_info_extension_t> (
                  max_score_beacon, max_score_iface, remote_ingress_if_no, remote_as,
                  static_info_extension)));

          if (PathNotSentBefore (remote_as_no, max_score_iface, max_score_beacon))
            {
              counters_changed = true;
              AddToSentBeacons (dst_as_no, remote_as_no, max_score_iface, max_score_beacon,
                                (float) max_score_raw_score);
              IncLinksJointnessOnSentPaths (dst_as_no, remote_as_no, max_score_iface,
                                            max_score_beacon);
            }
          else
            {
              counters_changed = false;
              UpdateSentBeaconTimer (remote_as_no, max_score_iface, max_score_beacon);
            }
        }

      max_score = 0.0;
      max_score_raw_score = 0.0;
      max_score_beacon = NULL;
      max_score_iface = 0;

      for (auto const &candidate : valid_candidates)
        {
          Beacon *the_beacon = candidate.first.first;
          uint16_t self_egress_if_no = candidate.first.second;
          ld score = 0.0;
          ld raw_score = 0.0;

          bool must_be_sent =
              PathNotSentBefore (remote_as_no, self_egress_if_no, the_beacon) &&
              sent_beacons_cnt.at (dst_as_no)->at (remote_as_no) < min_no_paths_to_send;

          if (!counters_changed ||
              !PathNotSentBefore (remote_as_no, self_egress_if_no, the_beacon))
            {
              raw_score = candidate.second.first;
              score = candidate.second.second;
            }
          else
            {
              raw_score = CalculateRawScore (the_beacon, dst_as_no, self_egress_if_no, remote_as);
              ld beacon_age = (ld) (now - the_beacon->initiation_time);
              ld beacon_exp_period =
                  (ld) (the_beacon->expiration_time - the_beacon->initiation_time);
              score = std::pow (raw_score, ALPHA * (beacon_age / beacon_exp_period));
              valid_candidates.at (std::make_pair (the_beacon, self_egress_if_no)) =
                  std::make_pair (raw_score, score);
            }

          if (!must_be_sent && score < SCORE_THRESHOLD)
            {
              continue;
            }

          if (score > max_score)
            {
              max_score = score;
              max_score_raw_score = raw_score;
              max_score_beacon = the_beacon;
              max_score_iface = self_egress_if_no;
            }
        }
    }
  return score_map_to_beacon_and_metadata;
}

inline ld
DiversityAgeBased::CalculateRawScore (Beacon *the_beacon, uint16_t dst_as_no,
                                        uint16_t self_egress_if_no, ScionAs *remote_as)
{
  ld link_diversity_score = CalculateLinkDiversityScoreForDissemination (
      remote_as->as_number, dst_as_no, self_egress_if_no, the_beacon);
  ld raw_score = link_diversity_score * SCALING_FACTOR;
  return raw_score;
}

inline ld
DiversityAgeBased::CalculateImportRawScore (Beacon &the_beacon)
{
  uint16_t dst_as_no = UPPER_16_BITS (the_beacon.the_path.at (0));
  ld link_diversity_score = CalculateLinkDiversityScoreForImport (dst_as_no, the_beacon);
  ld raw_score = link_diversity_score * SCALING_FACTOR;
  return raw_score;
}

void
DiversityAgeBased::UpdateSentBeaconTimer (uint16_t remote_as, uint16_t self_egress_if_no,
                                             Beacon *the_beacon)
{
  uint16_t new_exp_time = the_beacon->expiration_time;
  float raw_score = sent_beacons.at (self_egress_if_no)->at (the_beacon).first;
  sent_beacons.at (self_egress_if_no)->at (the_beacon) = std::make_pair (raw_score, new_exp_time);
}

void
DiversityAgeBased::IncLinksJointnessOnSentPaths (uint16_t dst_as_no, uint16_t remote_as_no,
                                                      uint16_t self_egress_if_no,
                                                      Beacon *the_beacon)
{
  if (the_beacon != NULL)
    {
      auto const &the_path = the_beacon->the_path;
      for (auto const &seg : the_path)
        {
          uint32_t link = UPPER_32_BITS (seg);
          if (links_jointnesses_on_sent_paths.at (remote_as_no).at (dst_as_no)->find (link) ==
              links_jointnesses_on_sent_paths.at (remote_as_no).at (dst_as_no)->end ())
            {
              links_jointnesses_on_sent_paths.at (remote_as_no)
                  .at (dst_as_no)
                  ->insert (std::make_pair (link, 0));
            }
          links_jointnesses_on_sent_paths.at (remote_as_no).at (dst_as_no)->at (link) =
              links_jointnesses_on_sent_paths.at (remote_as_no).at (dst_as_no)->at (link) + 1;
        }
    }

  uint32_t link = (((uint32_t) as->as_number) << 16) | ((uint32_t) self_egress_if_no);
  if (links_jointnesses_on_sent_paths.at (remote_as_no).at (dst_as_no)->find (link) ==
      links_jointnesses_on_sent_paths.at (remote_as_no).at (dst_as_no)->end ())
    {
      links_jointnesses_on_sent_paths.at (remote_as_no)
          .at (dst_as_no)
          ->insert (std::make_pair (link, 0));
    }
  links_jointnesses_on_sent_paths.at (remote_as_no).at (dst_as_no)->at (link) =
      links_jointnesses_on_sent_paths.at (remote_as_no).at (dst_as_no)->at (link) + 1;
}

void
DiversityAgeBased::IncLinksJointnessOnReceivedPaths (uint16_t dst_as_no, Beacon *the_beacon)
{
  auto const &the_path = the_beacon->the_path;
  for (auto const &seg : the_path)
    {
      uint32_t link = UPPER_32_BITS (seg);
      if (links_jointnesses_on_received_paths.at (dst_as_no)->find (link) ==
          links_jointnesses_on_received_paths.at (dst_as_no)->end ())
        {
          links_jointnesses_on_received_paths.at (dst_as_no)->insert (std::make_pair (link, 0));
        }
      links_jointnesses_on_received_paths.at (dst_as_no)->at (link) =
          links_jointnesses_on_received_paths.at (dst_as_no)->at (link) + 1;
    }
}

void
DiversityAgeBased::AddToSentBeacons (uint16_t dst_as_no, uint16_t remote_as,
                                        uint16_t self_egress_if_no, Beacon *the_beacon,
                                        float raw_score)
{
  sent_beacons.at (self_egress_if_no)
      ->insert (
          std::make_pair (the_beacon, std::make_pair (raw_score, the_beacon->expiration_time)));
  sent_beacons_cnt.at (dst_as_no)->at (remote_as)++;
}

ld
DiversityAgeBased::CalculateLinkDiversityScoreForDissemination (uint16_t remote_as,
                                                                     uint16_t dst_as,
                                                                     uint16_t egress_if_no,
                                                                     Beacon *the_beacon)
{
  if (links_jointnesses_on_sent_paths.at (remote_as).at (dst_as)->empty ())
    {
      return 1.0;
    }

  ld add_one = PathNotSentBefore (remote_as, egress_if_no, the_beacon) ? 1.0 : 0.0;

  ld jointness = 1.0;
  auto const &the_path = the_beacon->the_path;
  for (auto const &seg : the_path)
    {
      uint32_t link = UPPER_32_BITS (seg);
      if (links_jointnesses_on_sent_paths.at (remote_as).at (dst_as)->find (link) !=
          links_jointnesses_on_sent_paths.at (remote_as).at (dst_as)->end ())
        {
          jointness *=
              (add_one +
               1.0 * links_jointnesses_on_sent_paths.at (remote_as).at (dst_as)->at (link));
        }
    }

  uint32_t link = (((uint32_t) as->as_number) << 16) | ((uint32_t) egress_if_no);
  if (links_jointnesses_on_sent_paths.at (remote_as).at (dst_as)->find (link) !=
      links_jointnesses_on_sent_paths.at (remote_as).at (dst_as)->end ())
    {
      jointness *=
          (add_one + 1.0 * links_jointnesses_on_sent_paths.at (remote_as).at (dst_as)->at (link));
    }

  jointness = std::pow (jointness, 1.0 / (the_beacon->the_path.size () + 1.0));

  if (jointness >= MAX_ACCEPTABLE_JOINTNESS)
    {
      return 0.0;
    }
  return (MAX_ACCEPTABLE_JOINTNESS - jointness) / (MAX_ACCEPTABLE_JOINTNESS - 1.0);
}

ld
DiversityAgeBased::CalculateLinkDiversityScoreForImport (uint16_t dst_as, Beacon &the_beacon)
{
  if (links_jointnesses_on_received_paths.at (dst_as)->empty ())
    {
      return 1.0;
    }

  ld jointness = 1.0;
  auto const &the_path = the_beacon.the_path;
  for (auto const &seg : the_path)
    {
      uint32_t link = UPPER_32_BITS (seg);
      if (links_jointnesses_on_received_paths.at (dst_as)->find (link) !=
          links_jointnesses_on_received_paths.at (dst_as)->end ())
        {
          jointness *= (1.0 + 1.0 * links_jointnesses_on_received_paths.at (dst_as)->at (link));
        }
    }

  jointness = std::pow (jointness, 1.0 / (the_beacon.the_path.size () + 1.0));

  if (jointness >= MAX_ACCEPTABLE_JOINTNESS)
    {
      return 0.0;
    }
  return (MAX_ACCEPTABLE_JOINTNESS - jointness) / (MAX_ACCEPTABLE_JOINTNESS - 1.0);
}

bool
DiversityAgeBased::PathNotSentBefore (uint16_t remote_as, uint16_t self_egress_if_no,
                                         Beacon *the_beacon)
{
  if (sent_beacons.at (self_egress_if_no)->find (the_beacon) ==
      sent_beacons.at (self_egress_if_no)->end ())
    {
      return true;
    }
  return false;
}

void
DiversityAgeBased::RemoveInvalidSentBeacons (Beacon *the_beacon, uint16_t dst_as)
{
  for (uint32_t i = 0; i < as->GetNDevices (); ++i)
    {
      uint16_t remote_as_no = as->interface_to_neighbor_map.at (i);
      if (sent_beacons.at (i)->find (the_beacon) == sent_beacons.at (i)->end ())
        {
          continue;
        }

      if (sent_beacons.at (i)->at (the_beacon).second <= next_period)
        {
          sent_beacons.at (i)->erase (the_beacon);
          sent_beacons_cnt.at (dst_as)->at (as->interface_to_neighbor_map.at (i))--;
          DecLinksJointnessesOnSentPaths (the_beacon, dst_as, remote_as_no, i);
        }
    }
}

void
DiversityAgeBased::DecLinksJointnessesOnSentPaths (Beacon *the_beacon, uint16_t dst_as,
                                                        uint16_t remote_as_no,
                                                        uint16_t self_egress_if)
{
  auto const &the_path = the_beacon->the_path;
  for (auto const &seg : the_path)
    {
      uint32_t link = UPPER_32_BITS (seg);
      links_jointnesses_on_sent_paths.at (remote_as_no).at (dst_as)->at (link) =
          links_jointnesses_on_sent_paths.at (remote_as_no).at (dst_as)->at (link) - 1;
      if (links_jointnesses_on_sent_paths.at (remote_as_no).at (dst_as)->at (link) == 0)
        {
          links_jointnesses_on_sent_paths.at (remote_as_no).at (dst_as)->erase (link);
        }
    }

  uint32_t link = (((uint32_t) as->as_number) << 16) | ((uint32_t) self_egress_if);

  links_jointnesses_on_sent_paths.at (remote_as_no).at (dst_as)->at (link) =
      links_jointnesses_on_sent_paths.at (remote_as_no).at (dst_as)->at (link) - 1;
  if (links_jointnesses_on_sent_paths.at (remote_as_no).at (dst_as)->at (link) == 0)
    {
      links_jointnesses_on_sent_paths.at (remote_as_no).at (dst_as)->erase (link);
    }
}

void
DiversityAgeBased::DecLinksJointnessesOnReceivedPaths (Beacon *the_beacon, uint16_t dst_as)
{
  auto const &the_path = the_beacon->the_path;
  for (auto const &seg : the_path)
    {
      uint32_t link = UPPER_32_BITS (seg);
      links_jointnesses_on_received_paths.at (dst_as)->at (link) =
          links_jointnesses_on_received_paths.at (dst_as)->at (link) - 1;
      if (links_jointnesses_on_received_paths.at (dst_as)->at (link) == 0)
        {
          links_jointnesses_on_received_paths.at (dst_as)->erase (link);
        }
    }
}
} // namespace ns3
