#!/usr/bin/env python3

import enum
import json
import random
import argparse
import os

from config import DEFAULT_FLOW_NUMBERS, STARTUP_PHASE_SECONDS
import bgp_load_balancing


TOPOLOGY_FILE = "configs/traffic-engineering/toy-topology.xml"


class ScenarioType(enum.Enum):
    Ciao = 0
    Naive = 1
    BGP_FD = 2
    BGP_ECMP = 3


def flow_generate(flow_num, time_span, node_num, seed, hybrid, diverse):
    flow_list = []
    half_n = flow_num // 2
    q_n = flow_num * 3 // 4
    for fl in range(flow_num):
        seed += str(fl)
        random.seed(seed)
        st = random.randint(0, time_span-1)
        seed += 'a'
        random.seed(seed)
        du = random.randint(min(2, time_span-st), time_span-st)

        if not hybrid or fl < half_n:
            flow_type = 0  # Ciao
        elif fl < q_n:
            flow_type = 1  # GCC
        else:
            flow_type = 2
        if diverse or (hybrid and flow_type):
            seed += 'b'
            random.seed(seed)
            source = random.randint(0, node_num - 1)
            seed += 'b'
            random.seed(seed)
            dest = random.randint(0, node_num - 1)
            tr = 0
            while dest == source:
                seed = seed + str(tr)
                random.seed(seed)
                dest = random.randint(0, node_num - 1)
                tr += 1
        else:
            source = 0
            dest = 2

        app_limit = 1.2  # round(random.uniform(0, 2), 1)
        flow_list.append((fl, source, dest, app_limit, du, st, flow_type))
    return flow_list


def save_scenario(scenario, output_path):
    with open(output_path, 'w') as f:
        json.dump(scenario, f, indent=4)

    print(f"Created scenario: {output_path}")


def create_scenario(flow: list, startup_phase_end_seconds: int, settings: dict,
                    scen_type: ScenarioType, output_path: str) -> dict:
    applications = []
    events = []

    # Add a random offset to the startup time of flows within the same timeslot
    timeslot_offsets = {}

    timeslot_size_seconds = settings["slot_size"]

    for f in flow:
        flow_id, src_as, dst_as, app_limit, duration, start_timeslot, flow_type = f

        if flow_type == 0:
            traffic_type = "target"
            app_type = "rtc"
        else:
            traffic_type = "background"
            app_type = "rtc" if flow_type == 1 else "TcpCubic"

        if scen_type == ScenarioType.Ciao and traffic_type == "target":
            path_switching = True
        else:
            path_switching = False

        # UserDefinedEvents::StartApp (std::string src_isd_number, std::string real_src_as_no,
        #                      std::string src_local_address, std::string dst_isd_number,
        #                      std::string real_dst_as_no, std::string dst_local_address,
        #                      std::string app_type, std::string backgroundBwdFactor,
        #                      std::string app_id_str, std::string runtime_config)

        event_args = ["0", str(src_as), "2", "0", str(dst_as),
                      "2", app_type, "0.0", str(flow_id), "0"]

        start_time_seconds = startup_phase_end_seconds + start_timeslot * timeslot_size_seconds

        offset = timeslot_offsets.get(start_timeslot, 0)
        timeslot_offsets[start_timeslot] = offset + random.uniform(timeslot_size_seconds / 100, timeslot_size_seconds / 20)

        start_time_offset_seconds = start_time_seconds + offset

        start_event = {
            "time": f"{start_time_offset_seconds:.2f}s",
            "type": "start_application",
            "args": event_args
        }

        events.append(start_event)

        end_time_seconds = start_time_seconds + duration * timeslot_size_seconds

        # Stop a bit before the end of the timeslot
        end_time_offset_seconds = end_time_seconds - timeslot_size_seconds / 20

        # UserDefinedEvents::StopAppTraffic (std::string src_isd_number, std::string real_src_as_no,
        #                           std::string src_local_address, std::string app_id)

        event_args = ["0", str(src_as), "2", str(flow_id)]

        end_event = {
            "time": f"{end_time_offset_seconds:.2f}s",
            "type": "stop_app_traffic",
            "args": event_args
        }
        events.append(end_event)

        # Hosts are fixed for now
        src_host = 2
        dst_host = 2

        application = {
            "app_id": flow_id,
            "src_as": src_as,
            "src_host": src_host,
            "dst_as": dst_as,
            "dst_host": dst_host,
            "app_type": app_type,
            "start_slot": start_timeslot,
            "start_time": start_time_offset_seconds,
            "end_slot": start_timeslot + duration,
            "end_time": end_time_offset_seconds,
            "path_switching": path_switching,
            "traffic_type": traffic_type,
        }

        # Add BGP path to flow
        # NOTE: All background flows are getting BGP ECMP paths no matter the scenario type
        # NOTE: Iteration is currently not used for BGP path finding
        if scen_type == ScenarioType.BGP_ECMP or traffic_type == "background":
            application["path"] = bgp_load_balancing.get_path_as_dict(
                TOPOLOGY_FILE, src_as, src_host, dst_as, dst_host, flow_id, it=0, use_ecmp=True)
        elif scen_type == ScenarioType.BGP_FD:
            application["path"] = bgp_load_balancing.get_path_as_dict(
                TOPOLOGY_FILE, src_as, src_host, dst_as, dst_host, flow_id, it=0, use_ecmp=False)

        if args.path_change_margin:
            application["path_change_margin"] = args.path_change_margin

        applications.append(application)

    settings["description"] = scen_type.name

    scenario = {
        "applications": applications,
        "events": events,
        "settings": settings
    }

    output_path_postfix = ""

    if "path_change_margin" in settings:
        scenario["settings"]["description"] += f" (margin {args.path_change_margin:.1f})"
        output_path_postfix = f"-m{args.path_change_margin:.1f}"

    output_path = output_path.replace(".json", f"-f{settings['num_flows']:02}-i{settings['iteration']:02}-{scen_type.name}{output_path_postfix}.json")
    save_scenario(scenario, output_path)

    return scenario


def add_bgp_lb_paths_to_flows(scenario: dict, iteration: int, use_ecmp: bool) -> None:
    for app in scenario["applications"]:
        p = bgp_load_balancing.get_path(
            TOPOLOGY_FILE, app["src_as"], app["src_host"], app["dst_as"], app["dst_host"], app["app_id"], iteration, use_ecmp)
        p = bgp_load_balancing.transform_path_dict(p)
        app["path"] = p

    print(f"Generated BGP load balanced paths for all flows")


def main():
    global args

    parser = argparse.ArgumentParser(
        description='Generate events for a JSON file.')
    parser.add_argument('-n', '--number-of-flows', type=int, default=4,
                        help='number of flows to generate (default: 4)')
    parser.add_argument('-t', '--time-slots', type=int, default=5,
                        help='number of time slots (default: 5)')
    parser.add_argument('-s', '--slot-size', type=int, default=180,
                        help='size of a time slot in seconds (default: 180)')
    parser.add_argument('-i', '--iterations', type=int, default=3)
    parser.add_argument('--path-change-margin', type=float, default=0.0,
                        help='Path switch margin override for Ciao')
    parser.add_argument('-o', '--output', default='configs/traffic-engineering/toy.json',
                        help='output path')
    parser.add_argument('--bgp-fd', action='store_true',
                        help='use BGP load balancing with full diversity')
    parser.add_argument('--bgp-ecmp', action='store_true',
                        help='use BGP load balancing with ECMP')
    parser.add_argument('--naive', action='store_true',
                        help='use naive routing (random path)')
    parser.add_argument('--ciao', action='store_true',
                        help='use Ciao (dynamic path selection)')
    parser.add_argument('--src', type=int, default=0,
                        help='source AS number')
    parser.add_argument('--dst', type=int, default=2,
                        help='destination AS number')
    parser.add_argument('--all-to-all', action='store_true',
                        help='use random source and destination pairs')
    parser.add_argument('--hybrid', action='store_true',
                        help='use a mix of Ciao and TCP flows')
    parser.add_argument('-a', '--all-types', action='store_true',
                        help='generate all types of scenarios, equivalent\
                        to --bgp-fd --bgp-ecmp --naive --ciao')

    args = parser.parse_args()

    startup_phase_end_seconds = STARTUP_PHASE_SECONDS

    # If output is a directory, then generate multiple scenarios
    # for a pre-defined range of flow numbers
    if os.path.isdir(args.output):
        output_path_template = os.path.join(args.output, "scenario.json")
        flow_numbers = DEFAULT_FLOW_NUMBERS
    else:
        output_path_template = args.output
        flow_numbers = [args.number_of_flows]

    for num_flows in flow_numbers:
        for iteration in range(args.iterations):
            flows = flow_generate(num_flows, time_span=args.time_slots,
                                  node_num=4, seed=str(iteration), hybrid=args.hybrid, diverse=args.all_to_all)

            settings = {
                "time_slots": args.time_slots,
                "slot_size": args.slot_size,
                "num_flows": num_flows,
                "iteration": iteration,
            }

            if args.path_change_margin:
                settings["path_change_margin"] = args.path_change_margin

            if args.ciao or args.all_types:
                create_scenario(flows, startup_phase_end_seconds, settings, ScenarioType.Ciao, output_path=output_path_template)
            if args.naive or args.all_types:
                create_scenario(flows, startup_phase_end_seconds, settings, ScenarioType.Naive, output_path=output_path_template)
            if args.bgp_fd or args.all_types:
                create_scenario(flows, startup_phase_end_seconds, settings, ScenarioType.BGP_FD, output_path=output_path_template)
            if args.bgp_ecmp or args.all_types:
                create_scenario(flows, startup_phase_end_seconds, settings, ScenarioType.BGP_ECMP, output_path=output_path_template)


if __name__ == "__main__":
    main()
