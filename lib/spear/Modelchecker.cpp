/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
 */

#include "Modelchecker.h"

Modelchecker::Modelchecker() = default;

void Modelchecker::addExpression(const z3::expr& expression) {
    elements.push_back(expression);
}

void Modelchecker::clearExpressions() {
    elements.clear();
}

z3::context* Modelchecker::getContext() {
    return &context;
}

z3::check_result Modelchecker::check() {
    z3::solver s(context);
    for (const auto& element : elements) {
        s.add(element);
    }
    return s.check();
}
