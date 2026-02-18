/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
 */

int main() {
    int length = 9;

    if (length < 10) {                 // 1  -> true
        if (length > 0) {              // 2  -> true
            if (length == 5) {         // 3  -> true
                length = 20;
            }
        }
    }

    if (length < 10) {                 // 1  -> true
        if (length > 0) {              // 2  -> true
            if (length == 5) {         // 3  -> true
                length = 30;
            }
        }
    }

    if (length < 10) {                 // 1  -> true
        if (length > 0) {              // 2  -> true
            if (length == 5) {         // 3  -> true
                length = 30;
            }
        }
    }

    length--;

    return 0;
}
