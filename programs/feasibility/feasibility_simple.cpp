/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
 */

int main(){
    int length = 9;
    int cheese = 10;

    if (length > 10) {
        length++;
    }

    length--;

    for (int i = 0; i < length; i++) {
        cheese++;
    }

    cheese--;

    return 0;
}