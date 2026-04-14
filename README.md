
<p align="center">
  <img src="./misc/logo.png">
</p>

---


Welcome to SPEAR, the 
**S**tatic **P**redictive **E**nergy **A**nalysis Tool based on Intel **R**APL.


## Dependencies

SPEAR depends on several components to work. Make sure the following components are installed on your system 
before installing SPEAR.

* **Intel RAPL Interface**
  Required for energy profiling. Available on most modern Intel processors.
  If your CPU does not support RAPL, SPEAR cannot be used.
  Note: Support is limited to native systems. Behavior in containers or virtual machines is undefined.

* **LLVM (version 17)**
  SPEAR operates on LLVM IR and requires LLVM 17.
  Install via your package manager or from the official LLVM website.
  Using a different version may lead to undefined behavior.

* **Phasar**
  Used for static analysis of LLVM IR.
  Follow the installation instructions in the official Phasar repository.

* **Z3**
  SMT solver used for analysis.
  Install via your package manager or from the official Z3 repository.

* **CMake**
  Required to configure the build process.

* **Make**
  Used to compile the project.

* **Python 3**
  Required for auxiliary scripts.

* **bpftool**
  Used to profile system call behavior.
  Install via your package manager.

* **nlohmann-json**
  Header-only C++ library used for JSON output.
  Can be installed via a package manager or included manually.

---

## Installation

We added an installation script to automate the building and copying of the application. The script was designed to be run under Debian and uses the apt package manager.
If you want to use the script on a different distribution adapt the installation script accordingly. Alternatively you could build SPEAR using the build commands, see section [building](#building) for further details.

⚠️ The installation script requires you to have elevated rights. Please execute it using sudo. The application will be installed for all users of the machine.

To run the script execute:
```
chmod +x install.sh
sudo ./install.sh
```

## Introduction

Modern computers, especially in high computing applications require a lot of energy.
Data centers, which handle all of our modern Cloud-Infrastructure, are trying to save every last bit of energy to reduce costs.
Even though energy seems to be a problematic factor regarding cost and environment, most
developers are not well aware about the energy-consumption of their software. Either trough missing information about their
used architecture or through the abstraction their used language implements.

To work towards filling this knowledge-gap, this bachelor-thesis provides a tool
for static analysis of LLVM-IR Code which will get populated with energy-consumption profiling
from the Intel RAPL Interface.

## Running the profiler

Before running an analysis with spear, you have to profile your device.
Use the SPEAR `profile` command to generate a profile for your machine. The profile will be used to calculate the 
energy consumption of instructions and functions.

```bash
sudo spear profile \
    --config /etc/spear/defaultconfig.json \
    --model /etc/spear/profile/ \
    --savelocation .
```
The profile command uses the configuration file specified with `--config` to determine specific settings for the profiling 
process. The default config file is located in `/etc/spear/defaultconfig.json`. See the documentation for the config file
for more information on the available settings.

The `--model` parameter specifies the location of the profile-scripts, which will be used to generate the profile.
We generate a profile of size `10000` iterations by default, which should be sufficient for most use-cases.
To generate a new model use the supplied profile generator script. See the additional documentation for information on
how to generate a new model. 

The generated profile is saved to the current directory by default, 
but you can specify a different location with the `--savelocation` parameter.

Make sure that you have the necessary permissions to run the profiler, as it usually requires elevated privileges to 
access the RAPL interface.

## Running the Analysis

SPEAR provides the `analyze` command to statically estimate the energy consumption of a program based on a 
previously generated profile.

The command expects at least the following parameters:

* `--profile`  
  Path to the machine-specific energy profile generated with `spear profile`.

* `--config`  
  Path to the SPEAR configuration file. By default this is usually `/etc/spear/defaultconfig.json`.

* `--program`  
  Path to the LLVM IR file (`.ll`) that should be analyzed.

Example:

```bash
spear analyze \
    --profile profile.json \
    --config /etc/spear/defaultconfig.json \
    --program ../programs/loopbound/compiled/arrayReducer_forinfor.ll
```

To create a valid program file, you can compile your C/C++ code to LLVM IR using clang:

```bash
clang++-17 -g -O0 -Xclang -disable-O0-optnone -fno-discard-value-names -S -emit-llvm -emit-llvm -o output.ll input.c
```

Make sure to include the `-g` flag to preserve debug information, which is necessary for accurate analysis. 
The `-O0` flag disables optimizations, and the additional flags ensure that the generated LLVM IR retains necessary 
information for SPEAR to analyze effectively. Make sure that the version of clang you use matches the version of LLVM 
that SPEAR is built against (LLVM 17 in this case) to avoid compatibility issues.

## Results

The analysis prints additional information about the analyzed program to the console, including the estimated energy 
consumption of each function and instruction. Additionally by default a JSON file is generated in the current directory 
with the name `<name of the programm>.json` which contains the same information in a structured format.

See the documentation section TODO for more information about the structure of the generated JSON file.

Example:

```json
{
  "analysis": "monolithic",
  "duration": 2628,
  "functions": {
    "main": {
      "energy": 0.0011188347546023424,
      "nodes": [
        {
          "energy": 4.226557849702381e-07,
          "name": "entry",
          "type": "node"
        },
        {
          "callee": "_Z18benchmark_functioni",
          "energy": 0.0005184120988173721,
          "name": "Call to _Z18benchmark_functioni",
          "type": "call"
        },
        {
          "callee": "_ZNSolsEi",
          "energy": 0.0003,
          "name": "Call to _ZNSolsEi",
          "type": "call"
        },
        {
          "callee": "xyz",
          "energy": 0.0003,
          "name": "Call to xyz",
          "type": "call"
        }
      ]
    }
  }
}
```

## Contribute

Please feel free to open issues in this repository and create merge request if you like. Please respect, 
that I run this repository as side project and can only spend my time partly on developing SPEAR.

If you encounter a problem and want to create an issue,
please describe your system and problem detailed as possible. A detailed explanation on how to reproduce 
the problem should be provided.
