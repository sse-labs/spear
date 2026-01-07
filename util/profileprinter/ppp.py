import sys
import json
import plotly.graph_objects as go

def main():
    if len(sys.argv) < 2:
        print("Usage: python script.py <filename.json>")
        return

    filename = sys.argv[1]

    with open(filename, "r") as f:
        data = json.load(f)

    cpu = data.get("cpu", {})
    if not isinstance(cpu, dict) or not cpu:
        print("No CPU data found (expected cpu to be a non-empty object/dict).")
        return

    # Keep only numeric values
    items = [(k, v) for k, v in cpu.items() if isinstance(v, (int, float))]
    if not items:
        print("CPU object contains no numeric values to plot.")
        return

    # Sort by value (optional, makes it easier to read)
    items.sort(key=lambda kv: kv[0])

    labels = [k for k, _ in items]
    values = [v for _, v in items]

    fig = go.Figure(go.Bar(x=labels, y=values))
    fig.update_layout(
        title="CPU values by instruction",
        xaxis_title="Instruction",
        yaxis_title="Value",
        xaxis_tickangle=-45,
        bargap=0.15,
    )
    fig.show()

if __name__ == "__main__":
    main()