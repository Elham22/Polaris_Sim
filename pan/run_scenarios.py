#!/usr/bin/env python3
import sys
from os import path, listdir
import yaml
import json
import subprocess
from concurrent.futures import ThreadPoolExecutor, as_completed
from copy import deepcopy


TEMPLATE_CONFIG_YAML = path.join("configs", "traffic-engineering/toy.yaml")
TOPO_FILE = "configs/traffic-engineering/toy-topology.xml"  # 10 MB/s links
# TOPO_FILE = "configs/traffic-engineering/toy-topology-10Mbps.xml"


def render_progress_bar(completed, total, bar_length=40):
    progress = completed / total
    block = int(round(bar_length * progress))
    bar = "▓" * block + "░" * (bar_length - block)
    print(f"\r{bar} {completed}/{total} ({progress:.2%})", end='')


def run_scenario(scenario_json_path, config, scen, total_scenarios):
    config['events_file'] = f"{scenario_json_path}"
    config['output'] = f"{scenario_json_path.replace('.json', '.out.txt')}"
    config['topology'] = TOPO_FILE

    # Set the simulation duration according to time slots
    with open(scenario_json_path, "r") as f:
        scenario = json.load(f)
        simulation_duration = 1800 + scenario["settings"]["time_slots"] * scenario["settings"]["slot_size"] + 100
    config['simulation_duration'] = f"{simulation_duration}s"

    # Write the config for this scenario
    config_yaml_path = scenario_json_path.replace(".json", ".yaml")
    with open(config_yaml_path, "w") as f:
        yaml.dump(config, f)

    result = subprocess.run(
        ["python3.11", "waf", "--run-no-build", f"scion {config_yaml_path}"],
        capture_output=True,
        text=True
    )

    if result.returncode != 0:
        print(f"\rError running simulation: {config_yaml_path}")
        # print("Standard Output:", result.stdout)
        print("Standard Error:", result.stderr)
    else:
        print(f"\rFinished simulation: {config_yaml_path}")


def main():
    scenario_dir = None

    if len(sys.argv) > 1 and path.isdir(sys.argv[1]):
        scenario_dir = sys.argv[1]

    if not scenario_dir:
        print("Missing argument: scenario directory")
        return

    # Run the build command
    print("Running waf build...")
    build_result = subprocess.run(["python3.11", "./waf", "build"], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if build_result.returncode != 0:
        print("Build failed. Exiting.")
        print(build_result.stdout.decode())
        print(build_result.stderr.decode())
        return

    # Load the template config
    with open(TEMPLATE_CONFIG_YAML, "r") as f:
        config = yaml.safe_load(f)

    # Find all json files inside scenario_dir
    scenario_jsons = []
    for f in listdir(scenario_dir):
        if f.endswith(".json"):
            # Used to filter which scenarios to (re-)run
            flows = int(f.split('-')[1][1:])
            iteration = int(f.split('-')[2][1:])
            # if iteration < 5:
            #     continue
            # if flows > 24:
            #     continue
            scenario_jsons.append(path.join(scenario_dir, f))

    total_scenarios = len(scenario_jsons)
    completed_scenarios = 0

    try:
        with ThreadPoolExecutor(max_workers=16) as executor:
            futures = [
                executor.submit(run_scenario, scenario_json_path, deepcopy(config), scen, total_scenarios)
                for scen, scenario_json_path in enumerate(scenario_jsons, start=1)
            ]

            print(f"Simulating {total_scenarios} scenarios")
            render_progress_bar(completed_scenarios, total_scenarios)

            for future in as_completed(futures):
                try:
                    future.result()
                except Exception as exc:
                    print(f"\nScenario generated an exception: {exc}")
                completed_scenarios += 1
                render_progress_bar(completed_scenarios, total_scenarios)
    except KeyboardInterrupt:
        print("\nSimulation interrupted.")
    finally:
        print()


if __name__ == "__main__":
    main()
