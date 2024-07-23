import json
import random
import argparse
import os

import bgp_load_balancing


TOPOLOGY_FILE = "configs/traffic-engineering/toy-topology.xml"


def generate_flows(flow_num, time_span, node_num, seed):
    flow_list = []

    for flow_id in range(flow_num):
        current_seed = seed + str(flow_id)
        random.seed(current_seed)

        start_time = random.randint(0, time_span - 1)

        current_seed += 'a'
        random.seed(current_seed)

        duration = random.randint(
            min(2, time_span - start_time), time_span - start_time)

        source = args.src
        destination = args.dst

        if args.all_to_all:
            source = random.randint(0, node_num - 1)
            destination = random.randint(0, node_num - 1)

            # Ensure source and destination are not the same
            attempt_count = 0
            while source == destination:
                current_seed += str(attempt_count)
                random.seed(current_seed)
                destination = random.randint(0, node_num - 1)
                attempt_count += 1

        flow_type = 0  # Fixed value, can be changed to random if needed
        app_limit = 1.2  # Fixed value, can be adjusted if needed

        flow_list.append((flow_id, source, destination,
                         app_limit, duration, start_time, flow_type))

    return flow_list


def build_scenario(flow: list, startup_phase_end_seconds: int, timeslot_size_seconds: int,
                   path_switching: bool) -> dict:
    applications = []
    events = []

    # Add a random offset to the startup time of flows within the same timeslot
    timeslot_offsets = {}

    for f in flow:
        flow_id, src_as, dst_as, app_limit, duration, start_timeslot, flow_type = f

        app_type = "rtc" if flow_type == 0 else "TcpCubic"

        # UserDefinedEvents::StartApp (std::string src_isd_number, std::string real_src_as_no,
        #                      std::string src_local_address, std::string dst_isd_number,
        #                      std::string real_dst_as_no, std::string dst_local_address,
        #                      std::string app_type, std::string backgroundBwdFactor,
        #                      std::string app_id_str, std::string runtime_config)

        args = ["0", str(src_as), "2", "0", str(dst_as),
                "2", app_type, "0.0", str(flow_id), "0"]

        start_time_seconds = startup_phase_end_seconds + start_timeslot * timeslot_size_seconds

        offset = timeslot_offsets.get(start_timeslot, 0)
        timeslot_offsets[start_timeslot] = offset + random.uniform(timeslot_size_seconds / 100, timeslot_size_seconds / 20)

        start_time_offset_seconds = start_time_seconds + offset

        start_event = {
            "time": f"{start_time_offset_seconds:.2f}s",
            "type": "start_application",
            "args": args
        }

        events.append(start_event)

        end_time_seconds = start_time_seconds + duration * timeslot_size_seconds

        # Stop a bit before the end of the timeslot
        end_time_offset_seconds = end_time_seconds - timeslot_size_seconds / 20

        # UserDefinedEvents::StopAppTraffic (std::string src_isd_number, std::string real_src_as_no,
        #                           std::string src_local_address, std::string app_id)

        args = ["0", str(src_as), "2", str(flow_id)]

        end_event = {
            "time": f"{end_time_offset_seconds:.2f}s",
            "type": "stop_app_traffic",
            "args": args
        }
        events.append(end_event)

        applications.append({
            "app_id": flow_id,
            "src_as": src_as,
            "src_host": 2,
            "dst_as": dst_as,
            "dst_host": 2,
            "app_type": app_type,
            "start_slot": start_timeslot,
            "start_time": start_time_offset_seconds,
            "end_slot": start_timeslot + duration,
            "end_time": end_time_offset_seconds,
            "path_switching": path_switching,
        })

        # print(f"Adding flow {flow_id:02} of type {app_type:8} sending from AS {src_as} to AS {
        #       dst_as} running from time slot {start_timeslot:02} to {start_timeslot + duration:02} ({start_time_offset_seconds:.2f}s - {end_time_offset_seconds:.2f}s)")

    scenario = {
        "applications": applications,
        "events": events
    }

    return scenario


def add_bgp_lb_paths_to_flows(scenario: dict, iteration: int, use_ecmp: bool) -> None:
    for app in scenario["applications"]:
        p = bgp_load_balancing.get_path(
            TOPOLOGY_FILE, app["src_as"], app["src_host"], app["dst_as"], app["dst_host"], app["app_id"], iteration, use_ecmp)
        p = bgp_load_balancing.transform_path_json(p)
        app["path"] = p

    # print(f"Generated BGP load balanced paths for all flows")


def main():
    global args

    parser = argparse.ArgumentParser(
        description='Generate events for a JSON file.')
    parser.add_argument('-n', '--number-of-flows', type=int, default=4,
                        help='number of events to generate (default: 4)')
    parser.add_argument('-t', '--time-slots', type=int, default=5,
                        help='number of time slots (default: 5)')
    parser.add_argument('-s', '--slot-size', type=int, default=120,
                        help='size of a time slot in seconds (default: 120)')
    parser.add_argument('-i', '--iterations', type=int, default=3)
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
    parser.add_argument('-a', '--all-to-all', action='store_true',
                        help='use random source and destination pairs')

    args = parser.parse_args()

    num_flows_list = [args.number_of_flows]
    output_path_template = args.output

    # If output is a directory, then generate multiple scenarios
    # for a whole range of flow numbers
    if os.path.isdir(args.output):
        output_path_template = os.path.join(args.output, "scenario.json")
        num_flows_list = [2, 4, 6, 8, 10, 15, 20, 30, 50, 70, 100]

    for num_flows in num_flows_list:
        for iteration in range(args.iterations):
            flows = generate_flows(num_flows, time_span=args.time_slots,
                                   node_num=4, seed=str(iteration))

            scenario = build_scenario(
                flows, startup_phase_end_seconds=1800, timeslot_size_seconds=args.slot_size, path_switching=args.ciao)

            scenario["settings"] = {
                "time_slots": args.time_slots,
                "slot_size": args.slot_size,
                "num_flows": num_flows,
            }

            if args.bgp_fd:
                add_bgp_lb_paths_to_flows(scenario, iteration, use_ecmp=False)
                scenario["settings"]["description"] = "BGP FD"
                output_path = output_path_template.replace(".json", f"_{num_flows:02}-flows_{iteration:02}_bgp_fd.json")
            elif args.bgp_ecmp:
                add_bgp_lb_paths_to_flows(scenario, iteration, use_ecmp=True)
                scenario["settings"]["description"] = "BGP ECMP"
                output_path = output_path_template.replace(".json", f"_{num_flows:02}-flows_{iteration:02}_bgp_ecmp.json")
            elif args.naive:
                scenario["settings"]["description"] = "Naive"
                output_path = output_path_template.replace(".json", f"_{num_flows:02}-flows_{iteration:02}_naive.json")
            else:
                scenario["settings"]["description"] = "Ciao"
                output_path = output_path_template.replace(".json", f"_{num_flows:02}-flows_{iteration:02}_ciao.json")

            with open(output_path, 'w') as f:
                json.dump(scenario, f, indent=4)

            print(f"Saving scenario: {num_flows:2} flows, iteration {iteration:2} to {output_path}")


if __name__ == "__main__":
    main()
