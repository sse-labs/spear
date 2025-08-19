import csv
import math
import os
import json
import sys
import matplotlib.pyplot as plt
import numpy as np
import time

result = {}
stats = {"mean": {}, "variant": {}, "stddeviation": {}}
group = [
    "_cachewarmer",
    "add",
    "add_f",
    "and",
    "call",
    "div_f",
    "eq",
    "mul",
    "mul_f",
    "ne",
    "or",
    "rem_f",
    "sdiv",
    "select",
    "sext",
    "sge",
    "sgt",
    "shl",
    "shr",
    "sle",
    "slt",
    "srem",
    "sub",
    "sub_f",
    "udiv",
    "uge",
    "ugt",
    "ule",
    "ult",
    "urem",
    "xor",
    "zext"
]

def plot():
    tpllist = {gro: [] for gro in group}
    singlegraphs = group
    allgraphs = group

    # Collect data
    for key in result.keys():
        for gro in singlegraphs:
            tpllist[gro].append((key, result[key][gro]))

    # Plot each group individually
    for gro in singlegraphs:
        xs = [tpl[0] for tpl in tpllist[gro]]
        ys = [tpl[1] for tpl in tpllist[gro]]

        fig, ax = plt.subplots(figsize=(16, 9))

        if gro != "duration":
            ax.axhline(y=stats["mean"][gro], color='r', linestyle='-', label="Mittelwert")
            ax.axhline(y=stats["mean"][gro] + stats["stddeviation"][gro], color='g', linestyle='--', label="± Standardabweichung")
            ax.axhline(y=stats["mean"][gro] - stats["stddeviation"][gro], color='g', linestyle='--')

            ax.plot(xs, ys, label="Gruppe " + gro)
            ax.set_xlabel('Iterationen')
            ax.set_ylabel('Energie in J')
            ax.set_title("Entwicklung der Energiewerte bei Änderung der Messwiederholungen")
        else:
            ax.plot(xs, ys, label="Laufzeit")
            ax.set_xlabel('Iterationen')
            ax.set_ylabel('Zeit in s')
            ax.set_title("Entwicklung der Laufzeit abhängig von den Messwiederholungen")

        ax.legend()
        fig.tight_layout()
        fig.savefig(f"stability/plot_{gro}.png")
        plt.close(fig)

    # Combined plot for all groups
    fig, ax = plt.subplots(figsize=(16, 9))

    for gro in allgraphs:
        xs = [tpl[0] for tpl in tpllist[gro]]
        ys = [tpl[1] for tpl in tpllist[gro]]
        ax.plot(xs, ys, label="Gruppe " + gro)

    ax.set_xlabel('Iterationen')
    ax.set_ylabel('Energie in J')
    ax.set_title("Entwicklung der Energiewerte bei Änderung der Messwiederholungen")
    ax.legend(ncol=2)
    fig.tight_layout()
    fig.savefig("stability/plot_gesamt.png")
    plt.close(fig)



def addToResult(iterations, data):
    result[iterations] = {}
    start = int(data["startOfExecution"])
    end = int(data["endOfExecution"])
    result[iterations]["duration"] = (end - start) / 1000000000.0

    for key in data["profile"].keys():
        result[iterations][key] = data["profile"][key]


def write_result_to_file():
    with open('./stability/stability_result.csv', 'w') as f:
        size = len(result.keys())
        groupvals = {}

        for gro in group:
            stats["mean"][gro] = 0.0
            stats["variant"][gro] = 0.0
            stats["stddeviation"][gro] = 0.0
            groupvals[gro] = []

        # Calc mean
        for key in result.keys():
            for gro in group:
                groupvals[gro].append(result[key][gro])

        for gro in group:
            stats["mean"][gro] = np.mean(groupvals[gro])
            stats["variant"][gro] = np.var(groupvals[gro])
            stats["stddeviation"][gro] = np.std(groupvals[gro])

        headers = ["iterations", "duration"] + group

        w = csv.writer(f)
        w.writerow(headers)
        for key in result.keys():
            entry = result[key]
            vals = list(entry.values())
            w.writerow([key] + vals)

        w.writerow(["mean", *list(stats["mean"].values())])
        w.writerow(["variant", *list(stats["variant"].values())])
        w.writerow(["stddeviation", *list(stats["stddeviation"].values())])


def main(spearpath, profilepath):
    iterations = range(1, 100 , 1)

    for its in iterations:
        abspahts = {"profile": os.path.abspath(profilepath), "savedir": os.path.abspath("./stability")}

        isExist = os.path.exists(abspahts["savedir"])
        if not isExist:
            os.makedirs(abspahts["savedir"])

        command = "sudo {} profile --iterations {} --model {} --savelocation {}".format(spearpath, 10000, abspahts["profile"], abspahts["savedir"])
        print(command)
        outputstream = os.popen(command)
        output = outputstream.read()
        print(output)
        with open("{}/profile.json".format(abspahts["savedir"]), 'r') as f:
            data = json.load(f)
            print(data)
            addToResult(its, data)

    print("Printing result to file...")
    write_result_to_file()
    plot()


if __name__ == "__main__":
    if len(sys.argv) == 3:
        main(sys.argv[1], sys.argv[2])
    else:
        print("Please provide a path to a folder containing .ll files")
