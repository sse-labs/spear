/*
* Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
 */

int main(int argc, char **argv) {
    int length = argc;
    int cmp = (argc ^ (argv[0][0] & 1));

    int x = 0;

    if (cmp) {
        x = 42;

        x = x / 2;


        if (x > 10) {
            x++;
        }
    } else {
        x = -1;

        x = x * 2;


        if (x > 10) {
            x--;
        }
    }

    x++;

    return 0;
}