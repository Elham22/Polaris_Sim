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

#ifndef SCION_SIMULATOR_USER_DEFINED_EVENTS_H
#define SCION_SIMULATOR_USER_DEFINED_EVENTS_H

#include <any>
#include <fstream>
#include <functional>
#include <unordered_map>
#include <yaml-cpp/yaml.h>

#include "src/core/model/simulator.h"

#include "json.hpp"
#include "path-segment.h"
#include "scion-as.h"
#include "scion-packet.h"

template <typename Ret>
struct AnyCallable
{
  AnyCallable ()
  {
  }

  template <typename... Args>
  AnyCallable (std::function<Ret (Args...)> fun) : m_any (fun)
  {
  }

  template <typename... Args>
  Ret
  operator() (Args... args)
  {
    return std::invoke (std::any_cast<std::function<Ret (Args...)>> (m_any),
                        std::forward<Args> (args)...);
  }

  template <std::size_t... S, typename T>
  Ret
  operator() (const std::vector<T> &vec, std::index_sequence<S...>)
  {
    return operator() (vec[S]...);
  }

  template <std::size_t size, typename T>
  Ret
  operator() (const std::vector<T> &vec)
  {
    return operator() (vec, std::make_index_sequence<size> ());
  }

  std::any m_any;
};

template <int N>
struct my_placeholder
{
  static my_placeholder ph;
};

template <int N>
my_placeholder<N> my_placeholder<N>::ph;

namespace std {
template <int N>
struct is_placeholder<::my_placeholder<N>> : std::integral_constant<int, N>
{
};
} // namespace std

template <class R, class... Types, class U, int... indices>
std::function<R (Types...)>
BindFactory (R (U::*f) (Types...), U *val, std::integer_sequence<int, indices...> /*seq*/)
{
  return std::bind (f, val, my_placeholder<indices + 1>::ph...);
}

template <class R, class... Types, class U>
std::function<R (Types...)>
FunctionFactory (R (U::*f) (Types...), U *val)
{
  return BindFactory (f, val, std::make_integer_sequence<int, sizeof...(Types)> ());
}

namespace ns3 {

class UserDefinedEvents
{
public:
  UserDefinedEvents (YAML::Node &config, NodeContainer &as_nodes,
                     std::map<int32_t, uint16_t> &real_to_alias_as_no,
                     std::map<uint16_t, int32_t> &alias_to_real_as_no)
      : config (config),
        as_nodes (as_nodes),
        real_to_alias_as_no (real_to_alias_as_no),
        alias_to_real_as_no (alias_to_real_as_no)
  {
    if (!config["events_file"])
      {
        this->~UserDefinedEvents ();
        return;
      }

    ConstructFuncMap ();
    ReadAndScheduleUserDefinedEvents (config["events_file"].as<std::string> ());
  }

private:
  YAML::Node &config;
  NodeContainer &as_nodes;
  std::map<int32_t, uint16_t> &real_to_alias_as_no;
  std::map<uint16_t, int32_t> &alias_to_real_as_no;

  std::unordered_map<std::string, AnyCallable<void>> function_name_to_function;

  void ConstructFuncMap ();

  void ReadAndScheduleUserDefinedEvents (const std::string &events_file_str);

  void RunUserSpecifiedEvent (const std::string &func_name, std::vector<std::string> vec);

  void AddAHost (std::string isd_number, std::string real_as_no, std::string local_address);

  void LinkDown (std::string isd_number, std::string real_as_no, std::string if_id);
  void LinkUp (std::string isd_number, std::string real_as_no, std::string if_id);

  void SendAPacket (std::string src_isd_number, std::string real_src_as_no,
                    std::string src_local_address, std::string dst_isd_number,
                    std::string real_dst_as_no, std::string dst_local_address,
                    std::string pyload_size);

  void SendPacketBatch (std::string src_isd_number, std::string real_src_as_no,
                        std::string src_local_address, std::string dst_isd_number,
                        std::string real_dst_as_no, std::string dst_local_address,
                        std::string pyload_size, std::string no_pkts);

  void StartApp (std::string src_isd_number, std::string real_src_as_no,
                 std::string src_local_address, std::string dst_isd_number,
                 std::string real_dst_as_no, std::string dst_local_address, std::string app_type,
                 std::string backgroundBwdFactor);

  void SetLinkTraffic (std::string src_isd_number, std::string src_as_no, std::string ing_if,
                       std::string eg_if, std::string bwdFactor);

  void StopAppTraffic (std::string src_isd_number, std::string real_src_as_no,
                       std::string src_local_address, std::string app_id);

  void TimeReferencesDown ();
  void TimeReferencesUp ();
};
} // namespace ns3
#endif //SCION_SIMULATOR_USER_DEFINED_EVENTS_H
