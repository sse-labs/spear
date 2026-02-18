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
                                    if (length == 9) { // 9 -> true
                                        if (length <= 15) { // 10 -> true
                                            if (length != 0) { // 11 -> true
                                                if (length > 2) { // 12 -> true
                                                    if (length < 50) { // 13 -> true
                                                        if (length != 7) { // 14 -> true
                                                            if (length <= 3) { // 15 -> true
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
                        }
                    }
                }
            }
        }
    }

    length--;

    return 0;
}
