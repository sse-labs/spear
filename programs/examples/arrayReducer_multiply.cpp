#include <iostream>
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

        i=i*3;
    }

    std::cout << "Sum of array: " << sum << "\n";

    return 0;
}