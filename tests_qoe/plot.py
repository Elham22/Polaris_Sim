import argparse
import matplotlib.pyplot as plt
from matplotlib import gridspec
from cycler import cycler
import json
import os

import numpy as np


def parse_args():
    parser = argparse.ArgumentParser(prog="SCION simulator host QOE plotter",
                                     description="Plotting of host QOE simulation results from SCION simulator")
    parser.add_argument(
        "filepath", help="Input file (result file of simulation) to parse")
    parser.add_argument("-g", "--gradient",
                        action="store_true", help="Plot the GCC gradient")
    parser.add_argument("-p", "--paths",
                        action="store_true", help="Plot the active paths")
    parser.add_argument("-s", "--state", action="store_true",
                        help="Plot the GCC state")
    return parser.parse_args()


def getStateAttributeList(app: dict, attribute: str):
    states = app['states']
    return [state[attribute] for state in states]


def plotAppResults(apps: list[dict]):
    default_cycler = (cycler(color=['r', 'g', 'b', 'y']) +
                      cycler(linestyle=['-', '--', ':', '-.']))
    plt.rc('axes', prop_cycle=default_cycler)
    # plt.style.use('dark_background')

    plot_total_send_rate = False
    plot_active_paths = args.paths
    plot_gcc_gradient = args.gradient
    plot_gcc_state = args.state
    plot_gcc_params = False

    # For which app to plot extra details if there are multiple applications
    detail_app = 0

    no_plots = 4
    size_y = 4
    ratios = [1, 1, 1, 1]

    # Lazy hack to make plot sizing dynamic
    for var in list(locals()):
        if var.startswith('plot_') and locals()[var]:
            no_plots += 1
            size_y += 1
            ratios.append(1)

    plt.figure(figsize=(size_y, 1))
    plot_number = 0
    gs = gridspec.GridSpec(no_plots, 1, height_ratios=ratios)

    plt.subplots_adjust(left=0.05, right=0.95, top=0.95, bottom=-0.5 / no_plots)

    ax1 = plt.subplot(gs[plot_number])
    plot_number += 1
    for app in apps:
        plt.plot(app['time'], app['loss'], label=app['name'])

        plt.legend()
    plt.ylabel("Loss [%]")

    plt.subplot(gs[plot_number], sharex=ax1)
    plot_number += 1
    for app in apps:
        plt.plot(app['time'], app['latency'], label=app['name'])

        plt.legend()
    plt.ylabel("Latency [ms]")

    plt.subplot(gs[plot_number], sharex=ax1)
    plot_number += 1
    if len(apps) == 1:  # If there is only one application, plot everything for it
        app = apps[0]
        plt.plot(app['time'], app['sendrate'],
                 label=app['name'], linewidth=1.5)
        if 'A_s' in app:
            plt.plot(app['time'], app['A_s'], label="A_s")
        if 'A_r' in app:
            plt.plot(app['time'], app['A_r'], label="A_r")
        plt.plot(app['time'], app['bottleneck_share'], label="fair share",
                 linewidth=0.5, color='black')
    else:
        # Plot the sending rate for all applications
        for app in apps:
            plt.plot(app['time'], app['sendrate'], label=app['name'])
        # ..but the bottleneck_share only for the selected application
        app = apps[detail_app]
        plt.plot(app['time'], app['bottleneck_share'],
                 label=f"{detail_app}: fair share", linewidth=0.5, color='black')
    plt.legend()
    plt.ylabel("Send rate [MB/s]")

    if plot_total_send_rate:
        plt.subplot(gs[plot_number], sharex=ax1)
        plot_number += 1

        time = apps[0]['time'].copy()
        total_rate = apps[0]['sendrate'].copy()

        # Try to match the closest sendrates of all applications to sum them up
        for app in apps[1:]:
            for i, t in enumerate(time):
                # Find the closest time in the other app
                closest_time = min(app['time'], key=lambda x: abs(x - t))
                closest_index = app['time'].index(closest_time)
                total_rate[i] += app['sendrate'][closest_index]

        plt.plot(time, total_rate, label="Total")
        plt.grid()
        plt.legend()
        plt.ylabel("Total Send Rate [MB/s]")

    if plot_active_paths:
        plt.subplot(gs[plot_number], sharex=ax1)
        plot_number += 1
        for app in apps:
            plt.plot(app['time'], app['active_path'], label=app['name'])

        plt.legend()
        plt.ylabel("Paths")

    if plot_gcc_state:
        plt.subplot(gs[plot_number], sharex=ax1)
        plot_number += 1
        app = apps[detail_app]
        plt.plot(app['time'], app['gcc_state'], label="State",
                 drawstyle='steps-post', linestyle='--', linewidth=1)
        # plt.plot(app['time'], app['gcc_signal'], label="Signal",
        #          drawstyle='steps-post', linestyle='-.', linewidth=1)
        plt.legend()
        plt.ylabel("GCC State")

    if plot_gcc_gradient:
        plt.subplot(gs[plot_number], sharex=ax1)
        plot_number += 1
        app = apps[detail_app]
        plt.plot(app['time'], app['gradient'], label="Gradient",
                 color='red', linestyle='-', linewidth=1)
        plt.plot(app['time'], app['threshold_hi'], label="threshold γ",
                 color='black', linestyle=':', linewidth=0.5)
        plt.plot(app['time'], app['threshold_lo'], label="threshold -γ",
                 color='black', linestyle=':', linewidth=0.5)
        plt.legend()
        plt.ylabel("GCC Gradient")

    if plot_gcc_params:
        plt.subplot(gs[plot_number], sharex=ax1)
        plot_number += 1
        app = apps[detail_app]
        plt.plot(app['time'], app['kalman_gain'], label="Kalman gain",
                 color='red', linestyle='-', linewidth=1)
        plt.plot(app['time'], app['variance'], label="Variance",
                 color='green', linestyle='--', linewidth=1)
        plt.plot(app['time'], app['error'], label="Error",
                 color='black', linestyle=':', linewidth=1)

        plt.legend()
        plt.ylabel("GCC Parameters")

    xlabel = "Time (s)"
    plt.xlabel(xlabel)
    # plt.tight_layout()
    plt.show()


def host_eval(file):
    results = []
    while (True):
        line = file.readline().strip()
        if "End Apps evaluation" in line:
            break
        elif "Host at" in line:
            host_tokens = line.split(' ')
            host = host_tokens[2]
        elif "{" in line:
            app = json.loads(line)
            app['host'] = host
            print(f"Found results for host {host} app {
                  app['app_id']} type {app['app_type']}")

            # Remove startup from time and convert to seconds
            app['time'] = [state['time'] / 1000 -
                           1800 for state in app['states']]

            # Convert loss to percentage
            app['loss'] = [state['loss'] * 100 for state in app['states']]

            # For ever other field in states, generate a list of values
            for field in app['states'][0]:
                if field not in app:
                    app[field] = getStateAttributeList(app, field)

            if not 'name' in app:
                app['name'] = f"{app['app_id']}:{app['app_type']}"

            results.append(app)

    return results


def main():
    global args
    args = parse_args()
    paths = []
    if os.path.isdir(args.filepath):
        for file in os.listdir(args.filepath):
            paths.append(os.path.join(args.filepath, file))
    else:
        paths.append(args.filepath)

    print(f"Found {len(paths)} files")

    print("Parsing files")
    for filepath in paths:
        apps = []
        with open(filepath) as file:
            while True:
                line = file.readline()
                if line == '':
                    break
                if "# Host Apps evaluation" in line:
                    apps = host_eval(file)

        if len(apps) == 0:
            print(f"No app results found in {filepath}")
            continue

        plotAppResults(apps)

    print("Done. Exiting.")


main()
