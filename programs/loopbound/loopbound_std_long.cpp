#include <iostream>
#include <cstdlib>

int main() {
    int q = 0;
    int a = 2;
    int b = 1;
    int c = 333;

    for (int i = 0; i < 1000; i++) {
        q += i;
        q /= 5;

        // Nonsense arithmetic
        int x = (i * a + b) ^ c;
        int y = (x << 2) | (i & 7);
        int z = (y % (c + 1)) - (a * b);

        // Dead computation
        int unused = z * 42 + (rand() % 10);

        // Pointless branching
        if ((i % 3) == 0) {
            q += (x & 1);
        } else if ((i % 5) == 0) {
            q -= (y | 2);
        } else {
            q ^= z;
        }

        // More nonsense
        q += (i * i) % (a + b + c);
        q ^= (q << 1);
        q &= 0xFFFF;
    }

    for (int i = 0; i < 1000; i++) {
        q += i;
        q /= 5;

        // Nonsense arithmetic
        int x = (i * a + b) ^ c;
        int y = (x << 2) | (i & 7);
        int z = (y % (c + 1)) - (a * b);

        // Dead computation
        int unused = z * 42 + (rand() % 10);

        // Pointless branching
        if ((i % 3) == 0) {
            q += (x & 1);
        } else if ((i % 5) == 0) {
            q -= (y | 2);
        } else {
            q ^= z;
        }

        // More nonsense
        q += (i * i) % (a + b + c);
        q ^= (q << 1);
        q &= 0xFFFF;

    }

    for (int i = 0; i < 1000; i++) {
        q += i;
        q /= 5;

        // Nonsense arithmetic
        int x = (i * a + b) ^ c;
        int y = (x << 2) | (i & 7);
        int z = (y % (c + 1)) - (a * b);

        // Dead computation
        int unused = z * 42 + (rand() % 10);

        // Pointless branching
        if ((i % 3) == 0) {
            q += (x & 1);
        } else if ((i % 5) == 0) {
            q -= (y | 2);
        } else {
            q ^= z;
        }

        // More nonsense
        q += (i * i) % (a + b + c);
        q ^= (q << 1);
        q &= 0xFFFF;

    }

    for (int i = 0; i < 1000; i++) {
        q += i;
        q /= 5;

        // Nonsense arithmetic
        int x = (i * a + b) ^ c;
        int y = (x << 2) | (i & 7);
        int z = (y % (c + 1)) - (a * b);

        // Dead computation
        int unused = z * 42 + (rand() % 10);

        // Pointless branching
        if ((i % 3) == 0) {
            q += (x & 1);
        } else if ((i % 5) == 0) {
            q -= (y | 2);
        } else {
            q ^= z;
        }

        // More nonsense
        q += (i * i) % (a + b + c);
        q ^= (q << 1);
        q &= 0xFFFF;

    }

    for (int i = 0; i < 1000; i++) {
        q += i;
        q /= 5;

        // Nonsense arithmetic
        int x = (i * a + b) ^ c;
        int y = (x << 2) | (i & 7);
        int z = (y % (c + 1)) - (a * b);

        // Dead computation
        int unused = z * 42 + (rand() % 10);

        // Pointless branching
        if ((i % 3) == 0) {
            q += (x & 1);
        } else if ((i % 5) == 0) {
            q -= (y | 2);
        } else {
            q ^= z;
        }

        // More nonsense
        q += (i * i) % (a + b + c);
        q ^= (q << 1);
        q &= 0xFFFF;

    }

    for (int i = 0; i < 1000; i++) {
        q += i;
        q /= 5;

        // Nonsense arithmetic
        int x = (i * a + b) ^ c;
        int y = (x << 2) | (i & 7);
        int z = (y % (c + 1)) - (a * b);

        // Dead computation
        int unused = z * 42 + (rand() % 10);

        // Pointless branching
        if ((i % 3) == 0) {
            q += (x & 1);
        } else if ((i % 5) == 0) {
            q -= (y | 2);
        } else {
            q ^= z;
        }

        // More nonsense
        q += (i * i) % (a + b + c);
        q ^= (q << 1);
        q &= 0xFFFF;

    }

    for (int i = 0; i < 1000; i++) {
        q += i;
        q /= 5;

        // Nonsense arithmetic
        int x = (i * a + b) ^ c;
        int y = (x << 2) | (i & 7);
        int z = (y % (c + 1)) - (a * b);

        // Dead computation
        int unused = z * 42 + (rand() % 10);

        // Pointless branching
        if ((i % 3) == 0) {
            q += (x & 1);
        } else if ((i % 5) == 0) {
            q -= (y | 2);
        } else {
            q ^= z;
        }

        // More nonsense
        q += (i * i) % (a + b + c);
        q ^= (q << 1);
        q &= 0xFFFF;

    }

    for (int i = 0; i < 1000; i++) {
        q += i;
        q /= 5;

        // Nonsense arithmetic
        int x = (i * a + b) ^ c;
        int y = (x << 2) | (i & 7);
        int z = (y % (c + 1)) - (a * b);

        // Dead computation
        int unused = z * 42 + (rand() % 10);

        // Pointless branching
        if ((i % 3) == 0) {
            q += (x & 1);
        } else if ((i % 5) == 0) {
            q -= (y | 2);
        } else {
            q ^= z;
        }

        // More nonsense
        q += (i * i) % (a + b + c);
        q ^= (q << 1);
        q &= 0xFFFF;

    }

    for (int i = 0; i < 1000; i++) {
        q += i;
        q /= 5;

        // Nonsense arithmetic
        int x = (i * a + b) ^ c;
        int y = (x << 2) | (i & 7);
        int z = (y % (c + 1)) - (a * b);

        // Dead computation
        int unused = z * 42 + (rand() % 10);

        // Pointless branching
        if ((i % 3) == 0) {
            q += (x & 1);
        } else if ((i % 5) == 0) {
            q -= (y | 2);
        } else {
            q ^= z;
        }

        // More nonsense
        q += (i * i) % (a + b + c);
        q ^= (q << 1);
        q &= 0xFFFF;

    }

    for (int i = 0; i < 1000; i++) {
        q += i;
        q /= 5;

        // Nonsense arithmetic
        int x = (i * a + b) ^ c;
        int y = (x << 2) | (i & 7);
        int z = (y % (c + 1)) - (a * b);

        // Dead computation
        int unused = z * 42 + (rand() % 10);

        // Pointless branching
        if ((i % 3) == 0) {
            q += (x & 1);
        } else if ((i % 5) == 0) {
            q -= (y | 2);
        } else {
            q ^= z;
        }

        // More nonsense
        q += (i * i) % (a + b + c);
        q ^= (q << 1);
        q &= 0xFFFF;

    }

    for (int i = 0; i < 1000; i++) {
        q += i;
        q /= 5;

        // Nonsense arithmetic
        int x = (i * a + b) ^ c;
        int y = (x << 2) | (i & 7);
        int z = (y % (c + 1)) - (a * b);

        // Dead computation
        int unused = z * 42 + (rand() % 10);

        // Pointless branching
        if ((i % 3) == 0) {
            q += (x & 1);
        } else if ((i % 5) == 0) {
            q -= (y | 2);
        } else {
            q ^= z;
        }

        // More nonsense
        q += (i * i) % (a + b + c);
        q ^= (q << 1);
        q &= 0xFFFF;

    }

    for (int i = 0; i < 1000; i++) {
        q += i;
        q /= 5;

        // Nonsense arithmetic
        int x = (i * a + b) ^ c;
        int y = (x << 2) | (i & 7);
        int z = (y % (c + 1)) - (a * b);

        // Dead computation
        int unused = z * 42 + (rand() % 10);

        // Pointless branching
        if ((i % 3) == 0) {
            q += (x & 1);
        } else if ((i % 5) == 0) {
            q -= (y | 2);
        } else {
            q ^= z;
        }

        // More nonsense
        q += (i * i) % (a + b + c);
        q ^= (q << 1);
        q &= 0xFFFF;

    }

    for (int i = 0; i < 1000; i++) {
        q += i;
        q /= 5;

        // Nonsense arithmetic
        int x = (i * a + b) ^ c;
        int y = (x << 2) | (i & 7);
        int z = (y % (c + 1)) - (a * b);

        // Dead computation
        int unused = z * 42 + (rand() % 10);

        // Pointless branching
        if ((i % 3) == 0) {
            q += (x & 1);
        } else if ((i % 5) == 0) {
            q -= (y | 2);
        } else {
            q ^= z;
        }

        // More nonsense
        q += (i * i) % (a + b + c);
        q ^= (q << 1);
        q &= 0xFFFF;

    }

    for (int i = 0; i < 1000; i++) {
        q += i;
        q /= 5;

        // Nonsense arithmetic
        int x = (i * a + b) ^ c;
        int y = (x << 2) | (i & 7);
        int z = (y % (c + 1)) - (a * b);

        // Dead computation
        int unused = z * 42 + (rand() % 10);

        // Pointless branching
        if ((i % 3) == 0) {
            q += (x & 1);
        } else if ((i % 5) == 0) {
            q -= (y | 2);
        } else {
            q ^= z;
        }

        // More nonsense
        q += (i * i) % (a + b + c);
        q ^= (q << 1);
        q &= 0xFFFF;

    }

    return q;
}