#!/usr/bin/env python3

from os import listdir, path
import json
import subprocess
import yaml
import sys


def main():
    template_config_yaml = path.join("configs", "traffic-engineering/toy.yaml")
    # scenario_dir = path.join("pan", "scenarios")
    scenario_dir = None

    if len(sys.argv) > 1 and path.isdir(sys.argv[1]):
        scenario_dir = sys.argv[1]

    if not scenario_dir:
        print("Missing argument: scenario directory")
        return

    # Load the template config
    with open(template_config_yaml, "r") as f:
        config = yaml.safe_load(f)

    # Find all json files inside scenario_dir
    scenario_jsons = [path.join(scenario_dir, f) for f in listdir(scenario_dir) if f.endswith(".json") and "." in f]

    scen = 1
    for scenario_json_path in scenario_jsons:
        config['events_file'] = f"{scenario_json_path}"
        config['output'] = f"{scenario_json_path.replace('.json', '.out.txt')}"

        # Set the simulation duration according to time slots
        with open(scenario_json_path, "r") as f:
            scenario = json.load(f)
            simulation_duration = 1800 + scenario["settings"]["time_slots"] * scenario["settings"]["slot_size"]
        config['simulation_duration'] = f"{simulation_duration}s"

        # Write the config for this scenario
        config_yaml_path = scenario_json_path.replace(".json", ".yaml")
        with open(config_yaml_path, "w") as f:
            yaml.dump(config, f)

        # print(f"Generated config file: {config_yaml_path}")

        print(f"Setting up scenario {scen:2} out of {len(scenario_jsons)}: {config_yaml_path}")
        scen += 1

        subprocess.run(["python3.11", "waf", "--run", f"scion {config_yaml_path}"])


if __name__ == "__main__":
    main()
