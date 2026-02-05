#include <iostream>
#include <array>
#include <algorithm>
#include "../helper/randomFiller.cpp"

long sumArray(int *array, int length){
    long s = 0;

    for(int i=0; i < length; i = i + 4){
        int a1 = array[i];
        s += a1;
    }

    return s;
}

int main(){
    int length = 9000;
    int *searchroom = new int[length];
    fillArrayRandom(searchroom, length, length*4);

    long sum = 0;

    for(int i=0; i < length/3; i = i + 1){
        int a1 = searchroom[i];

        sum += a1;
    }

    std::cout << "Sum of array: " << sum << "\n";

    return 0;
}