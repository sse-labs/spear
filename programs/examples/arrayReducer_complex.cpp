#include <iostream>
#include <array>
#include <algorithm>
#include "../helper/randomFiller.cpp"

long sumArray(int *array, int length){
    long s = 0;

    for(int i=0; i < length; i = i + 4){
        int a1 = array[i];
        int a2 = array[i+1];
        int a3 = array[i+2];
        int a4 = array[i+3];

        s += a1 + a2 + a3 + a4;
    }

    return s;
}

int main(){
    int length = 9000;
    int *searchroom = new int[length];
    fillArrayRandom(searchroom, length, length*4);

    long sum = 0;

    for(int i=0; i < length; i = i + 4){
        int a1 = searchroom[i];
        int a2 = searchroom[i+1];
        int a3 = searchroom[i+2];
        int a4 = searchroom[i+3];

        sum += a1 + a2 + a3 + a4;
    }

    long ssum = sumArray(searchroom, length);
    std::cout << "Sum of array: " << sum << "\n";
    std::cout << "Sum of array method call: " << ssum << "\n";

    return 0;
}