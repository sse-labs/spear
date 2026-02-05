#include <iostream>
#include <array>
#include <algorithm>
#include "../helper/randomFiller.cpp"

int main(){
    int length = 9000;
    int *searchroom = new int[length];
    fillArrayRandom(searchroom, length, length*4);

    long sum = 0;
    int i = 0;

    while(i < length) {
        int a1 = searchroom[i];
        int a2 = searchroom[i+1];
        int a3 = searchroom[i+2];
        int a4 = searchroom[i+3];

        sum += a1 + a2 + a3 + a4;

        if (i < length/2) {
            i = i + 4;
        } else {
            i = i + 3;
        }
    }

    int j = 0;

    while(j < length) {
        int a1 = searchroom[j];
        int a2 = searchroom[j+1];
        int a3 = searchroom[j+2];
        int a4 = searchroom[j+3];

        sum += a1 + a2 + a3 + a4;

        if (j < length/3) {
            j = j + 6;
        } else {
            j = j + 7;
        }
    }

    std::cout << "Sum of array 2x: " << sum << "\n";

    return 0;
}