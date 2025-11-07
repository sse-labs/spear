


#include "EnergyFunction.h"
#include "ProgramGraph.h"


//Constructor of the EnergyFunction class
EnergyFunction::EnergyFunction() {
    this->programGraph = new ProgramGraph();
    this->energy = 0.00;
    this->name = "";
}