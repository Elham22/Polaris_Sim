import json
import random
import argparse

import bgp_load_balancing


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

        source = 0
        destination = 2
        # source = random.randint(0, node_num - 1)
        # dest = random.randint(0, node_num - 1)

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


def generate_random_events(app_id, time):
    source_node = random.randint(0, 3)
    target_node = random.randint(0, 3)

    while source_node == target_node:
        target_node = random.randint(0, 3)

    # TODO: Make these test scenarios configurable
    source_node = 0
    target_node = 1

    args = ["0", str(source_node), "2", "0", str(target_node),
            "2", "rtc", "0.0", str(app_id), "0"]

    event = {
        "time": f"{time:.2f}min",
        "type": "start_application",
        "args": args
    }

    print(f"Application {app_id} sending from {source_node} to {
          target_node} starting at {time:.2f}min")

    return event


def generate_bgp_lb_paths(events: dict, out_path: str):
    topology_file = "configs/traffic-engineering/toy-topology.xml"
    paths = {}
    for e in events:
        src_as = e["args"][1]
        src_host = e["args"][2]
        dest_as = e["args"][4]
        dest_host = e["args"][5]
        app_id = e["args"][8]
        p = bgp_load_balancing.get_path(
            topology_file, int(src_as), int(src_host), int(dest_as), int(dest_host), int(app_id))
        p = bgp_load_balancing.transform_path_json(p)
        key = f"{src_as}-{dest_as}-{src_host}-{dest_host}-{app_id}"
        paths[key] = p

    return paths


def main():
    parser = argparse.ArgumentParser(
        description='Generate events for a JSON file.')
    parser.add_argument('-e', '--events', type=int, default=4,
                        help='number of events to generate (default: 4)')
    parser.add_argument(
        '-o', '--output', default='configs/traffic-engineering/toy.json', help='output path')
    parser.add_argument('--bgp-lb', action='store_true',
                        help='Generate a GBP LB paths file alongside the events (default: False)')

    args = parser.parse_args()

    number_of_events = args.events

    # TODO: use the generate_flows function to re-produce the same random scenarios used for MILP
    # flows = generate_flows(number_of_events, 30, 4, str(0))

    events = []
    time = 30.0  # starting time in minutes

    for i in range(number_of_events):
        event = generate_random_events(i, time)
        events.append(event)
        # increment time by a random amount for variety
        time += random.uniform(0.0, 0.1)

    output = {
        "events": events
    }

    if args.bgp_lb:
        output["paths"] = generate_bgp_lb_paths(events, args.output)

    with open(args.output, 'w') as f:
        json.dump(output, f, indent=4)

    print(f"Generated scenario with {
          number_of_events} flows and saved to {args.output}")


if __name__ == "__main__":
    main()
