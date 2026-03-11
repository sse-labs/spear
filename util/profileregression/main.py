import os
import re
import glob
import pandas as pd
import numpy as np
import matplotlib.pyplot as plt


def main():
    csv_files = glob.glob("../../cmake-build-debug/profile_*.csv")

    if not csv_files:
        print("No profile_*.csv files found.")
        return

    data = []

    for file in csv_files:
        match = re.search(r"profile_(\d+)\.csv$", os.path.basename(file))
        if not match:
            continue

        k = int(match.group(1))
        df = pd.read_csv(file)

        add_row = df[df["program"].str.lower() == "add"]

        if add_row.empty:
            print(f"'add' not found in {file}")
            continue

        median_value = add_row["median"].iloc[0]
        data.append((k, median_value))

    if not data:
        print("No data found for instruction 'add'.")
        return

    data.sort(key=lambda x: x[0])

    ks = np.array([x[0] for x in data])
    medians = np.array([x[1] for x in data])

    # lineare Regression
    slope, intercept = np.polyfit(ks, medians, 1)
    regression_line = slope * ks + intercept

    # Plot
    plt.figure(figsize=(8, 5))
    plt.scatter(ks, medians, label="Messwerte")
    plt.plot(ks, regression_line, label="Regression", linestyle="--")

    # Gleichung anzeigen
    equation = f"y = {slope/1000}x + {intercept:.6f}"
    plt.text(0.05, 0.95, equation, transform=plt.gca().transAxes,
             verticalalignment='top')

    plt.xlabel("k (Iterationen)")
    plt.ylabel("Median Energie")
    plt.title("Regression für ADD Instruktion")
    plt.legend()
    plt.grid(True)
    plt.tight_layout()
    plt.show()



if __name__ == "__main__":
    main()