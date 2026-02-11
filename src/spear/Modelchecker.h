/*
 * Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
 */

#ifndef SRC_SPEAR_MODELCHECKER_H_
#define SRC_SPEAR_MODELCHECKER_H_

#include <z3++.h>
#include <iostream>
#include <vector>

class Modelchecker {
 public:
    Modelchecker();

    /**
     * Adds the given expression to our list of expressions to check.
     * The expression should be in the context of this Modelchecker.
     * @param expression
     */
    void addExpression(const z3::expr& expression);

    /**
     * Clear the collected expressions.
     */
    void clearExpressions();

    /**
     * Query the context of the Modelchecker.
     * This is required to create expressions that are in the same context as the Modelchecker.
     * @return
     */
    z3::context* getContext();

    /**
     * Check if the collected expressions are satisfiable.
     * @return sat or unsat depending on the result of the check
     */
    z3::check_result check();

    static void example() {
        z3::context c;
        z3::expr x = c.int_const("x");
        z3::solver s(c);
        s.add(x > 5);
        std::cout << s.check() << "\n";
        if (s.check() == z3::sat) std::cout << s.get_model() << "\n";
        std::cout << "Hello from the Modelchecker!" << std::endl;
    }

 private:
    /**
     * The model context to create expressions in. All expressions added to the Modelchecker should be in this context.
     */
    z3::context context;

    /**
     * The expressions to check. These should be in the context of this Modelchecker.
     */
    std::vector<z3::expr> elements;
};

#endif  // SRC_SPEAR_MODELCHECKER_H_
