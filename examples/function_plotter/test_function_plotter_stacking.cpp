#include "function_plotter.h"

#include <iostream>

int main()
{
    Function_plotter plotter;
    plotter.add_function();

    plotter.set_stack_functions(true);
    for (int i = 0; i < plotter.function_count(); ++i) {
        if (plotter.get_function(i)->series()->stack_group != 1) {
            std::cerr << "existing function was not stacked\n";
            return 1;
        }
    }

    plotter.add_function();
    if (plotter.get_function(plotter.function_count() - 1)->series()->stack_group != 1) {
        std::cerr << "new function did not inherit stacking\n";
        return 1;
    }

    plotter.set_stack_functions(false);
    for (int i = 0; i < plotter.function_count(); ++i) {
        if (plotter.get_function(i)->series()->stack_group != 0) {
            std::cerr << "function remained stacked after disabling\n";
            return 1;
        }
    }

    return 0;
}
