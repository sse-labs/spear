/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
 */

int foo(int x) {
    return 0;
}

int main(){
    int length = 0;

    foo(length);

    if (length > 10) {
        length++;
    }

    length--;

    return 0;
}