#include <iostream>

void leafA() {
    std::cout << "leafA\n";
}

void leafB() {
    std::cout << "leafB\n";
}

void helper1() {
    leafA();
}

void helper2() {
    leafB();
}

void helper3() {
    leafA();
    leafB();
}

void midLevel() {
    helper1();
    helper2();
}

void topLevel() {
    midLevel();
    helper3();
}

int main() {
    topLevel();
    return 0;
}