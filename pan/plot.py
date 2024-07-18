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
    parser.add_argument("-l", "--loss", action='store_true', help="Plot the loss")
    parser.add_argument("-d", "--latency", action='store_true', help="Plot the latency")
    parser.add_argument("-t", "--total", action='store_true', help="Plot the total send rate")
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

    # max number of apps before we stop showing the legend
    max_apps_legend = 10

    plot_loss = args.loss
    plot_latency = args.latency
    plot_total_send_rate = args.total
    plot_active_paths = args.paths
    plot_gcc_gradient = args.gradient
    plot_gcc_state = args.state
    plot_gcc_params = False

    # If true, automatically align metric timestamps to those of the first
    # application for summing up the total send rate
    auto_match_time = False

    # For which app to plot extra details if there are multiple applications
    detail_app = 0

    no_plots = 1
    size_y = 1
    ratios = [1]

    # Lazy hack to make plot sizing dynamic
    for var in list(locals()):
        if var.startswith('plot_') and locals()[var]:
            no_plots += 1
            size_y += 1
            ratios.append(1)

    fig = plt.figure(figsize=(size_y, 1))
    plot_number = 0
    gs = gridspec.GridSpec(no_plots, 1, height_ratios=ratios)

    # plt.subplots_adjust(left=0.05, right=0.95, top=0.95, bottom=-0.5 / no_plots)
    plt.subplots_adjust(left=0.05, right=0.95, top=0.95, bottom=0.05)

    lines_dict = dict()

    def on_pick(event):
        legend_text = None
        # Check if the pick event is on the legend text
        if event.artist in legend.get_texts():
            legend_text = event.artist.get_text()
        # Check if the pick event is on the legend line
        elif event.artist in legend.get_lines():
            line_index = legend.get_lines().index(event.artist)
            legend_text = legend.get_texts()[line_index].get_text()
            
        if legend_text:
            # Find the original lines associated with this legend entry
            origlines = lines_dict.get(legend_text, [])
            if origlines:
                visibility = origlines[0].get_visible()
                for origline in origlines:
                    origline.set_visible(not visibility)
                plt.gcf().canvas.draw()

    fig.canvas.mpl_connect('pick_event', on_pick)

    ax = None

    if plot_loss:
        if ax is None:
            ax = plt.subplot(gs[plot_number])
        else:
            plt.subplot(gs[plot_number], sharex=ax)
        plot_number += 1
        for app in apps:
            plt.plot(app['time'], app['loss'], label=app['name'])
        if len(apps) <= max_apps_legend:
            plt.legend()
        plt.ylabel("Loss [%]")

    if plot_latency:
        if ax is None:
            ax = plt.subplot(gs[plot_number])
        else:
            plt.subplot(gs[plot_number], sharex=ax)
        plot_number += 1
        for app in apps:
            plt.plot(app['time'], app['latency'], label=app['name'])
        if len(apps) <= max_apps_legend:
            plt.legend()
        plt.ylabel("Latency [ms]")

    if ax is None:
            ax = plt.subplot(gs[plot_number])
    else:
        plt.subplot(gs[plot_number], sharex=ax)
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
                 linewidth=0.5, color='black', linestyle='--')
        print(f"Median send rate:        {np.median(app['sendrate'])}")
    else:
        # Plot the sending rate for all applications
        for app in apps:
            lines = ()
            line_sendrate, = plt.plot(app['time'], app['sendrate'], label=app['name'], picker=5)
            lines += (line_sendrate,)
            # Plot bottleneck share and transition rates without a label to exclude them from the legend
            if 'bottleneck_share' in app:
                line_fair_share, = plt.plot(app['time'], app['bottleneck_share'], linewidth=0.5, color='black', linestyle='--', picker=5)
                # if not the detail app, hide the fair share line
                if app['name'] != apps[detail_app]['name']:
                    line_fair_share.set_visible(False)
                lines += (line_fair_share,)
            if 'oldrate' in app:
                line_oldrate, = plt.plot(app['time_transition'], app['oldrate'], linewidth=0.5, color='red', linestyle=':', picker=5)
                line_oldrate.set_visible(False)
                lines += (line_oldrate,)
            if 'newrate' in app:
                line_newrate, = plt.plot(app['time_transition'], app['newrate'], linewidth=0.5, color='blue', linestyle='-.', picker=5)
                line_newrate.set_visible(False)
                lines += (line_newrate,)

            # Store references to the lines for toggling
            lines_dict[app['name']] = lines
        
        # Print highest median send rate
        highest_median = 0
        highest_median_app = ""
        lowest_median = 1000
        lowest_median_app = ""
        for app in apps:
            median = np.median(app['sendrate'])
            if median > highest_median:
                highest_median = median
                highest_median_app = app['name']
            if median < lowest_median:
                lowest_median = median
                lowest_median_app = app['name']
        print(f"Highest median send rate: {highest_median} by app {highest_median_app})")
        print(f"Lowest median send rate:  {lowest_median} by app {lowest_median_app})")
    if len(apps) <= max_apps_legend:
        legend = plt.legend()
        # Make legend entries pickable
        for legline, origline in zip(legend.get_lines(), legend.get_texts()):
            legline.set_picker(5)  # 5 pts tolerance
            origline.set_picker(5)  # Also make the text pickable
    plt.ylabel("Send rate [MB/s]")

    if plot_total_send_rate:
        plt.subplot(gs[plot_number], sharex=ax)
        plot_number += 1

        if auto_match_time:
            time = apps[0]['time'].copy()
            total_rate = apps[0]['sendrate'].copy()

            # Try to match the closest sendrates of all applications to sum them up
            for app in apps[1:]:
                for i, t in enumerate(time):
                    # Find the closest time in the other app, make use of the fact that the time is sorted
                    closest_index = np.searchsorted(app['time'], t)
                    if closest_index >= len(app['time']):
                        continue
                    total_rate[i] += app['sendrate'][closest_index]

            plt.plot(time, total_rate, label="Total")

        else:
            sendrate_at_time = dict()
            for app in apps:
                for i, t in enumerate(app['time']):
                    if t not in sendrate_at_time:
                        sendrate_at_time[t] = app['sendrate'][i]
                    sendrate_at_time[t] += app['sendrate'][i]
            
            plt.plot(list(sendrate_at_time.keys()), list(sendrate_at_time.values()), label="Total")

            # Draw some lines for reference. Since all links are 1MB/s, a simple
            # upper bound for the total min(no_apps, no_paths)
            upper_bound = min(len(apps), max(apps[0]['active_path']) + 1)
            plt.axhline(y=upper_bound, color='black', linewidth=0.5, linestyle='--', label="Upper bound")

            print(f"Max total send rate:     {max(list(sendrate_at_time.values()))}")
            print(f"Median total send rate:  {np.median(list(sendrate_at_time.values()))}")
            print(f"Mean total send rate:    {np.mean(list(sendrate_at_time.values()))}")
        if len(apps) <= max_apps_legend:
            plt.legend()
        plt.legend()
        plt.ylabel("Total Send Rate [MB/s]")

    if plot_active_paths:
        plt.subplot(gs[plot_number], sharex=ax)
        plot_number += 1
        for app in apps:
            plt.plot(app['time'], app['active_path'], label=app['name'])

        if len(apps) <= max_apps_legend:
            plt.legend()
        plt.ylabel("Paths")

    if plot_gcc_state:
        plt.subplot(gs[plot_number], sharex=ax)
        plot_number += 1
        app = apps[detail_app]
        plt.plot(app['time'], app['gcc_state'], label="State",
                 drawstyle='steps-post', linestyle='--', linewidth=1)
        # plt.plot(app['time'], app['gcc_signal'], label="Signal",
        #          drawstyle='steps-post', linestyle='-.', linewidth=1)
        if len(apps) <= max_apps_legend:
            plt.legend()
        plt.ylabel("GCC State")

    if plot_gcc_gradient:
        plt.subplot(gs[plot_number], sharex=ax)
        plot_number += 1
        app = apps[detail_app]
        plt.plot(app['time'], app['gradient'], label="Gradient",
                 color='red', linestyle='-', linewidth=1)
        plt.plot(app['time'], app['threshold_hi'], label="threshold γ",
                 color='black', linestyle=':', linewidth=0.5)
        plt.plot(app['time'], app['threshold_lo'], label="threshold -γ",
                 color='black', linestyle=':', linewidth=0.5)
        if len(apps) <= max_apps_legend:
            plt.legend()
        plt.ylabel("GCC Gradient")

    if plot_gcc_params:
        plt.subplot(gs[plot_number], sharex=ax)
        plot_number += 1
        app = apps[detail_app]
        plt.plot(app['time'], app['kalman_gain'], label="Kalman gain",
                 color='red', linestyle='-', linewidth=1)
        plt.plot(app['time'], app['variance'], label="Variance",
                 color='green', linestyle='--', linewidth=1)
        plt.plot(app['time'], app['error'], label="Error",
                 color='black', linestyle=':', linewidth=1)

        if len(apps) <= max_apps_legend:
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
            
            # if app['states'] is null
            if 'states' not in app or app['states'] is None:
                print(f"    No states found for app {app['app_id']}")
                print(json.dumps(app, indent=4))
                continue

            # Remove startup from time and convert to seconds
            app['time'] = [state['time'] / 1000 -
                           1800 for state in app['states']]

            # Convert loss to percentage
            app['loss'] = [state['loss'] * 100 for state in app['states']]

            # Optional metrics for path transitions
            if 'in_transition' in app['states'][0]:
                app['oldrate'] = []
                app['newrate'] = []
                app['time_transition'] = []
                for i, state in enumerate(app['states']):
                    if state['in_transition']:
                        app['oldrate'].append(state['oldrate'])
                        app['newrate'].append(state['newrate'])
                        app['time_transition'].append(app['time'][i])
                    else:
                        app['oldrate'].append(None)
                        app['newrate'].append(None)
                        app['time_transition'].append(None)
            
            # For ever other field in states, generate a list of values
            for field in app['states'][0]:
                if field not in app:
                    app[field] = getStateAttributeList(app, field)

            if not 'name' in app:
                app['name'] = f"{app['app_id']}:{app['app_type']}"

            if 'bytes_sent' in app:
                print(f"    Bytes sent:     {app['bytes_sent']}")
            
            if 'bytes_received' in app:
                print(f"    Bytes received: {app['bytes_received']}")

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
