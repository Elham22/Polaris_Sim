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

#include <istream>
#include <omp.h>
#include <random>
#include <set>

#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/nstime.h"
#include "ns3/point-to-point-channel.h"
#include "ns3/point-to-point-helper.h"
#include "ns3/ptr.h"

#include "src/SCION/model/apps/general-traffic-app.h"
#include "src/SCION/model/apps/rtc-app.h"
#include "src/SCION/model/beaconing/baseline.h"
#include "src/SCION/model/beaconing/beacon-server.h"
#include "src/SCION/model/beaconing/diversity-age-based.h"
#include "src/SCION/model/beaconing/green-beaconing.h"
#include "src/SCION/model/beaconing/latency-optimized-beaconing.h"
#include "src/SCION/model/beaconing/scionlab-algo.h"
#include "post-simulation-evaluations.h"
#include "schedule-periodic-events.h"
#include "scion-as.h"
#include "scion-core-as.h"
#include "time-server.h"
#include "utils.h"

namespace ns3 {

void
PostSimulationEvaluations::DoFinalEvaluations ()
{
  if (!config["post_eval"])
    {
      return;
    }

  const YAML::Node &evals = config["post_eval"];
  for (auto it = evals.begin (); it != evals.end (); ++it)
    {
      const YAML::Node &eval = *it;
      std::string func = eval["func"].as<std::string> ();
      ((this)->*function_name_to_function.at (func)) ();
    }
}

void
PostSimulationEvaluations::PrintTrafficSentFromCollectorsPerDstPerPeriod ()
{
  std::cout << "####################################### Traffic sent from each collector "
               "#######################################"
            << std::endl;
  std::list<int32_t> collectors ({3303,  3130,  1239,  701,   5413,  34224, 7018,
                                  53767, 3741,  31019, 22652, 2497,  57866, 37100,
                                  3130,  3257,  3549,  6939,  18106, 1299,  23673,
                                  2914,  11537, 2152,  852,   8492,  34224, 11686});
  for (int32_t collector : collectors)
    {
      double_t consumed_bwd = 0.0;
      for (uint32_t i = 0; i < as_nodes.GetN (); ++i)
        {
          ScionAs *as = dynamic_cast<ScionAs *> (PeekPointer (as_nodes.Get (i)));
          if (alias_to_real_as_no.at (as->as_number) == collector)
            {
              double_t periods = 0.0;
              for (Time t = Time (0); t < last_beaconing_event_time; t += beaconing_period)
                {
                  periods += 1.0;
                  for (uint32_t if_index = 0; if_index < as->GetNDevices (); ++if_index)
                    {
                      consumed_bwd += (double_t) as->GetBeaconServer ()
                                          ->GetBytesSentPerInterfacePerPeriod ()
                                          .at ((uint16_t) t.ToInteger (Time::MIN))
                                          .at (if_index);
                    }
                }
              consumed_bwd = (double_t) consumed_bwd / as->GetNDevices () / periods;
              break;
            }
        }
      std::cout << collector << "\t" << consumed_bwd << std::endl;
    }
}

void
PostSimulationEvaluations::FindMinLatencyToDnsRootServers ()
{
  std::string probes_file = "inputs/atlas_probes.xml";
  std::ifstream fin_probes (probes_file.c_str ());
  std::ostringstream probes_sstr;
  probes_sstr << fin_probes.rdbuf ();
  probes_sstr.flush ();
  fin_probes.close ();

  std::string xml_probes_data = probes_sstr.str ();
  rapidxml::xml_document<> probes_doc;
  probes_doc.parse<0> (&xml_probes_data[0]);

  rapidxml::xml_node<> *probes_root_node = probes_doc.first_node ("root");
  rapidxml::xml_node<> *probes_node = probes_root_node->first_node ("Probes");

  std::list<std::string> root_server_names ({"a-root", "b-root", "c-root", "d-root", "e-root",
                                             "f-root", "h-root", "j-root", "k-root", "l-root",
                                             "m-root"});
  for (auto const &root_server_name : root_server_names)
    {
      std::cout << "################################################## " << root_server_name
                << " #########################################################" << std::endl;

      std::string dns_root_file = "inputs/" + root_server_name + ".xml";
      std::ifstream fin_dns_root (dns_root_file.c_str ());
      std::ostringstream dns_root_sstr;
      dns_root_sstr << fin_dns_root.rdbuf ();
      dns_root_sstr.flush ();
      fin_dns_root.close ();

      std::set<int32_t> set_of_src_ases;

      std::string xml_dns_root_data = dns_root_sstr.str ();
      rapidxml::xml_document<> dns_root_doc;
      dns_root_doc.parse<0> (&xml_dns_root_data[0]);

      rapidxml::xml_node<> *dns_root_node = dns_root_doc.first_node ("root");
      int32_t dst_as_no = std::stoi (dns_root_node->first_node ("ASN")->value ());

      if (real_to_alias_as_no.find (dst_as_no) == real_to_alias_as_no.end ())
        {
          std::cout << root_server_name << ": " << dst_as_no
                    << " The root DNS server's AS is not among the top 2000 real_to_alias_as_no"
                    << std::endl;
          continue;
        }
      std::cout << "Probe|ASN|Latency|Distance|Probe coordinates|Path Coordinates|Instance "
                   "Coordinates|real_to_alias_as_no on Path"
                << std::endl;

      uint16_t dst_alias_as_no = real_to_alias_as_no.at (dst_as_no);
      ScionAs *dst_as = dynamic_cast<ScionAs *> (PeekPointer (as_nodes.Get (dst_alias_as_no)));

      rapidxml::xml_node<> *curr_probe = probes_node->first_node ("item");
      while (curr_probe)
        {
          int32_t src_as_no = std::stoi (curr_probe->first_node ("ASN")->value ());
          double probe_lat = std::stod (curr_probe->first_node ("Latitude")->value ());
          double probe_long = std::stod (curr_probe->first_node ("Longitude")->value ());

          if (real_to_alias_as_no.find (src_as_no) == real_to_alias_as_no.end ())
            {
              curr_probe = curr_probe->next_sibling ("item");
              continue;
            }

          set_of_src_ases.insert (src_as_no);

          ScionAs *src_alias_as_no = dynamic_cast<ScionAs *> (
              PeekPointer (as_nodes.Get (real_to_alias_as_no.at (src_as_no))));

          uint16_t last_br = 0;
          double min_latency_to_dst_as = std::numeric_limits<double>::max ();
          path *selected_path = NULL;

          auto const &beacons_to_dns_root_as =
              src_alias_as_no->GetBeaconServer ()->GetBeaconStore ().at (dst_alias_as_no);

          for (auto const &len_beacons_pair : beacons_to_dns_root_as)
            {
              auto const &same_len_beacons = len_beacons_pair.second;

              for (auto const &the_beacon : same_len_beacons)
                {
                  if (the_beacon->is_valid)
                    {
                      assert (UPPER_16_BITS (the_beacon->the_path.at (0)) == dst_alias_as_no);

                      uint16_t first_br = LOWER_16_BITS (the_beacon->the_path.back ());
                      std::pair<double, double> first_br_coordinates =
                          src_alias_as_no->interfaces_coordinates.at (first_br);
                      double latency_from_probe_to_first_hop = CalculateGreatCircleLatency (
                          probe_lat, probe_long, first_br_coordinates.first,
                          first_br_coordinates.second);
                      if (the_beacon->static_info_extension.at (StaticInfoType::LATENCY) +
                              latency_from_probe_to_first_hop <
                          min_latency_to_dst_as)
                        {
                          min_latency_to_dst_as =
                              the_beacon->static_info_extension.at (StaticInfoType::LATENCY) +
                              latency_from_probe_to_first_hop;
                          last_br = SECOND_UPPER_16_BITS (the_beacon->the_path.at (0));
                          selected_path = &the_beacon->the_path;
                        }
                    }
                }
            }

          double min_overall_latency = std::numeric_limits<double>::max ();
          std::pair<double, double> selected_instance_coordinates;

          rapidxml::xml_node<> *sites_node = dns_root_node->first_node ("Sites");
          rapidxml::xml_node<> *curr_site = sites_node->first_node ("item");
          while (curr_site)
            {
              double instance_lat = std::stod (curr_site->first_node ("Latitude")->value ());
              double instance_long = std::stod (curr_site->first_node ("Longitude")->value ());
              std::pair<double, double> last_br_coordinates =
                  dst_as->interfaces_coordinates.at (last_br);
              double overall_latency =
                  min_latency_to_dst_as + CalculateGreatCircleLatency (instance_lat, instance_long,
                                                                       last_br_coordinates.first,
                                                                       last_br_coordinates.second);
              if (overall_latency < min_overall_latency)
                {
                  min_overall_latency = overall_latency;
                  selected_instance_coordinates =
                      std::pair<double, double> (instance_lat, instance_long);
                }
              curr_site = curr_site->next_sibling ("item");
            }

          std::cout
              << curr_probe->first_node ("ID")->value () << "|" << src_as_no
              << "|"
              // << "(" << selected_instance_coordinates.first << ", " << selected_instance_coordinates.second << ")" << "|"
              << min_overall_latency << "|"
              << CalculateGreatCircleDistance (probe_lat, probe_long,
                                               selected_instance_coordinates.first,
                                               selected_instance_coordinates.second)
              << "|"
              << "(" << probe_lat << ", " << probe_long << ")"
              << "|";

          uint32_t hop_cnt = 0;
          std::vector<link_information>::reverse_iterator hop = selected_path->rbegin ();
          for (; hop != selected_path->rend (); ++hop)
            {
              if (hop_cnt != 0)
                {
                  std::cout << " ";
                }

              ScionAs *as =
                  dynamic_cast<ScionAs *> (PeekPointer (as_nodes.Get (UPPER_16_BITS (*hop))));
              std::pair<double, double> br_coordinates =
                  as->interfaces_coordinates.at (SECOND_UPPER_16_BITS (*hop));
              std::cout << "(" << br_coordinates.first << ", " << br_coordinates.second << ")";
              hop_cnt++;
            }

          std::cout << "|"
                    << "(" << selected_instance_coordinates.first << ", "
                    << selected_instance_coordinates.second << ")"
                    << "|";

          hop_cnt = 0;
          hop = selected_path->rbegin ();
          for (; hop != selected_path->rend (); ++hop)
            {
              if (hop_cnt != 0)
                {
                  std::cout << " ";
                }
              std::cout << alias_to_real_as_no.at (SECOND_LOWER_16_BITS (*hop));
              hop_cnt++;
            }

          std::cout << " " << alias_to_real_as_no.at (UPPER_16_BITS (selected_path->at (0)));

          std::cout << std::endl;

          curr_probe = curr_probe->next_sibling ("item");
        }

      for (auto const &src_as_no : set_of_src_ases)
        {
          ScionAs *src_as = dynamic_cast<ScionAs *> (
              PeekPointer (as_nodes.Get (real_to_alias_as_no.at (src_as_no))));
          auto const &beacons_to_dns_root_as =
              src_as->GetBeaconServer ()->GetBeaconStore ().at (dst_alias_as_no);
          for (auto const &len_beacons_pair : beacons_to_dns_root_as)
            {
              auto const &same_len_beacons = len_beacons_pair.second;
              for (auto const &the_beacon : same_len_beacons)
                {
                  if (!the_beacon->is_valid)
                    {
                      continue;
                    }

                  std::cout << src_as_no << "|" << dst_as_no << "|";
                  path *the_path = &the_beacon->the_path;

                  uint32_t hop_cnt = 0;
                  std::vector<link_information>::reverse_iterator hop = the_path->rbegin ();
                  for (; hop != the_path->rend (); ++hop)
                    {
                      if (hop_cnt != 0)
                        {
                          std::cout << " ";
                        }

                      ScionAs *as = dynamic_cast<ScionAs *> (
                          PeekPointer (as_nodes.Get (UPPER_16_BITS (*hop))));
                      std::pair<double, double> br_coordinates =
                          as->interfaces_coordinates.at (SECOND_UPPER_16_BITS (*hop));
                      std::cout << "(" << br_coordinates.first << ", " << br_coordinates.second
                                << ")";
                      hop_cnt++;
                    }

                  std::cout << "|";

                  hop_cnt = 0;
                  hop = the_path->rbegin ();
                  for (; hop != the_path->rend (); ++hop)
                    {
                      if (hop_cnt != 0)
                        {
                          std::cout << " ";
                        }
                      std::cout << alias_to_real_as_no.at (SECOND_LOWER_16_BITS (*hop));
                      hop_cnt++;
                    }

                  std::cout << " " << alias_to_real_as_no.at (UPPER_16_BITS (the_path->at (0)));

                  std::cout << std::endl;
                }
            }
        }
    }
}

void
PostSimulationEvaluations::PrintAllDiscoveredPaths ()
{
  std::cout << "################################################ Paths Information "
               "##############################################################"
            << std::endl;

  for (uint32_t i = 0; i < as_nodes.GetN (); ++i)
    {
      ScionAs *as = dynamic_cast<ScionAs *> (PeekPointer (as_nodes.Get (i)));

      std::cout << "From: " << alias_to_real_as_no.at (as->as_number) << std::endl;

      as->GetBeaconServer ()->InsertPulledBeaconsToBeaconStore ();

      for (auto const &dst_as_beacons_pair : as->GetBeaconServer ()->GetBeaconStore ())
        {
          uint16_t dst_as = dst_as_beacons_pair.first;
          auto const &same_dst_as_beacons = dst_as_beacons_pair.second;

          std::cout << "\t"
                    << "To: " << alias_to_real_as_no.at (dst_as) << std::endl;

          for (auto const &beacons_from_same_nbr : same_dst_as_beacons)
            {
              for (auto const &the_beacon : beacons_from_same_nbr.second)
                {
                  if (!the_beacon->is_valid)
                    {
                      continue;
                    }
                  std::cout << "\t"
                            << "\t";
                  uint32_t hop_cnt = 0;
                  std::vector<link_information>::reverse_iterator hop =
                      the_beacon->the_path.rbegin ();
                  for (; hop != the_beacon->the_path.rend (); ++hop)
                    {
                      if (hop_cnt != 0)
                        {
                          std::cout << ", ";
                        }
                      std::cout << alias_to_real_as_no.at (SECOND_LOWER_16_BITS (*hop)) << ":"
                                << LOWER_16_BITS (*hop) << ", "
                                << alias_to_real_as_no.at (UPPER_16_BITS (*hop)) << ":"
                                << SECOND_UPPER_16_BITS (*hop);
                      hop_cnt++;
                    }

                  if (the_beacon->beacon_direction == BeaconDirectionT::PULL_BASED)
                    {
                      std::cout << "; pull"
                                << "; initiation_time = " << the_beacon->initiation_time;
                    }
                  else
                    {
                      std::cout << "; push";
                    }

                  std::cout << "; ";

                  for (auto const &metric : the_beacon->static_info_extension)
                    {
                      if (metric.first == LATENCY)
                        {
                          std::cout << "latency = " << metric.second;
                          std::cout << "; ";
                        }
                      else if (metric.first == BW)
                        {
                          std::cout << "BWD = " << metric.second << ";";
                        }
                    }

                  std::cout << std::endl;
                }
            }
        }
    }
}

void
PostSimulationEvaluations::PrintAllPathsAttributes ()
{
  std::cout << "################################################ Paths Information "
               "##############################################################"
            << std::endl;

  for (uint32_t i = 0; i < as_nodes.GetN (); ++i)
    {
      ScionAs *as = dynamic_cast<ScionAs *> (PeekPointer (as_nodes.Get (i)));

      std::cout << "From: " << alias_to_real_as_no.at (as->as_number) << std::endl;

      for (auto const &dst_as_beacons_pair : as->GetBeaconServer ()->GetBeaconStore ())
        {
          uint16_t dst_as = dst_as_beacons_pair.first;
          auto const &same_dst_as_beacons = dst_as_beacons_pair.second;

          std::cout << "\t"
                    << "To: " << alias_to_real_as_no.at (dst_as) << std::endl;

          for (auto const &beacons_from_same_nbr : same_dst_as_beacons)
            {
              for (auto const &the_beacon : beacons_from_same_nbr.second)
                {
                  if (!the_beacon->is_valid)
                    {
                      continue;
                    }
                  std::cout << "\t"
                            << "\t";
                  for (auto const &[att_type, att_value] : the_beacon->static_info_extension)
                    {
                      if (att_type == StaticInfoType::LATENCY)
                        {
                          std::cout << "latency = " << att_value << "; ";
                        }
                      if (att_type == StaticInfoType::BW)
                        {
                          std::cout << "BW = " << att_value << "; ";
                        }
                    }

                  if (the_beacon->optimization_target != NULL)
                    {
                      for (auto const &[criteria_type, criteria_coef] :
                           the_beacon->optimization_target->criteria)
                        {
                          std::cout << "iface_group_id = "
                                    << the_beacon->optimization_target->target_if_group << "; ";
                          if (criteria_type == StaticInfoType::LATENCY)
                            {
                              std::cout << "latency_coef = " << criteria_coef << "; ";
                            }
                          if (criteria_type == StaticInfoType::BW)
                            {
                              std::cout << "BW_coef = " << criteria_coef << "; ";
                            }
                        }
                    }
                  std::cout << std::endl;
                }
            }
        }
    }
}

void
PostSimulationEvaluations::PrintNoBeaconsPerInterfacePerDstOrOpt ()
{
  std::cout
      << "####################################### sent beacons on each interface in each period"
         "#######################################"
      << std::endl;
  for (uint16_t time = 0; time <= last_beaconing_event_time.ToInteger (Time::MIN);
       time += beaconing_period.ToInteger (Time::MIN))
    {
      std::cout << time << "|";
      for (uint32_t i = 0; i < as_nodes.GetN (); ++i)
        {
          ScionAs *as = dynamic_cast<ScionAs *> (PeekPointer (as_nodes.Get (i)));
          auto const &counters_per_period =
              as->GetBeaconServer ()->GetBeaconsSentPerInterfacePerPeriod ().at (time);
          for (uint32_t if_index = 0; if_index < as->GetNDevices (); ++if_index)
            {
              uint64_t no_sent_beacons = counters_per_period.at (if_index);
              std::cout << alias_to_real_as_no.at (as->as_number) << ":" << if_index << "="
                        << no_sent_beacons << ",";
            }
        }
      std::cout << std::endl;
    }

  std::cout << "####################################### cumulative sent beacons on each interface "
               "#######################################"
            << std::endl;

  for (uint32_t i = 0; i < as_nodes.GetN (); ++i)
    {
      ScionAs *as = dynamic_cast<ScionAs *> (PeekPointer (as_nodes.Get (i)));
      for (uint32_t if_index = 0; if_index < as->GetNDevices (); ++if_index)
        {
          uint64_t no_sent_beacons =
              as->GetBeaconServer ()->GetBeaconsSentPerInterface ().at (if_index);
          std::cout << alias_to_real_as_no.at (as->as_number) << ":" << if_index << "="
                    << no_sent_beacons << ",";
        }
    }

  std::cout << std::endl;

  std::cout << "####################################### sent beacons per interface per destination "
               "#######################################"
            << std::endl;

  std::unordered_map<uint16_t, std::vector<uint32_t>> beacons_sent_per_dst_per_interface;

  for (uint32_t i = 0; i < as_nodes.GetN (); ++i)
    {
      ScionAs *as = dynamic_cast<ScionAs *> (PeekPointer (as_nodes.Get (i)));
      auto &counters = as->GetBeaconServer ()->GetBeaconsSentPerDstPerInterfacePerPeriod ();
      for (uint32_t if_index = 0; if_index < as->GetNDevices (); ++if_index)
        {
          auto &counters_per_interface = counters.at (if_index);
          for (auto const &[dst_as, counter] : counters_per_interface)
            {
              if (beacons_sent_per_dst_per_interface.find (dst_as) ==
                  beacons_sent_per_dst_per_interface.end ())
                {
                  beacons_sent_per_dst_per_interface.insert (
                      std::make_pair (dst_as, std::vector<uint32_t> ()));
                }
              beacons_sent_per_dst_per_interface.at (dst_as).push_back (counter);
            }
        }
    }

  for (auto const &[dst_as, counters] : beacons_sent_per_dst_per_interface)
    {
      std::cout << alias_to_real_as_no.at (dst_as) << "|";
      for (auto const counter : counters)
        {
          std::cout << counter << ",";
        }
      std::cout << std::endl;
    }

  std::cout << "####################################### push sent beacons per interface per "
               "optimization target "
               "#######################################"
            << std::endl;

  std::unordered_map<const OptimizationTarget *, std::vector<uint32_t>>
      beacons_sent_per_opt_per_interface;

  for (uint32_t i = 0; i < as_nodes.GetN (); ++i)
    {
      ScionAs *as = dynamic_cast<ScionAs *> (PeekPointer (as_nodes.Get (i)));
      auto &counters =
          as->GetBeaconServer ()->GetPushBasedBeaconsSentPerOptPerInterfacePerPeriod ();

      for (uint32_t if_index = 0; if_index < as->GetNDevices (); ++if_index)
        {
          auto &counters_per_interface = counters.at (if_index);
          for (auto const &[opt_target, counter] : counters_per_interface)
            {
              if (beacons_sent_per_opt_per_interface.find (opt_target) ==
                  beacons_sent_per_opt_per_interface.end ())
                {
                  beacons_sent_per_opt_per_interface.insert (
                      std::make_pair (opt_target, std::vector<uint32_t> ()));
                }
              beacons_sent_per_opt_per_interface.at (opt_target).push_back (counter);
            }
        }
    }

  for (auto const &[opt_target, counters] : beacons_sent_per_opt_per_interface)
    {
      std::cout << int64_t (opt_target) << "|" << alias_to_real_as_no.at (opt_target->target_as)
                << "|" << opt_target->target_id << "|" << opt_target->target_if_group << "|";
      for (auto const counter : counters)
        {
          std::cout << counter << ",";
        }
      std::cout << std::endl;
    }

  std::cout << "####################################### pull sent beacons per interface per "
               "optimization target "
               "#######################################"
            << std::endl;

  beacons_sent_per_opt_per_interface.clear ();

  for (uint32_t i = 0; i < as_nodes.GetN (); ++i)
    {
      ScionAs *as = dynamic_cast<ScionAs *> (PeekPointer (as_nodes.Get (i)));
      auto &counters =
          as->GetBeaconServer ()->GetPullBasedBeaconsSentPerOptPerInterfacePerPeriod ();

      for (uint32_t if_index = 0; if_index < as->GetNDevices (); ++if_index)
        {
          auto &counters_per_interface = counters.at (if_index);
          for (auto const &[opt_target, counter] : counters_per_interface)
            {
              if (beacons_sent_per_opt_per_interface.find (opt_target) ==
                  beacons_sent_per_opt_per_interface.end ())
                {
                  beacons_sent_per_opt_per_interface.insert (
                      std::make_pair (opt_target, std::vector<uint32_t> ()));
                }
              beacons_sent_per_opt_per_interface.at (opt_target).push_back (counter);
            }
        }
    }

  for (auto const &[opt_target, counters] : beacons_sent_per_opt_per_interface)
    {
      std::cout << int64_t (opt_target) << "|" << alias_to_real_as_no.at (opt_target->target_as)
                << "|" << alias_to_real_as_no.at (0xFFFF - opt_target->target_id) << "|";
      for (auto const counter : counters)
        {
          std::cout << counter << ",";
        }
      std::cout << std::endl;
    }
}

void
PostSimulationEvaluations::PrintNoBeaconsPerInterface ()
{
  std::cout << "####################################### cumulative sent beacons on each interface "
               "#######################################"
            << std::endl;
  std::cout << "link"
            << "\t"
            << "sent beacons" << std::endl;
  for (uint32_t i = 0; i < as_nodes.GetN (); ++i)
    {
      ScionAs *as = dynamic_cast<ScionAs *> (PeekPointer (as_nodes.Get (i)));
      for (uint32_t if_index = 0; if_index < as->GetNDevices (); ++if_index)
        {
          uint64_t no_sent_beacons =
              as->GetBeaconServer ()->GetBeaconsSentPerInterface ().at (if_index);
          std::cout << alias_to_real_as_no.at (as->as_number) << ":" << if_index << "\t"
                    << no_sent_beacons << std::endl;
        }
    }

  for (Time t = Seconds (0.0); t < last_beaconing_event_time; t += beaconing_period)
    {
      std::cout << "####################################### frequencies of sent beacons at Time "
                << t << " #######################################" << std::endl;

      std::map<uint32_t, uint32_t> frequencies_of_sent_beacon_numbers;
      for (uint32_t i = 0; i < as_nodes.GetN (); ++i)
        {
          ScionAs *as = dynamic_cast<ScionAs *> (PeekPointer (as_nodes.Get (i)));
          for (uint32_t if_index = 0; if_index < as->GetNDevices (); ++if_index)
            {
              uint32_t no_sent_beacons = as->GetBeaconServer ()
                                             ->GetBeaconsSentPerInterfacePerPeriod ()
                                             .at (t.ToInteger (Time::MIN))
                                             .at (if_index);

              if (frequencies_of_sent_beacon_numbers.find (no_sent_beacons) !=
                  frequencies_of_sent_beacon_numbers.end ())
                {
                  frequencies_of_sent_beacon_numbers.at (no_sent_beacons)++;
                }
              else
                {
                  frequencies_of_sent_beacon_numbers.insert (std::make_pair (no_sent_beacons, 1));
                }
            }
        }

      std::cout << "sent beacons on a link"
                << "\t"
                << "frequency" << std::endl;
      for (auto const &bwd_freq_pair : frequencies_of_sent_beacon_numbers)
        {
          std::cout << bwd_freq_pair.first << "\t" << bwd_freq_pair.second << std::endl;
        }
    }
}

void
PostSimulationEvaluations::PrintConsumedBwAtEachPeriod ()
{
  for (Time t = Seconds (0.0); t < last_beaconing_event_time; t += beaconing_period)
    {
      std::cout
          << "####################################### frequencies of consumed bandwidth at Time "
          << t << " #######################################" << std::endl;

      std::map<uint32_t, uint32_t> frequencies_of_consumed_bwd;
      for (uint32_t i = 0; i < as_nodes.GetN (); ++i)
        {
          ScionAs *as = dynamic_cast<ScionAs *> (PeekPointer (as_nodes.Get (i)));
          for (uint32_t if_index = 0; if_index < as->GetNDevices (); ++if_index)
            {
              uint32_t consumed_bwd = as->GetBeaconServer ()
                                          ->GetBytesSentPerInterfacePerPeriod ()
                                          .at (t.ToInteger (Time::MIN))
                                          .at (if_index);

              if (frequencies_of_consumed_bwd.find (consumed_bwd) !=
                  frequencies_of_consumed_bwd.end ())
                {
                  frequencies_of_consumed_bwd.at (consumed_bwd)++;
                }
              else
                {
                  frequencies_of_consumed_bwd.insert (std::make_pair (consumed_bwd, 1));
                }
            }
        }

      std::cout << "consumed bandwidth on a link"
                << "\t"
                << "frequency" << std::endl;
      for (auto const &bwd_freq_pair : frequencies_of_consumed_bwd)
        {
          std::cout << bwd_freq_pair.first << "\t" << bwd_freq_pair.second << std::endl;
        }
    }
}

void
PostSimulationEvaluations::PrintDistributionOfPathsWithSpecificHopCount ()
{
  for (uint32_t path_length = 1; path_length <= 4; ++path_length)
    {
      std::cout << "######################################### frequencies of path counts per "
                   "destination AS with "
                   "hop count: "
                << path_length << "#########################################" << std::endl;
      std::map<uint64_t, uint64_t> frequencies_of_path_counts_per_dst_as_with_certain_length;
      for (uint32_t i = 0; i < as_nodes.GetN (); ++i)
        {
          for (auto const &dst_as_beacons_pair :
               DynamicCast<ScionAs> (as_nodes.Get (i))->GetBeaconServer ()->GetBeaconStore ())
            {
              uint64_t number_of_paths_with_certain_length = 0;
              if (dst_as_beacons_pair.second.find (path_length) ==
                  dst_as_beacons_pair.second.end ())
                {
                  continue;
                }
              else
                {
                  number_of_paths_with_certain_length =
                      dst_as_beacons_pair.second.at (path_length).size ();
                }

              if (frequencies_of_path_counts_per_dst_as_with_certain_length.find (
                      number_of_paths_with_certain_length) !=
                  frequencies_of_path_counts_per_dst_as_with_certain_length.end ())
                {
                  frequencies_of_path_counts_per_dst_as_with_certain_length.at (
                      number_of_paths_with_certain_length)++;
                }
              else
                {
                  frequencies_of_path_counts_per_dst_as_with_certain_length.insert (
                      std::make_pair (number_of_paths_with_certain_length, 1));
                }
            }
        }

      std::cout << "path count per source AS"
                << "\t"
                << "frequency" << std::endl;
      for (auto const &count_freq_pair : frequencies_of_path_counts_per_dst_as_with_certain_length)
        {
          std::cout << count_freq_pair.first << "\t" << count_freq_pair.second << std::endl;
        }
    }
}

void
PostSimulationEvaluations::PrintMinimumLatencyDist ()
{
  std::cout << "###################################################### MINIMUM "
               "LATENCY####################################"
            << std::endl;

  std::map<float, int> distribution;
  for (uint32_t i = 0; i < as_nodes.GetN (); i++)
    {
      ScionAs *as = dynamic_cast<ScionAs *> (PeekPointer (as_nodes.Get (i)));
      for (uint32_t j = 0; j < as_nodes.GetN (); ++j)
        {
          if (i == j)
            continue;
          float min_latency = std::numeric_limits<float>::max ();
          for (auto const &len_beacons_pair : as->GetBeaconServer ()->GetBeaconStore ().at (j))
            {
              for (auto const &the_beacon : len_beacons_pair.second)
                {
                  assert (the_beacon->the_path.size () != 1 ||
                          the_beacon->static_info_extension.at (StaticInfoType::LATENCY) ==
                              (float) 0);
                  if (the_beacon->static_info_extension.at (StaticInfoType::LATENCY) < min_latency)
                    {
                      min_latency = the_beacon->static_info_extension.at (StaticInfoType::LATENCY);
                    }
                }
            }

          if (distribution.find (min_latency) == distribution.end ())
            {
              distribution.insert (std::make_pair (min_latency, 0));
            }
          distribution.at (min_latency)++;
        }
    }

  int cumulative_counter = 0;
  for (auto const &entry : distribution)
    {
      float latency = entry.first;
      distribution.at (latency) += cumulative_counter;
      cumulative_counter = distribution.at (latency);
    }

  for (auto const &entry : distribution)
    {
      std::cout << entry.first << "\t" << (double) entry.second / cumulative_counter << std::endl;
    }
}

void
PostSimulationEvaluations::PrintPathNoDistribution ()
{
  std::cout << "############################################# Path No Distribution "
               "##################################"
            << std::endl;
  std::map<uint32_t, uint32_t> distribution;
  for (uint32_t i = 0; i < as_nodes.GetN (); ++i)
    {
      ScionAs *node = dynamic_cast<ScionAs *> (PeekPointer (as_nodes.Get (i)));
      for (auto const &dst_count_pair : node->GetBeaconServer ()->GetValidBeaconsCountPerDstAs ())
        {
          if (distribution.find (dst_count_pair.second) == distribution.end ())
            {
              distribution.insert (std::make_pair (dst_count_pair.second, 0));
            }

          distribution.at (dst_count_pair.second) = distribution.at (dst_count_pair.second) + 1;
        }
    }

  uint32_t cumulative_counter = 0;
  for (auto const &path_cnt_cnt_pair : distribution)
    {
      distribution.at (path_cnt_cnt_pair.first) =
          distribution.at (path_cnt_cnt_pair.first) + cumulative_counter;
      cumulative_counter = distribution.at (path_cnt_cnt_pair.first);
    }

  for (auto const &entry : distribution)
    {
      std::cout << entry.first << "\t" << (double) entry.second / cumulative_counter << std::endl;
    }
}

void
PostSimulationEvaluations::PrintPathPollutionIndex ()
{
  int x = 5;
  std::map<double, uint32_t> min_distribution;
  std::map<double, uint32_t> mean_distribution;
  std::map<double, uint32_t> top_x_mean_distribution;

  std::cout
      << "############################################# Latencies of paths; Min pollution; Mean of "
      << x << "-least-polluting; Mean of all ##################################" << std::endl;

  for (uint32_t i = 0; i < as_nodes.GetN (); ++i)
    {
      ScionAs *as = dynamic_cast<ScionAs *> (PeekPointer (as_nodes.Get (i)));

      for (uint32_t j = 0; j < as_nodes.GetN (); ++j)
        {
          if (i == j)
            continue;

          float min = std::numeric_limits<float>::max ();

          float mean = (float) 0;
          int cnt = 0;

          float top_x_mean = (float) 0;
          int top_x_cnt = 0;
          std::multimap<float, float> pollution_indexes;

          float latency_of_path_with_min_pollution = 0;
          float avg_latency_of_x_least_polluting_paths = 0;
          float avg_latency_of_all_paths = 0;

          for (auto const &len_beacons_pair : as->GetBeaconServer ()->GetBeaconStore ().at (j))
            {
              for (auto const &the_beacon : len_beacons_pair.second)
                {
                  pollution_indexes.insert (std::make_pair (
                      the_beacon->static_info_extension.at (StaticInfoType::CO2),
                      the_beacon->static_info_extension.at (StaticInfoType::LATENCY)));

                  cnt++;
                  mean += the_beacon->static_info_extension.at (StaticInfoType::CO2);
                  avg_latency_of_all_paths +=
                      the_beacon->static_info_extension.at (StaticInfoType::LATENCY);

                  if (the_beacon->static_info_extension.at (StaticInfoType::CO2) < min)
                    {
                      min = the_beacon->static_info_extension.at (StaticInfoType::CO2);
                      latency_of_path_with_min_pollution =
                          the_beacon->static_info_extension.at (StaticInfoType::LATENCY);
                    }
                }
            }

          for (auto const &pollution_index_latency_pair : pollution_indexes)
            {
              if (top_x_cnt >= x)
                break;
              top_x_mean += pollution_index_latency_pair.first;
              avg_latency_of_x_least_polluting_paths += pollution_index_latency_pair.second;
              top_x_cnt++;
            }
          pollution_indexes.clear ();

          top_x_mean /= top_x_cnt;
          if (top_x_mean_distribution.find (top_x_mean) == top_x_mean_distribution.end ())
            {
              top_x_mean_distribution.insert (std::make_pair (top_x_mean, 0));
            }

          mean /= cnt;
          if (mean_distribution.find (mean) == mean_distribution.end ())
            {
              mean_distribution.insert (std::make_pair (mean, 0));
            }

          if (min_distribution.find (min) == min_distribution.end ())
            {
              min_distribution.insert (std::make_pair (min, 0));
            }

          top_x_mean_distribution.at (top_x_mean)++;
          mean_distribution.at (mean)++;
          min_distribution.at (min)++;

          avg_latency_of_all_paths /= cnt;
          avg_latency_of_x_least_polluting_paths /= top_x_cnt;

          std::cout << alias_to_real_as_no.at (i) << "\t" << alias_to_real_as_no.at (j) << "\t"
                    << latency_of_path_with_min_pollution << "\t"
                    << avg_latency_of_x_least_polluting_paths << "\t" << avg_latency_of_all_paths
                    << std::endl;
        }
    }

  uint32_t cumulative_counter = 0;
  for (auto const &path_cnt_cnt_pair : min_distribution)
    {
      min_distribution.at (path_cnt_cnt_pair.first) =
          min_distribution.at (path_cnt_cnt_pair.first) + cumulative_counter;
      cumulative_counter = min_distribution.at (path_cnt_cnt_pair.first);
    }

  std::cout << "############################################# MIN Pollution Index Distribution "
               "##################################"
            << std::endl;
  for (auto const &entry : min_distribution)
    {
      std::cout << entry.first << "\t" << (double) entry.second / cumulative_counter << std::endl;
    }

  cumulative_counter = 0;
  for (auto const &path_cnt_cnt_pair : top_x_mean_distribution)
    {
      top_x_mean_distribution.at (path_cnt_cnt_pair.first) =
          top_x_mean_distribution.at (path_cnt_cnt_pair.first) + cumulative_counter;
      cumulative_counter = top_x_mean_distribution.at (path_cnt_cnt_pair.first);
    }

  std::cout << "############################################# TOP" << x
            << " Paths MEAN Pollution Index Distribution ##################################"
            << std::endl;
  for (auto const &entry : top_x_mean_distribution)
    {
      std::cout << entry.first << "\t" << (double) entry.second / cumulative_counter << std::endl;
    }

  cumulative_counter = 0;
  for (auto const &path_cnt_cnt_pair : mean_distribution)
    {
      mean_distribution.at (path_cnt_cnt_pair.first) =
          mean_distribution.at (path_cnt_cnt_pair.first) + cumulative_counter;
      cumulative_counter = mean_distribution.at (path_cnt_cnt_pair.first);
    }

  std::cout << "############################################# MEAN Pollution Index Distribution "
               "##################################"
            << std::endl;
  for (auto const &entry : mean_distribution)
    {
      std::cout << entry.first << "\t" << (double) entry.second / cumulative_counter << std::endl;
    }
}

void
PostSimulationEvaluations::PrintLeastPollutingPaths ()
{
  std::ifstream bgp_paths_file ("inputs/BGP_path_and_pollution.txt");
  std::string line;

  std::map<std::tuple<int, int>, int> bgp_path_no = std::map<std::tuple<int, int>, int> ();
  while (getline (bgp_paths_file, line))
    {
      std::vector<std::string> fields;
      fields = Split (line, '|', fields);

      int from = std::stoi (fields[0]);
      int to = std::stoi (fields[1]);
      int bgp_paths = std::stoi (fields[6]);

      bgp_path_no.insert (std::make_pair (std::make_pair (from, to), bgp_paths));
    }
  bgp_paths_file.close ();

  std::cout.precision (10);
  for (uint32_t i = 0; i < as_nodes.GetN (); ++i)
    {
      ScionAs *as1 = dynamic_cast<ScionAs *> (PeekPointer (as_nodes.Get (i)));

      for (uint32_t j = 0; j < as_nodes.GetN (); ++j)
        {
          if (i == j)
            continue;

          ScionAs *as2 = dynamic_cast<ScionAs *> (PeekPointer (as_nodes.Get (j)));

          if (bgp_path_no.find (std::make_pair (alias_to_real_as_no.at (as1->as_number),
                                                alias_to_real_as_no.at (as2->as_number))) ==
              bgp_path_no.end ())
            {
              continue;
            }

          std::map<double, std::map<double, std::set<Beacon *>>>
              sorted_beacons_by_pollution_by_latency =
                  std::map<double, std::map<double, std::set<Beacon *>>> ();
          SortBeaconsByPollutionByLatency (as_nodes, as1, as2, beaconing_policy_str,
                                           sorted_beacons_by_pollution_by_latency);

          double min_pollution = sorted_beacons_by_pollution_by_latency.begin ()->first;
          double latency_of_min_pollution =
              sorted_beacons_by_pollution_by_latency.begin ()->second.begin ()->first;
          Beacon *min_polluting_beacon =
              *sorted_beacons_by_pollution_by_latency.begin ()->second.begin ()->second.begin ();

          double avg_pollution_top_5 = 0;
          double avg_pollution_top_n = 0;
          double avg_pollution_all = 0;

          double avg_latency_top_5 = 0;
          double avg_latency_top_n = 0;
          double avg_latency_all = 0;

          int counter = 0;
          int n = bgp_path_no.at (std::make_pair (alias_to_real_as_no.at (as1->as_number),
                                                  alias_to_real_as_no.at (as2->as_number)));

          for (auto const &[pollution, latency_2_set_of_beacons_map] :
               sorted_beacons_by_pollution_by_latency)
            {
              for (auto const &[latency, set_of_beacons] : latency_2_set_of_beacons_map)
                {
                  for (uint32_t k = 0; k < set_of_beacons.size (); ++k)
                    {
                      avg_pollution_all += pollution;
                      avg_latency_all += latency;

                      if (counter < 5)
                        {
                          avg_pollution_top_5 += pollution;
                          avg_latency_top_5 += latency;
                        }

                      if (counter < n)
                        {
                          avg_pollution_top_n += pollution;
                          avg_latency_top_n += latency;
                        }

                      counter += 1;
                    }
                }
            }

          avg_pollution_all /= counter;
          avg_latency_all /= counter;

          if (counter < 5)
            {
              avg_pollution_top_5 /= counter;
              avg_latency_top_5 /= counter;
            }
          else
            {
              avg_pollution_top_5 /= 5;
              avg_latency_top_5 /= 5;
            }

          if (counter < n)
            {
              avg_pollution_top_n /= counter;
              avg_latency_top_n /= counter;
            }
          else
            {
              avg_pollution_top_n /= n;
              avg_latency_top_n /= n;
            }

          std::cout << alias_to_real_as_no.at (as1->as_number) << "|"
                    << alias_to_real_as_no.at (as2->as_number) << "|" << min_pollution << "|"
                    << avg_pollution_top_5 << "|" << avg_pollution_top_n << "|" << avg_pollution_all
                    << "|" << latency_of_min_pollution << "|" << avg_latency_top_5 << "|"
                    << avg_latency_top_n << "|" << avg_latency_all << "|" << counter << "|";

          int hop_cnt = 0;
          std::vector<link_information>::reverse_iterator hop =
              min_polluting_beacon->the_path.rbegin ();
          for (; hop != min_polluting_beacon->the_path.rend (); ++hop)
            {
              if (hop_cnt != 0)
                {
                  std::cout << ", ";
                }
              std::cout << alias_to_real_as_no.at (SECOND_LOWER_16_BITS (*hop)) << ":"
                        << LOWER_16_BITS (*hop) << ", "
                        << alias_to_real_as_no.at (UPPER_16_BITS (*hop)) << ":"
                        << SECOND_UPPER_16_BITS (*hop);
              hop_cnt++;
            }

          std::cout << "|";

          hop_cnt = 0;
          hop = min_polluting_beacon->the_path.rbegin ();
          for (; hop != min_polluting_beacon->the_path.rend (); ++hop)
            {
              if (hop_cnt != 0)
                {
                  std::cout << ", ";
                }
              ScionAs *hop_as = dynamic_cast<ScionAs *> (
                  PeekPointer (as_nodes.Get (SECOND_LOWER_16_BITS (*hop))));
              std::cout << "(" << hop_as->interfaces_coordinates.at (LOWER_16_BITS (*hop)).first
                        << "," << hop_as->interfaces_coordinates.at (LOWER_16_BITS (*hop)).second
                        << ")";
              hop_cnt++;
            }

          std::cout << std::endl;
        }
    }
}

void
PostSimulationEvaluations::PrintBestPerHopPollutionIndexes ()
{
  std::cout
      << "############################################### BestPerHopLatencyAndPollutionIndexes "
         "########################################################################"
      << std::endl;
  for (uint32_t i = 0; i < as_nodes.GetN (); ++i)
    {
      ScionAs *as = dynamic_cast<ScionAs *> (PeekPointer (as_nodes.Get (i)));
      for (auto const &the_ifaces_list1 : as->interfaces_per_neighbor_as)
        {
          uint16_t the_neighbor1 = the_ifaces_list1.first;
          for (auto const &the_ifaces_list2 : as->interfaces_per_neighbor_as)
            {
              uint16_t the_neighbor2 = the_ifaces_list2.first;
              if (the_neighbor1 == the_neighbor2)
                continue;
              double min_latency = std::numeric_limits<double>::max ();
              for (uint16_t iface1 : the_ifaces_list1.second)
                {
                  for (uint16_t iface2 : the_ifaces_list2.second)
                    {
                      if (as->latencies_between_interfaces.at (iface1).at (iface2) < min_latency)
                        {
                          min_latency = as->latencies_between_interfaces.at (iface1).at (iface2);
                        }
                    }
                }
              std::cout << alias_to_real_as_no.at (the_neighbor1) << " "
                        << alias_to_real_as_no.at (as->as_number) << " "
                        << alias_to_real_as_no.at (the_neighbor2) << "\t"
                        << min_latency *
                               ((GreenBeaconing *) as->GetBeaconServer ())->GetDirtyEnergyRatio ()
                        << "\t" << min_latency << "\t" << std::endl;
            }
        }
    }
}

void
PostSimulationEvaluations::PrintConsumedBwForBeaconing ()
{
  for (uint32_t i = 0; i < as_nodes.GetN (); ++i)
    {
      ScionAs *as_node = dynamic_cast<ScionAs *> (PeekPointer (as_nodes.Get (i)));
      for (auto const &el : as_node->GetBeaconServer ()->GetBytesSentPerInterfacePerPeriod ())
        {
          auto const &vector = el.second;
          std::cerr << "\nNode: " << real_to_alias_as_no.at (as_node->as_number) << " at time 0."
                    << std::endl;
          for (auto const &element : vector)
            {
              std::cerr << element << " ";
            }
        }
    }
}

void
PostSimulationEvaluations::PrintBeaconStores ()
{
  for (uint32_t i = 0; i < as_nodes.GetN (); ++i)
    {
      ScionAs *as_node = dynamic_cast<ScionAs *> (PeekPointer (as_nodes.Get (i)));
      std::cout << "From: " << real_to_alias_as_no.at (as_node->as_number) << std::endl;

      for (auto const &dst_as_beacons_pair : as_node->GetBeaconServer ()->GetBeaconStore ())
        {
          uint16_t dst_as = dst_as_beacons_pair.first;
          auto const &same_dst_as_beacons = dst_as_beacons_pair.second;

          std::cout << "\t"
                    << "To: " << real_to_alias_as_no.at (dst_as) << std::endl;

          for (auto const &beacons_from_same_nbr : same_dst_as_beacons)
            {
              for (auto const &the_beacon : beacons_from_same_nbr.second)
                {
                  if (!the_beacon->is_valid)
                    {
                      continue;
                    }
                  std::cout << "\t"
                            << "\t";
                  uint32_t hop_cnt = 0;
                  std::vector<link_information>::reverse_iterator hop =
                      the_beacon->the_path.rbegin ();
                  for (; hop != the_beacon->the_path.rend (); ++hop)
                    {
                      if (hop_cnt != 0)
                        {
                          std::cout << ", ";
                        }
                      std::cout << real_to_alias_as_no.at (SECOND_LOWER_16_BITS (*hop)) << ":"
                                << LOWER_16_BITS (*hop) << ", "
                                << real_to_alias_as_no.at (UPPER_16_BITS (*hop)) << ":"
                                << SECOND_UPPER_16_BITS (*hop);
                      hop_cnt++;
                    }
                  std::cout << "; ";
                  std::cout << "latency = "
                            << the_beacon->static_info_extension.at (StaticInfoType::LATENCY);
                  std::cout << "; ";
                  std::cout << "BWD = "
                            << the_beacon->static_info_extension.at (StaticInfoType::BW);
                  std::cout << std::endl;
                }
            }
        }
    }
}

void
PostSimulationEvaluations::PrintNumberOfValidBeaconEntriesInBeaconStore ()
{
  for (uint32_t i = 0; i < as_nodes.GetN (); ++i)
    {
      ScionAs *as_node = dynamic_cast<ScionAs *> (PeekPointer (as_nodes.Get (i)));
      std::cerr << "Beacon Store on Node: " << real_to_alias_as_no.at (as_node->as_number)
                << std::endl;
      for (auto const &src_as_beacons_pair : as_node->GetBeaconServer ()->GetBeaconStore ())
        {
          auto const &src_as = src_as_beacons_pair.first;
          auto const &beacons = src_as_beacons_pair.second;

          int count = 0;
          for (auto const &len_beacon_set_pair : beacons)
            {
              auto const &length = len_beacon_set_pair.first;
              auto const &beacon_set = len_beacon_set_pair.second;
              std::cout << length;
              for (auto b : beacon_set)
                {
                  if (b->is_valid)
                    {
                      count++;
                    }
                }
            }
          std::cerr << "\t" << real_to_alias_as_no.at (src_as) << ":" << count << std::endl;
        }
      std::cerr << std::endl;
    }
}

void
PostSimulationEvaluations::PrintHostApps ()
{
  std::cout << "########################### Host Apps evaluation "
               "#####################################"
            << std::endl;
  for (uint32_t i = 0; i < as_nodes.GetN (); ++i)
    {
      ScionAs *node = dynamic_cast<ScionAs *> (PeekPointer (as_nodes.Get (i)));
      for (uint32_t j = 0; j < node->GetNHosts (); ++j)
        {
          ScionHost *host = dynamic_cast<ScionHost *> (node->GetHost (j + 2));
          if (host != NULL)
            {
              host->PrintAppsEval ();
            }
        }
    }
  std::cout << "End Apps evaluation" << std::endl;
}

void
PostSimulationEvaluations::PrintLinkStatistics ()
{
  std::cout << "########################### Link Statistics"
               "#####################################"
            << std::endl;

  for (auto it = BackgroundTrafficApp::backgroundTrafficApps.begin ();
       it != BackgroundTrafficApp::backgroundTrafficApps.end (); ++it)
    {
      auto br = it->first.first;
      auto app = it->second;
      std::cout << "BR " << br->GetAddressAsString () << " " << app->GetIfInfoAsString ()
                << " --------------" << std::endl;
      std::cout << "Time, Throughput, Loss" << std::endl;
      br->PrintLinkInfo (it->first.second);
      std::cout << "End results of Link." << std::endl;
    }
  std::cout << "End link statistics" << std::endl;
}

void
PostSimulationEvaluations::PrintAppPathInfo ()
{
  std::cout << "########################### Path info per app"
               "#####################################"
            << std::endl;
  for (uint32_t i = 0; i < as_nodes.GetN (); ++i)
    {
      ScionAs *node = dynamic_cast<ScionAs *> (PeekPointer (as_nodes.Get (i)));
      for (uint32_t j = 0; j < node->GetNHosts (); ++j)
        {
          ScionHost *host = dynamic_cast<ScionHost *> (node->GetHost (j + 2));
          if (host != NULL)
            {
              for (const auto &[app_id, app] : host->apps)
                {
                  app->PrintPathInfo ();
                }
            }
        }
    }
  std::cout << "End path info" << std::endl;
}

void
PostSimulationEvaluations::InvestigateAffectedTimeServers ()
{
  std::cout << "########################### InvestigateAffectedTimeServers "
               "#####################################"
            << std::endl;
  std::vector<std::pair<std::string, uint32_t>> path_selections = {
      std::make_pair ("random", 1), std::make_pair ("short", 1), std::make_pair ("random", 5),
      std::make_pair ("short", 5), std::make_pair ("disjoint", 5)};

  std::vector<std::map<uint32_t, std::unordered_set<ia_t>>>
      pre_calculated_inherently_malicious_ases;
  std::unordered_set<ia_t> inherently_malicious_ases;
  std::set<ia_t> benign_ases;
  uint32_t num_all_ases = as_nodes.GetN ();

  for (uint32_t i = 0; i < num_all_ases; ++i)
    {
      ia_t ia_addr = dynamic_cast<ScionAs *> (PeekPointer (as_nodes.Get (i)))->ia_addr;
      benign_ases.insert (ia_addr);
    }

  uint32_t malicious_incremental_step = std::ceil (num_all_ases / 100);

  for (uint32_t rpt = 0; rpt < 20; ++rpt)
    {
      std::map<uint32_t, std::unordered_set<ia_t>> num_inherent_malicious_to_malicious;
      pre_calculated_inherently_malicious_ases.push_back (num_inherent_malicious_to_malicious);

      benign_ases.insert (inherently_malicious_ases.begin (), inherently_malicious_ases.end ());
      inherently_malicious_ases.clear ();

      for (uint32_t num_inherent_malicious = malicious_incremental_step;
           num_inherent_malicious <= std::ceil (num_all_ases / 3) + malicious_incremental_step;
           num_inherent_malicious += malicious_incremental_step)
        {
          std::set<ia_t> new_inherently_malicious;
          std::set<ia_t> new_benign;

          std::sample (benign_ases.begin (), benign_ases.end (),
                       std::inserter (new_inherently_malicious, new_inherently_malicious.begin ()),
                       malicious_incremental_step, std::random_device{});

          std::set_difference (std::make_move_iterator (benign_ases.begin ()),
                               std::make_move_iterator (benign_ases.end ()),
                               new_inherently_malicious.begin (), new_inherently_malicious.end (),
                               std::inserter (new_benign, new_benign.end ()));

          benign_ases.swap (new_benign);
          inherently_malicious_ases.insert (new_inherently_malicious.begin (),
                                            new_inherently_malicious.end ());

          pre_calculated_inherently_malicious_ases.at (rpt).insert (
              std::make_pair (num_inherent_malicious, inherently_malicious_ases));
        }
    }

  for (auto const &path_selection : path_selections)
    {
      std::cout << "******************************** " << path_selection.second << " "
                << path_selection.first << " ********************************" << std::endl;

#pragma omp parallel for
      for (uint32_t i = 0; i < num_all_ases; ++i)
        {
          ScionAs *scion_as = dynamic_cast<ScionAs *> (PeekPointer (as_nodes.Get (i)));
          TimeServer *time_server = dynamic_cast<TimeServer *> (scion_as->GetHost (2));

          time_server->path_selection = path_selection.first;
          time_server->number_of_paths_to_use_for_global_sync = path_selection.second;

          time_server->ConstructSetOfSelectedPaths ();
        }

      for (uint32_t rpt = 0; rpt < 20; ++rpt)
        {
          for (auto const &num_inherent_malicious_to_malicious :
               pre_calculated_inherently_malicious_ases.at (rpt))
            {
              std::unordered_set<ia_t> inherently_and_transitive_malicious =
                  num_inherent_malicious_to_malicious.second;
              uint32_t num_inherent_malicious = num_inherent_malicious_to_malicious.first;
              bool c = false;
              while (true)
                {
                  c = false;
#pragma omp parallel for
                  for (uint32_t i = 0; i < num_all_ases; ++i)
                    {
                      ScionAs *scion_as = dynamic_cast<ScionAs *> (PeekPointer (as_nodes.Get (i)));

                      if (inherently_and_transitive_malicious.find (scion_as->ia_addr) !=
                          inherently_and_transitive_malicious.end ())
                        {
                          continue;
                        }

                      uint32_t number_of_affected_dst = 0;
                      TimeServer *time_server = dynamic_cast<TimeServer *> (scion_as->GetHost (2));

                      assert (time_server->set_of_selected_paths.size () >= as_nodes.GetN () - 1);

                      for (auto const &[dst_ia, selected_paths_to_dst] :
                           time_server->set_of_selected_paths)
                        {
                          assert (selected_paths_to_dst.size () != 0);

                          uint32_t number_of_affected_paths = 0;
                          for (auto const &path_seg : selected_paths_to_dst)
                            {
                              uint32_t path_len = path_seg->hops.size ();

                              assert (GET_HOP_IA (path_seg->hops.at (0)) == scion_as->ia_addr);
                              assert (GET_HOP_IA (path_seg->hops.at (path_len - 1)) == dst_ia);
                              assert (path_len >= 2);
                              bool path_affected = false;

                              for (uint32_t j = 1; j < path_len; ++j)
                                {
                                  uint64_t hop = path_seg->hops.at (j);
                                  ia_t hop_ia = GET_HOP_IA (hop);
                                  if (inherently_and_transitive_malicious.find (hop_ia) !=
                                      inherently_and_transitive_malicious.end ())
                                    {
                                      path_affected = true;
                                      break;
                                    }
                                }
                              assert (inherently_and_transitive_malicious.find (dst_ia) ==
                                          inherently_and_transitive_malicious.end () ||
                                      path_affected);
                              if (path_affected)
                                {
                                  number_of_affected_paths++;
                                }
                            }
                          assert (inherently_and_transitive_malicious.find (dst_ia) ==
                                      inherently_and_transitive_malicious.end () ||
                                  number_of_affected_paths == selected_paths_to_dst.size ());
                          if (number_of_affected_paths * 2 >= selected_paths_to_dst.size ())
                            {
                              number_of_affected_dst++;
                            }
                        }

                      assert (number_of_affected_dst >= num_inherent_malicious);

                      if (3 * number_of_affected_dst + 1 > num_all_ases)
                        {
                          time_server->affected_by_malicious_ases = true;
                        }
                    }

                  for (uint32_t i = 0; i < num_all_ases; ++i)
                    {
                      ScionAs *scion_as = dynamic_cast<ScionAs *> (PeekPointer (as_nodes.Get (i)));

                      if (inherently_and_transitive_malicious.find (scion_as->ia_addr) !=
                          inherently_and_transitive_malicious.end ())
                        {
                          continue;
                        }

                      TimeServer *time_server = dynamic_cast<TimeServer *> (scion_as->GetHost (2));

                      if (time_server->affected_by_malicious_ases)
                        {
                          inherently_and_transitive_malicious.insert (scion_as->ia_addr);
                          time_server->affected_by_malicious_ases = false;
                          c = true;
                        }
                    }
                  if (!c)
                    {
                      break;
                    }
                }
              std::cout << num_inherent_malicious << "\t"
                        << inherently_and_transitive_malicious.size () << std::endl;
            }
        }
    }
}

void
SortBeaconsByPollutionByLatency (
    NodeContainer &as_nodes, ScionAs *as1, ScionAs *as2, std::string beaconing_policy_str,
    std::map<double, std::map<double, std::set<Beacon *>>> &sorted_beacons_by_pollution_by_latency)
{
  if (beaconing_policy_str == "green_beaconing")
    {
      const std::vector<std::multimap<ld, Beacon *>> &sorted_beacons_per_ing_if =
          ((GreenBeaconing *) as1->GetBeaconServer ())
              ->GetBeaconsSortedByPollution ()
              .at (as2->as_number);

      for (auto const &sorted_beacons : sorted_beacons_per_ing_if)
        {
          for (auto const &[pollution, beacon] : sorted_beacons)
            {
              sorted_beacons_by_pollution_by_latency.insert (
                  std::make_pair (pollution, std::map<double, std::set<Beacon *>> ()));

              double latency = beacon->static_info_extension.at (StaticInfoType::LATENCY);
              if (sorted_beacons_by_pollution_by_latency.at (pollution).find (latency) ==
                  sorted_beacons_by_pollution_by_latency.at (pollution).end ())
                {
                  sorted_beacons_by_pollution_by_latency.at (pollution).insert (
                      std::make_pair (latency, std::set<Beacon *> ()));
                }
              sorted_beacons_by_pollution_by_latency.at (pollution).at (latency).insert (beacon);
            }
        }
    } /*else if (beaconing_policy_str == "baseline") {
            auto const & beacons_from_AS1_to_AS2 = AS1->GetBeaconServer()->beacon_store.at(AS2->as_number);
            for (auto const & [length, beacons] : beacons_from_AS1_to_AS2) {
                for (auto const & beacon : beacons) {
                    double pollution = 0;
                    bool first_hop = true;
                    uint64_t previous_hop = 0;
                    for (uint64_t hop : beacon->the_path) {
                        if (first_hop) {
                            previous_hop = hop;
                            first_hop = false;
                            continue;
                        }

                        uint16_t ingress_if = LOWER_16_BITS(previous_hop);
                        uint16_t egress_if = SECOND_UPPER_16_BITS(hop);
                        assert(SECOND_LOWER_16_BITS(previous_hop) == UPPER_16_BITS(hop));
                        uint16_t as_hop_nr = UPPER_16_BITS(hop);
                        SCION_AS* as_hop = dynamic_cast<SCION_AS*>(PeekPointer(AS_nodes.Get(as_hop_nr)));


                        pollution += (double) (as_hop->intra_as_energies.at(ingress_if).at(egress_if) * as_hop->dirty_energy_ratio * 700 / 3.6e6);

                        previous_hop = hop;
                    }
                    double latency = (double) beacon->static_info_extension.at(static_info_type_t::LATENCY);

                    if (sorted_beacons_by_pollution_by_latency.find(pollution) == sorted_beacons_by_pollution_by_latency.end()) {
                        sorted_beacons_by_pollution_by_latency.insert(std::make_pair(pollution, std::map<double, std::set<Beacon*>>()));
                    }

                    if (sorted_beacons_by_pollution_by_latency.at(pollution).find(latency) == sorted_beacons_by_pollution_by_latency.at(pollution).end()) {
                        sorted_beacons_by_pollution_by_latency.at(pollution).insert(std::make_pair(latency, std::set<Beacon*> ()));
                    }

                    sorted_beacons_by_pollution_by_latency.at(pollution).at(latency).insert(beacon);
                }
            }
        }*/
}

} // namespace ns3
