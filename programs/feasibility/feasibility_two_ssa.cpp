/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
 */

int main(int argc, char **argv) {
    int length = argc;
    int cmp = (argc ^ (argv[0][0] & 1));   // depends on argv, so not a constant

    if (length > cmp) {
        if (length >= cmp) {
            if (length < cmp) {
                length++;
            }
        }
    }

    length--;
    return length;
}