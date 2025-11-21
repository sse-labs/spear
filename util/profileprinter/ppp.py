import sys
import json

def main():
    if len(sys.argv) < 2:
        print("Usage: python script.py <filename.json>")
        return

    filename = sys.argv[1]

    with open(filename, "r") as f:
        data = json.load(f)

    cpu = data.get("cpu", {})
    noise = cpu.get("_noise")

    if noise is None:
        print("JSON does not contain cpu._noise")
        return

    # Subtract noise from each value (except _noise itself)
    result = {
        key: (value - noise if isinstance(value, (int, float)) and key != "_noise" else value)
        for key, value in cpu.items()
    }

    # Replace original cpu section with processed one
    data["cpu"] = result

    print(json.dumps(data, indent=4))

if __name__ == "__main__":
    main()
