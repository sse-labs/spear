"""
Utilization functions
"""

class Util:

    @staticmethod
    def to_comma_separated(arr):
        if len(arr) >= 2:
            return ", ".join(arr)
        return arr[0] if arr else ""

    @staticmethod
    def get_type_default(type: str):
        if  type == "i32":
            return "0"
        elif type == "i0":
            return "0"
        elif type == "i8":
            return "0"
        elif type == "float":
            return "0.0"
        return None

