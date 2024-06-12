out_folder = "./"

import argparse
import matplotlib.pyplot as plt
from matplotlib import gridspec
from typing import List, Tuple
from cycler import cycler
import os
import numpy as np

class AppResults:

    def __init__(self, host, app_id, app_type):
        self.host = host
        self.app_id = app_id
        self.app_type = app_type
        self.time = []
        self.loss = []
        self.path = []
        self.score = []
        self.latency = []
        self.quality = []
        self.bytes = []
        self.send_rate = [] # current send rate
        self.A_r = [] # receiver estimated send rate
        self.A_s = [] # sender estimated send rate
        self.fair_share = [] # fair share for this path as estimated via probing
        self.controller_state = [] # state of the delay based controller
        self.controller_signal = [] # signal of the delay based controller
        self.controller_gradient_estimate = []
        self.controller_gradient_measured = []
        self.controller_treshold = []
        self.kalman_gain = []
        self.variance = []
        self.e = []

    def get_name(self):
        return f"{self.host}:{self.app_id}, {self.app_type}"

    def print(self):
        if ("VCA" in self.app_type):
            print(f"{self.host}:{self.app_id}, type {self.app_type}, avg loss: {sum(self.loss) / len(self.loss)}, avg lat: {sum(self.latency) / len(self.latency)}, avg score: {sum(self.score) / len(self.score)}")
        else:
            print(f"{self.host}:{self.app_id}, type {self.app_type}, avg loss: {sum(self.loss) / len(self.loss)}, avg lat: {sum(self.latency) / len(self.latency)}, avg send rate: {sum(self.send_rate) / len(self.send_rate)}")

class LinkResults:
    def __init__(self, address, ing, eg) -> None:
        self.address = address
        self.ing = ing
        self.eg = eg
        self.info = f"{address} ing {ing} eg {eg}"
        self.time = []
        self.throughput = []
        self.loss = []

    def print(self):
        print(f"Link {self.address}, {self.ing}, {self.eg}: avg loss: {sum(self.loss) / len(self.loss)}")

class PathInfo:
    def __init__(self, host, app_id) -> None:
        self.host = host
        self.app_id = app_id
        self.paths = []

def parse_args():
    parser = argparse.ArgumentParser(prog="SCION simulator host QOE plotter", description="Plotting of host QOE simulation results from SCION simulator")
    parser.add_argument("filepath", help="Input file (result file of simulation) to parse")
    parser.add_argument("-ms", "--milliseconds", help="Plot timestamps in milliseconds", action="store_true")
    parser.add_argument("--details", help="Print and plot details for each run", action="store_true")
    parser.add_argument("--plotLinks", help="Plot link statistics as well.", action="store_true")
    parser.add_argument("--startTime", help="Start time of the experiments", type=float, default=0.0)
    parser.add_argument("--nolegend", help="Do not add legends to the plot", action="store_true")
    parser.add_argument("--multihost", help="Combine results of multiple multihost runs", action="store_true")
    parser.add_argument("--pathnumber", help="Show the number of path selections instead of which app which paht", action="store_true")
    parser.add_argument("--plotMulti", help="Plot the combined results of a multihost set", action="store_true")
    parser.add_argument("--saveFig", help="Save figure instead of showing it", action="store_true")
    parser.add_argument("--excludeStartup", help="Cut the startup phase from the results", action="store_true")
    return parser.parse_args()


def parse_app_results(host, app_id, file):
    app_info_tokens = file.readline().strip().split(' ')

    if "rtc" == app_info_tokens[1]:
        app_type = "rtc"
        file.readline()
        results = AppResults(host, app_id, app_type)
        tokens = file.readline().strip().split(',')

        while len(tokens) > 6 and not "End of app" in tokens[0]:
            # strip all tokens
            tokens = [token.strip() for token in tokens]
            i = 0
            timestamp = float(tokens[i].split("(")[0])
            if args.milliseconds:
                relative_timestamp = timestamp
            else:
                relative_timestamp = timestamp / 1000 / 60  # convert to minutes

            results.time.append(relative_timestamp - args.startTime)
            i += 1
            results.latency.append(float(tokens[i]))
            i += 1
            results.loss.append(float(tokens[i])*100)
            i += 1
            results.path.append(int(tokens[i]))
            i += 1
            results.send_rate.append(float(tokens[i]))
            i += 1
            results.A_s.append(float(tokens[i]))
            i += 1
            results.A_r.append(float(tokens[i]))
            i += 1
            results.fair_share.append(float(tokens[i]))
            i += 1
            results.controller_state.append(int(tokens[i]))
            i += 1
            results.controller_signal.append(int(tokens[i]))
            i += 1
            results.controller_gradient_estimate.append(float(tokens[i]))
            i += 1
            results.controller_treshold.append(float(tokens[i]))
            i += 1
            results.controller_gradient_measured.append(float(tokens[i]))
            i += 1
            results.kalman_gain.append(float(tokens[i]))
            i += 1
            results.variance.append(float(tokens[i]))
            i += 1
            results.e.append(float(tokens[i]))

            tokens = file.readline().strip().split(',')
        return results

    elif len(app_info_tokens) >= 7 and "VCA" == app_info_tokens[1]:
        app_type = app_info_tokens[2]
        file.readline()
        results = AppResults(host, app_id, app_type)
        tokens = file.readline().strip().split(',')

        # strip all tokens
        tokens = [token.strip() for token in tokens]

        while len(tokens) > 6 and not "End of app" in tokens[0]:
            i = 0
            if "ComputeScore" in tokens[i]:
                i += 3

            timestamp = float(tokens[i].split("(")[0])
            if args.milliseconds:
                relative_timestamp = timestamp
            else:
                relative_timestamp = timestamp / 1000 / 60  # convert to minutes

            results.time.append(relative_timestamp - args.startTime)
            i += 1
            results.latency.append(float(tokens[i]))
            i += 1
            results.loss.append(float(tokens[i])*100)
            i += 1
            results.bytes.append(float(tokens[i]))
            i += 1
            results.path.append(int(tokens[i]))
            i += 1
            results.quality.append(float(tokens[i]))
            i += 1
            results.score.append(float(tokens[i]))

            tokens = file.readline().strip().split(', ')
        return results
    else:
        print("Unknown app type ", app_info_tokens)



def plotAppResults(results: List[AppResults], pathInfos: List[PathInfo]):

    default_cycler = (cycler(color=['r', 'g', 'b', 'y']) +
                  cycler(linestyle=['-', '--', ':', '-.']))
    plt.rc('axes', prop_cycle=default_cycler)

    plot_number = 411
    if args.plotLinks:
        plot_number += 200

    plt.figure(figsize=(9, 1)) 
    plot_number = 0
    gs = gridspec.GridSpec(7, 1, height_ratios=[1, 1, 1, 1, 1, 3, 1]) 
    ax1 = plt.subplot(gs[plot_number])
    plot_number += 1
    for res in results:
        plt.plot (res.time, res.loss, label = res.get_name())
    if not args.nolegend:
        plt.legend()
    """if args.milliseconds:
        plt.xlabel("Time (ms)")
    else:
        plt.xlabel("Time (min)")"""
    plt.ylabel("Loss (%)")

    plt.subplot(gs[plot_number], sharex=ax1)
    plot_number += 1
    for res in results:
        plt.plot (res.time, res.latency, label = res.get_name())
    if not args.nolegend:
        plt.legend()
    """if args.milliseconds:
        plt.xlabel("Time (ms)")
    else:
        plt.xlabel("Time (min)")"""
    plt.ylabel("Latency (ms)")

    plt.subplot(gs[plot_number], sharex=ax1)
    plot_number += 1
    if len(results) == 1: # If there is only one application, plot all the rates
         for res in results[:1]:
            plt.plot (res.time, res.send_rate, label = "Send rate", linewidth=1.5)
            plt.plot (res.time, res.A_r, label = "A_r")
            plt.plot (res.time, res.A_s, label = "A_s")
            plt.plot (res.time, res.fair_share, label = "fair share", linewidth=0.5)
    else: # Plot just the sending rate for all applications
        for res in results:
            plt.plot (res.time, res.send_rate, label = res.get_name())
       
    if not args.nolegend:
        plt.legend()
    """if args.milliseconds:
        plt.xlabel("Time (ms)")
    else:
        plt.xlabel("Time (min)")"""
    plt.ylabel("Send rate [MB/s]")

    # plt.subplot(gs[plot_number], sharex=ax1)
    # plot_number += 1
    # # create a new list that contains the sums of all the bytes at each time
    # time_rate_map = {}
    # for res in results:
    #     for i in range(len(res.time)):
    #         if res.time[i] not in time_rate_map:
    #             time_rate_map[res.time[i]] = res.bytes[i]
    #         else:
    #             time_rate_map[res.time[i]] += res.bytes[i]
    # total_times = sorted(time_rate_map.keys())
    # total_bytes = [time_rate_map[time] for time in sorted(time_rate_map.keys())]
    # plt.plot(total_times, total_bytes, label = "Total")
    # if not args.nolegend:
    #     plt.legend()
    # """if args.milliseconds:
    #     plt.xlabel("Time (ms)")
    # else:
    #     plt.xlabel("Time (min)")"""
    # plt.ylabel("Total Send Rate [MB/s]")

    if not args.pathnumber:
        plt.subplot(gs[plot_number], sharex=ax1)
        plot_number += 1
        for res in results:
            # pathInfo = [x for x in pathInfos if x.host == res.host and x.app_id == res.app_id][0]
            # #paths = list(map(lambda path_id: pathInfo.paths[path_id], res.path))
            # paths = list(map(lambda path_id: f"id {int(path_id % (len(pathInfo.paths)/3))}", res.path))
            # plt.plot (res.time, paths, label = res.get_name())
            plt.plot (res.time, res.path, label = res.get_name())
        if not args.nolegend:
            plt.legend()
        if args.milliseconds:
            plt.xlabel("Time (ms)")
        else:
            plt.xlabel("Time (min)")
        plt.ylabel("Paths")
    else:
        plt.subplot(gs[plot_number], sharex=ax1)
        plot_number += 1
        print((len(pathInfos[0].paths), len(results[0].path)))
        paths_amount = np.zeros((len(pathInfos[0].paths), len(results[0].path)))
        for res in results:
            pathInfo = [x for x in pathInfos if x.host == res.host and x.app_id == res.app_id][0]
            #paths = list(map(lambda path_id: pathInfo.paths[path_id], res.path))
            for i, path in enumerate(res.path):
                path_id = int(path % (len(pathInfo.paths)/3))
                paths_amount[path_id, i] += 1

        for i in range(len(pathInfos[0].paths)):
            path = paths_amount[i]
            if path.sum() > 0:
                plt.plot (results[0].time, path, label = f"path_id {i}")
        plt.legend()
        if args.milliseconds:
            plt.xlabel("Time (ms)")
        else:
            plt.xlabel("Time (min)")
        plt.ylabel("Num selected")

    plt.subplot(gs[plot_number], sharex=ax1)
    plot_number += 1
    for res in results[:1]:
        plt.plot (res.time, res.controller_state, label = "State", drawstyle='steps-post', linestyle='--', linewidth=1)
        plt.plot (res.time, res.controller_signal, label = "Signal", drawstyle='steps-post', linestyle='-.', linewidth=1)
    plt.ylim(-0.25, 2.25)
    if not args.nolegend:
        plt.legend()
    """if args.milliseconds:
        plt.xlabel("Time (ms)")
    else:
        plt.xlabel("Time (min)")"""
    plt.ylabel("Controller")

    # Plot the gradient and treshold
    plt.subplot(gs[plot_number], sharex=ax1)
    plot_number += 1
    for res in results[:1]:
        # treshold are both black and dotted
        plt.plot (res.time, res.controller_gradient_estimate, label = "Gradient estimate m", color='red', linestyle='-', linewidth=1)
        plt.plot (res.time, [x / 5 for x in res.controller_gradient_measured], label = "Gradient measured d_m", color='green', linestyle='--', linewidth=0.5)
        plt.plot (res.time, res.controller_treshold, label = "Treshold γ", color='black', linestyle=':', linewidth=0.5)
        plt.plot (res.time, [-x for x in res.controller_treshold], label = "Treshold -γ", color='black', linestyle=':', linewidth=0.5)
    if not args.nolegend:
        plt.legend()
    """if args.milliseconds:
        plt.xlabel("Time (ms)")
    else:
        plt.xlabel("Time (min)")"""
    plt.ylabel("Gradient and treshold [ms]")

    # Plot the kalman gain, variance and e
    plt.subplot(gs[plot_number], sharex=ax1)
    plot_number += 1
    for res in results[:1]:
        plt.plot (res.time, res.kalman_gain, label = "Kalman gain", color='red', linestyle='-', linewidth=1)
        plt.plot (res.time, res.variance, label = "Variance", color='green', linestyle='--', linewidth=1)
        plt.plot (res.time, res.e, label = "e", color='black', linestyle=':', linewidth=1)
    if not args.nolegend:
        plt.legend()
    if args.milliseconds:
        plt.xlabel("Time (ms)")
    else:
        plt.xlabel("Time (min)")
    plt.ylabel("State parameters")

    plt.tight_layout()

def host_eval(file):
    results : List[AppResults] = []
    while (True):
        host_tokens = file.readline().strip().split(' ')
        if len(host_tokens) < 6 or (not "Host" in host_tokens[0]):
            break
        host = host_tokens[2]
        num_apps = int(host_tokens[4])
        for i in range(num_apps):
            res = parse_app_results(host, i, file)
            if (res != None):
                results.append(res)
            else:
                print(f"Failed to parse app {i} results")
    
    if args.details:
        for res in results:
            res.print()

    return results

def plotLinkResults(allresults: List[LinkResults], links: List[List]):
    results = list(filter(lambda res: filterLink(res, links), allresults))
    ax1 = fig.get_axes()[0]
    plot_number = 615
    plt.subplot(plot_number, sharex=ax1)
    plot_number += 1
    for res in results:
        plt.plot(res.time, res.throughput, label = res.info)
    if not args.nolegend:
        plt.legend()
    if args.milliseconds:
        plt.xlabel("Time (ms)")
    else:
        plt.xlabel("Time (min)")
    plt.ylabel("Throughput (MB/s)")

    plt.subplot(plot_number, sharex=ax1)
    plot_number += 1
    for res in results:
        plt.plot(res.time, res.loss, label = res.info)
    if not args.nolegend:
        plt.legend()
    if args.milliseconds:
        plt.xlabel("Time (ms)")
    else:
        plt.xlabel("Time (min)")
    plt.ylabel("Loss (%)")

def plotMulti(names, tuples):
    mechs = ["active", "passive", "naive", "given"]
    default_cycler = (cycler(color=['r', 'g', 'b', 'y']) +
                  cycler(linestyle=['-', '--', ':', '-.']))
    for (title, results) in tuples:
        assert len(names) == len(results)
        fig = plt.figure()
        plt.rc('axes', prop_cycle=default_cycler)
        for mech in mechs:
            apps = []
            vals = []
            for i, name in enumerate(names):
                if not mech in name:
                    continue
                #run_id = int(name.split("run")[1].split(".")[0])
                #napps = run_id * 100
                napps = int(name.split("(")[1].split(")")[0])
                if napps > 1e3:
                    continue # only plot up to a size
                apps.append(napps)
                vals.append(results[i])
            if mech == "given":
                labelMech = "Shortest path"
            else:
                labelMech = mech
            plt.plot(apps, vals, label=labelMech)
        plt.legend()
        plt.xlabel("Number apps")
        plt.ylabel(title)
        if args.saveFig:
            plt.savefig(f"figures/{title.split('(')[0]}.png", format="png")
        else:
            plt.show()
            # maximize the plot window
            figManager = plt.get_current_fig_manager()
            figManager.window.showMaximized()

def selectLinks(appResults: List[AppResults], pathInfos: List[PathInfo]):
    selected = []
    for appRes in appResults:
        for pathInfo in pathInfos:
            if appRes.host == pathInfo.host and appRes.app_id == pathInfo.app_id:
                paths = [pathInfo.paths[id] for id in appRes.path]
                for path in paths:
                    links = linksOfPath(path)
                    selected.extend([link for link in links if not link in selected])
    return selected

def linksOfPath(path: str):
    links = []
    linksRaw = path.strip().split("),")
    for linkRaw in linksRaw:
        tokens = linkRaw.strip(" ()").split(",")
        if len(tokens) < 3:
            break
        node = tokens[0] + ":0"
        ing = int(tokens[1][5:])
        eg = int(tokens[2][4:])
        links.append([node, ing, eg])
    return links

"""
def filterLink (result: LinkResults):
    links = [
        ["0:0:0", 8, 0],
        ["0:0:0", 10, 0],
        ["0:0:0", 3, 0],
    ]
    for link in links:
        if result.address == link[0] and result.ing == link[1] and result.eg == link[2]:
            return True
    return False
"""
def filterLink (result: LinkResults, links):
    for link in links:
        if result.address == link[0] and result.ing == link[1] and result.eg == link[2]:
            return True
    return False

def link_eval(file):
    line: str = file.readline()
    resultsList = []
    while not "End link statistics" in line:
        tokens = line.split(' ')
        if tokens[0] == "BR":
            results = LinkResults(tokens[1], int(tokens[3]), int(tokens[5]))
            file.readline()
            line = file.readline()
            while not "End results of Link" in line:
                tokens = line.split(",")
                if len(tokens) < 3:
                    raise RuntimeError("Invalid link statistics")
                if args.milliseconds:
                    timestamp = float(tokens[0])
                else:
                    timestamp = float(tokens[0]) / 1000 / 60 # convert to minutes
                results.time.append(timestamp)
                results.throughput.append(int(tokens[1])/1e6)
                results.loss.append(float(tokens[2])*100)
                line = file.readline()
            resultsList.append(results)
        line = file.readline()

    if args.details:
        for res in resultsList:
            res.print()
    return resultsList

def path_eval(file):
    resultsList = []
    line = file.readline()
    while not "End path info" in line:
        hostTokens = line.split(' ')
        pathInfo = PathInfo(hostTokens[0], int(hostTokens[1]))
        line = file.readline()
        while not "End of app path info" in line:
            path = line.split('[')[1][:-4]
            pathInfo.paths.append(path)
            line = file.readline()
            
        resultsList.append(pathInfo)
        line = file.readline()
    return resultsList
            


def main():
    global args
    args = parse_args()
    paths = []
    results_file = "./out.txt"
    if os.path.isdir(args.filepath):
        for file in os.listdir(args.filepath):
            paths.append(os.path.join(args.filepath, file))
    else:
        paths.append(args.filepath)

    print(f"Found {len(paths)} files")

    if args.plotMulti:
        names = []
        losses = []
        scores = []
        latencies = []
        throughput = []
        quality = []
        linkLosses = []
        numLinkCongestions = []
        secondsCongestions = []

    print("Parsing files")
    for filepath in paths:
        appRes = []
        with open(filepath) as file:
            line = file.readline()
            
            while True:
                line = file.readline()
                if line == '':
                    break
                if "########################### Host Apps evaluation #####################################" in line:
                    appRes = host_eval(file)
                if "########################### Link Statistics" in line:
                    linkRes = link_eval(file)
                if "Path info per app" in line:
                    pathInfos = path_eval(file)

        if len(appRes) == 0:
            print(f"No app results found in {filepath}")
            continue

        with open(results_file, "a") as outf:
            name = os.path.splitext(os.path.basename(filepath))[0]
            types = ["active", "passive", "naive", "given"]
            outf.write(f"{name}, ")
            if not args.multihost:
                for t in types:
                    found = False
                    for res in appRes:
                        if res.app_type == t:
                            outf.write(f"{t}, {sum(res.loss) / len(res.loss)}, {sum(res.latency) / len(res.latency)}, {sum(res.score) / len(res.score)}, ")
                            found = True
                            break
                    if not found:
                        outf.write(", , , , ")
            else:
                args.nolegend = True
                score = 0
                loss = 0
                lat = 0
                qualitySum = 0
                throughputSum = 0
                startIndex = 0
                startTime = args.startTime
                if not args.milliseconds:
                    startTime *= 60 * 1000
                
                for res in appRes:
                    if args.excludeStartup:
                        for startIndex, time in enumerate(res.time):
                            if time > startTime:
                                break
                    length = len(res.score[startIndex:])
                    score += sum(res.score[startIndex:]) / length
                    loss += sum(res.loss[startIndex:]) / length
                    lat += sum(res.latency[startIndex:]) / length
                    qualitySum += sum(res.quality[startIndex:]) / length
                    throughputSum += sum(res.bytes[startIndex:])/2 / length
                linkLoss = 0
                congestedLinks = 0
                secondsCongested = 0
                for link in linkRes:
                    if args.excludeStartup:
                        for startIndex, time in enumerate(link.time):
                            if time > startTime:
                                break
                    sumLoss = sum(link.loss[startIndex:]) / len(link.loss[startIndex:])
                    linkLoss += sumLoss
                    if sumLoss > 0:
                        congestedLinks += 1
                        secondsCongested += len([x for x in link.loss[startIndex:] if x > 0])
                
                if args.plotMulti:
                    names.append(name)
                    scores.append(score/len(appRes))
                    losses.append(loss/len(appRes))
                    latencies.append(lat/len(appRes))
                    quality.append(qualitySum/len(appRes))
                    throughput.append(throughputSum/len(appRes))
                    linkLosses.append(linkLoss/len(linkRes))
                    numLinkCongestions.append(congestedLinks)
                    secondsCongestions.append(secondsCongested/len(linkRes))
                outf.write(f"{score/len(appRes)}, {loss/len(appRes)}, {lat/len(appRes)}, links:, {linkLoss/len(linkRes)}, {congestedLinks}, {secondsCongested/len(linkRes)}")

            outf.write("\n")
 
        if args.details:
            plotAppResults(appRes, pathInfos)
            if args.plotLinks:
                selectedLinks = selectLinks(appRes, pathInfos)
                plotLinkResults(linkRes, selectedLinks)
            #plt.subplots_adjust(left=0.1, right=0.97)
            #plt.xlim(0.0, 0.5)
            print(f"Showing {name}")
            plt.show()

    if args.plotMulti:
        print("Creating plots for multihosts case")
        plotMulti(names, [("Avg. score", scores),
                          ("Avg. loss (%)", losses),
                          ("Avg. latency (ms)", latencies),
                          ("Avg. sending rate (categorical)", quality),
                          ("Avg. throughput received (Bytes/s)", throughput),
                          ("Avg. loss per link (%)", linkLosses),
                          ("Number of links experienced congestion", numLinkCongestions),
                          ("Avg. time of congestion per link (seconds)", secondsCongestions)]) 

    print("Done. Exiting.")

main()