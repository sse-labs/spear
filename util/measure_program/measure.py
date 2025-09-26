import sys
import os
import subprocess
import struct
import time
import csv

core = 0
iterations = 100
datapoints = 100

def runprogram(program, db):
    if db:
        command = f"taskset -c 0 {program} < {db}"
    else:
        command = f"taskset -c 0 {program}"
    try:
        result = subprocess.run(
            command,
            shell=True,           # Needed to use `<`
            capture_output=True,   # Capture stdout and stderr
            text=True              # Get output as string
        )

        if result.stdout:
            #print("STDOUT:\n", result.stdout)
            pass
        if result.stderr:
            print("STDERR:\n", result.stderr)

    except Exception as e:
        print("Error:", e)


def readRapl():
    registerpath = "/dev/cpu/{}/msr".format(core)
    energyreg = 0x639
    unitreg = 0x606

    energy = 0
    unit = 0

    with open(registerpath, 'rb') as rf:
        rf.seek(energyreg)
        rawenergy = rf.read(8)
        energy = struct.unpack('L', rawenergy)
        cleaned_energy = energy[0]

        rf.seek(0)
        rf.seek(unitreg)
        rawunit = rf.read(8)
        unit = struct.unpack('Q', rawunit)[0]
        cleaned_unit = (unit >> 8) & 0x1F

    return cleaned_energy * pow(0.5, cleaned_unit)


def measure(file, db):
    engergy_before = readRapl()

    runprogram(file, db)

    energy_after = readRapl()

    cost = energy_after - engergy_before
    return cost


def calculate_for_file(filepath, dbname):
    # Check if file exists
    if not os.path.exists(filepath):
        print(f"Error: File '{filepath}' does not exist.")
        sys.exit(1)

    # Example: Print absolute path and file size
    abs_path = os.path.abspath(filepath)
    file_size = os.path.getsize(filepath)

    summed_cost = 0
    summed_time = 0.0

    for i in range(1, iterations):
        start = time.perf_counter()
        cost = measure(abs_path, dbname)
        end = time.perf_counter()

        summed_time = summed_time + end - start
        summed_cost = summed_cost + cost
        #time.sleep(0.5)
        #print(f"Cost {cost}")
    
    mean = summed_cost / iterations
    mean_time = summed_time / iterations
    print(f"Mean cost for {filepath} over {iterations} iterations: {mean}. ({mean_time})")

    return ( mean, mean_time )

def main():

    files = [
        ("/home/max/Documents/repos/llvm-energy/examples/combine/libsodium/build/example/testprog_default", "", "default"),
        ("/home/max/Documents/repos/llvm-energy/examples/combine/libsodium/build/example/testprog_energy", "", "energy"),
    ]

    defaults = []
    optimized = []

    for i in range(datapoints):
        print(f"i={i}")
        for file in files:
            (eng, tme) = calculate_for_file(file[0], file[1])
            #time.sleep(10)
            
            if file[2] == "default":
                defaults.append({"id": i, "energy": eng, "time": tme})
            else:
                optimized.append({"id": i, "energy": eng, "time": tme})


    # Write defaults.csv
    with open("defaults.csv", "w", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=["id", "energy", "time"])
        writer.writeheader()
        writer.writerows(defaults)

    # Write optimized.csv
    with open("optimized.csv", "w", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=["id", "energy", "time"])
        writer.writeheader()
        writer.writerows(optimized)
    
if __name__ == "__main__":
    main()
