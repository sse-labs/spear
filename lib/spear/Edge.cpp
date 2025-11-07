#include "ProgramGraph.h"

//Simple constructor. Set the properties to the given pointers
Edge::Edge(Node *start, Node *end) {
    this->start = start;
    this->end = end;
}

//Represents the Edge as string
std::string Edge::toString() const {
    //Init the string with an (
    std::string output = "(";

    //Append the start nodes string representation
    output.append(this->start->toString());

    //Append a separator
    output.append(" | ");

    //Append the end nodes string representation
    output.append(this->end->toString());

    //Close the parenthesis
    output.append(")");

    //Return the constructed string
    return output;
}
