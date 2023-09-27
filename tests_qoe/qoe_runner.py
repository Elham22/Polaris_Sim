import argparse
import os
import time
import shutil
from typing import List
import random

class PathInfo:
    def __init__(self, path: str) -> None:
        self.linksOfPath(path)

    def linksOfPath(self, path: str):
        self.links = []
        linksRaw = path.strip().split("),")
        for linkRaw in linksRaw:
            tokens = linkRaw.strip(" ()").split(",")
            if len(tokens) < 3:
                break
            node = tokens[0].split(":")
            isd_number = int(node[0])
            as_number = int(node[1])

            ing = int(tokens[1][5:])
            eg = int(tokens[2][4:])
            self.links.append({'isd': isd_number, 'as': as_number, 'ing': ing, 'eg': eg})

def path_eval(filepath: str) -> List[PathInfo]:
    with open(filepath) as file:
        line = file.readline()
        
        while True:
            line = file.readline()
            if line == '':
                break
            if "Path info per app" in line:
                pathInfos = []
                line = file.readline()
                while not "End path info" in line:
                    line = file.readline()
                    while not "End of app path info" in line:
                        path = line.split('[')[1][:-4]
                        pathInfos.append(PathInfo(path))
                        line = file.readline()
                        
                    line = file.readline()
                
                return pathInfos[:int(len(pathInfos)/3)]
    return []


class Event:
    def __init__(self, time, type, args) -> None:
        self.time = time
        self.type = type
        self.args = args

    def to_string(self):
        res = f"{{\n\"time\": \"{self.time}\",\n\"type\": \"{self.type}\",\n\"args\": ["
        for i in range(len(self.args)-1):
            res = res + f"\"{self.args[i]}\", "
        if len(self.args) > 0:
            res = res + f"\"{self.args[-1]}\""
        res = res + f"]\n}}"
        return res

def parse_args():
    parser = argparse.ArgumentParser(prog="SCION simulator QoE runner", description="Runnning SCION simulator QoE experiments")
    parser.add_argument("--source", help="Source AS", type=int)
    parser.add_argument("--dest", help="Destination AS", type=int)
    parser.add_argument("--runs", help="Number of runs", type=int, default=1)
    parser.add_argument("--name", help="Name of the experiment. Will be applied to the filenames", default="qoe_exp")
    parser.add_argument("--congestion", help="Probability of each path to be congested", type=float, default=0.5)
    parser.add_argument("--bt_amplitude", help="Amplitude of the background traffic", type=float, default=1.1)
    parser.add_argument("--config", help="Name of the config file (path relative to the configs folder)", default="qoe_exp.yaml")
    parser.add_argument("--transitions", help="Change network state during a run", action='store_true')
    parser.add_argument("--save_configs", help="Save the configuration (the user defined events file) of each run", action='store_true')
    parser.add_argument("--multihost", help="Run multiple applications over multiple hosts", action='store_true')
    parser.add_argument("--mechanism", help="Select which mechanism to test from [active, passive, naive, given]", default="active")
    return parser.parse_args()

mechanisms = {
        'active': 'video conference active',
        'passive': 'video conference passive',
        'naive': 'video conference naive',
        'given': 'video conference given',
    }
def create_events_file(events_filepath, events: List[Event]):
    with open(events_filepath, 'w') as f:
        f.write('{"events":\n[\n')
        for i in range(len(events)-1):
            f.write(events[i].to_string() + ",\n")
        if len(events) > 0:
            f.write(events[-1].to_string() + "\n")
        f.write(']\n}')

def shortest_path(paths: List[PathInfo]):
    min_len = int(1e100)
    for path in paths:
        curr_len = len(path.links)
        if curr_len < min_len:
            min_len = curr_len
    
    consider_paths = list(filter(lambda path: len(path.links) <= min_len, paths))
    assert len(consider_paths) > 0
    asn = consider_paths[0].links[1]['as']
    for _ in range(100):
        shortest = consider_paths[random.randint(0, len(consider_paths)-1)]
        if shortest.links[1]['as'] == asn:
            break
    #bgp_path = consider_paths[0]
    shortest_string = "["
    for link in shortest.links:
        shortest_string += f"({link['isd']}, {link['as']}, {link['ing']}, {link['eg']}), "
    shortest_string += "]"
    return shortest_string

def create_congest_list(paths: List[PathInfo], congestion_prob):
    congest = []
    for j in range(len(paths)):
        if random.random() < congestion_prob:
            congest.append(j)
    return congest

def create_run_events(paths: List[PathInfo], congest: List[int], source, dest, bt_amplitude):
    events = []
    events.append(Event("102min", "send_packet", ["0", source, "2", "0", dest, "2", "1024"]))
    events.append(Event("103min", "start_application", ["0", source, "2", "0", dest, "2", "video conference active", "0.0"]))
    events.append(Event("103min", "start_application", ["0", source, "2", "0", dest, "2", "video conference passive", "0.0"]))
    events.append(Event("103min", "start_application", ["0", source, "2", "0", dest, "2", "video conference naive", "0.0"]))
    events.append(Event("103min", "start_application", ["0", source, "2", "0", dest, "2", f"video conference given:{shortest_path(paths)}", "0.0"]))

    for congestion in congest:
        path = paths[congestion]
        #for link in path.links[:-1]:
        link = path.links[0]
        events.append(Event("103.01min", "set_link_rate", [link['isd'], link['as'], link['ing'], link['eg'], bt_amplitude]))
    return events

def create_multihost_run_events(source, dest, bt_amplitude, mech, n_apps, paths):
    n_hosts = 10
    events = []
    for i in range(n_hosts):
        host = f"{i + 2}"
        events.append(Event("92min", "send_packet", ["0", source, host, "0", dest, host, "1024"]))

    startTime = 93
    increment = 2 / n_apps # two minutes start time
    for i in range(n_apps):
        host = f"{i % 10 + 2}"
        if mech == "given":
            mechanism_str = f"{mechanisms[mech]}:{shortest_path(paths)}"
        else:
            mechanism_str = mechanisms[mech]
        events.append(Event(f"{startTime}min", "start_application", ["0", source, host, "0", dest, host, mechanism_str, bt_amplitude]))
        startTime += increment
    return events

def main():
    startTime = time.time()
    args = parse_args()
    print(args)
    simulator_folder = "/cluster/home/passuter/scion-simulator"
    scratch_folder = "/cluster/scratch/passuter"
    config = f"{simulator_folder}/configs/{args.config}"
    name = "qoe_exp"
    if name != args.name:
        with open(config, 'r') as f:
            content = f.read()
        config = f"{scratch_folder}/{args.name}.yaml"
        with open(config, 'w') as f:
            f.write(content.replace(name, args.name))
        name = args.name
    events_filepath = scratch_folder + f"/{name}.json"
    results_filepath = scratch_folder + f"/{name}_results.txt"
    cmd = f"{simulator_folder}/waf --run \"scion {config}\""
    
    #initial run
    init_events = []
    init_events.append(Event("102min", "send_packet", ["0", args.source, "2", "0", args.dest, "2", "1024"]))
    init_events.append(Event("103min", "start_application", ["0", args.source, "2", "0", args.dest, "2", "video conference active", "0.0"]))
    create_events_file(events_filepath, init_events)
    os.system(cmd)
    paths = path_eval(results_filepath)
    if len(paths) == 0:
        print("No paths found in initial run. Terminating.")
        return

    #test case with multiple host competing for the network
    if args.multihost:
        if args.mechanism == "all":
            mechanismsList = list(mechanisms.keys())
        else:
            mechanismsList = [args.mechanism]

        for i in range(args.runs):
            if i < 4:
                n_apps = 5 * (i+1) # [5..20] step 5
            elif i < 7:
                n_apps = 20 * (i-2) # [40, 60, 80]
            elif i < 10:
                n_apps = 100 * (i-6) # [100, 200, 300]
            else:
                n_apps = 500 * (i-9) # [500, 1000, 1500, 2000, ...]
            for mech in mechanismsList:
                run_start_time = time.time()
                events = create_multihost_run_events(args.source, args.dest, args.bt_amplitude, mech, n_apps, paths)
                create_events_file(events_filepath, events)
                print(f"Executing run {i+1}/{args.runs} with {n_apps} apps")
                os.system(cmd)
                if i < 10:
                    run_id = f"0{i}"
                else:
                    run_id = f"{i}"
                shutil.copy(results_filepath, f"{scratch_folder}/{name}{mech}_run{run_id}({n_apps*10}).txt")
                if args.save_configs:
                    shutil.copy(events_filepath, f"{scratch_folder}/{name}{mech}_run{run_id}({n_apps*10}).json")
                runtime = time.time() - run_start_time
                print(f"Run {i+1} runtime: {int(runtime/3600)}:{int((runtime%3600)/60)}:{runtime%60}")

        runtime = time.time() - startTime
        print(f"Runtime {int(runtime/3600)}:{int((runtime%3600)/60)}:{runtime%60}")
        return

    #test case with one host searching for best path in partially congested network
    #for path in paths:
    #    print(path.links)

    for i in range(args.runs):
        run_start_time = time.time()
        congest = create_congest_list(paths, args.congestion)
        num_congest = len(congest)
        events = create_run_events(paths, congest, args.source, args.dest, args.bt_amplitude)
        if args.transitions:
            congest = create_congest_list(paths, args.congestion)
            for congestion in congest:
                path = paths[congestion]
                #for link in path.links[:-1]:
                link = path.links[0]
                events.append(Event("103.7min", "set_link_rate", [link['isd'], link['as'], link['ing'], link['eg'], args.bt_amplitude]))
            num_congest += len(congest)
            
            congest = create_congest_list(paths, args.congestion)
            for congestion in congest:
                path = paths[congestion]
                #for link in path.links[:-1]:
                link = path.links[0]
                events.append(Event("104.2min", "set_link_rate", [link['isd'], link['as'], link['ing'], link['eg'], args.bt_amplitude]))
            num_congest += len(congest)

        create_events_file(events_filepath, events)
        print(f"Executing run {i+1}/{args.runs} with {num_congest} congested paths (total)")
        os.system(cmd)
        shutil.copy(results_filepath, f"{scratch_folder}/{name}_run{i+1}.txt")
        if args.save_configs:
            shutil.copy(events_filepath, f"{scratch_folder}/{name}_run{i+1}.json")
        runtime = time.time() - run_start_time
        print(f"Run {i+1} runtime: {int(runtime/3600)}:{int((runtime%3600)/60)}:{runtime%60}")



    runtime = time.time() - startTime
    print(f"Runtime {int(runtime/3600)}:{int((runtime%3600)/60)}:{runtime%60}")

main()