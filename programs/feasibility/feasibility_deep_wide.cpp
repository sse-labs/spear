/*
* Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
 */

int main() {
    int length = 9;
    int cheese = 10;

    if (length < 10) {                 // 1  -> true
        if (length > 0) {              // 2  -> true
            if (length != 5) {         // 3  -> true
                if (length <= 9) {     // 4  -> true
                    if (length >= 1) { // 5  -> true
                        if (length < 20) {  // 6  -> true
                            if (length != 100) { // 7 -> true
                                if (length == 9) { // 8 -> true
                                    if (length != 9) { // 9 -> true
                                        cheese++;
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    if (length < 10) {                 // 1  -> true
        if (length > 0) {              // 2  -> true
            if (length != 5) {         // 3  -> true
                if (length <= 9) {     // 4  -> true
                    if (length >= 1) { // 5  -> true
                        if (length < 20) {  // 6  -> true
                            if (length != 100) { // 7 -> true
                                if (length == 9) { // 8 -> true
                                    if (length != 9) { // 9 -> true
                                        cheese++;
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    if (length < 10) {                 // 1  -> true
        if (length > 0) {              // 2  -> true
            if (length != 5) {         // 3  -> true
                if (length <= 9) {     // 4  -> true
                    if (length >= 1) { // 5  -> true
                        if (length < 20) {  // 6  -> true
                            if (length != 100) { // 7 -> true
                                if (length == 9) { // 8 -> true
                                    if (length != 9) { // 9 -> true
                                        cheese++;
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    length--;

    return 0;
}
