#include "function_plotter.h"

#include <QGuiApplication>

#include <iostream>

int main(int argc, char** argv)
{
    QGuiApplication app(argc, argv);

    vnm::plot::Plot_widget widget;
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

    Function_entry* first  = plotter.get_function(0);
    Function_entry* second = plotter.get_function(1);
    Function_entry* third  = plotter.get_function(2);
    plotter.move_function(0, 2);
    if (plotter.get_function(0) != second || plotter.get_function(1) != third ||
        plotter.get_function(2) != first ||
        !(second->series_id() < third->series_id() && third->series_id() < first->series_id()) ||
        second->series()->series_label != "f(x1)" || third->series()->series_label != "f(x2)" ||
        first->series()->series_label != "f(x3)")
    {
        std::cerr << "function reorder did not update model and stack order\n";
        return 1;
    }

    plotter.set_stack_functions(false);
    for (int i = 0; i < plotter.function_count(); ++i) {
        if (plotter.get_function(i)->series()->stack_group != 0) {
            std::cerr << "function remained stacked after disabling\n";
            return 1;
        }
    }

    plotter.set_plot_widget(&widget);
    if (widget.format_timestamp_precise(1250) != QStringLiteral("1.250")) {
        std::cerr << "function indicator x value lost fractional precision\n";
        return 1;
    }

    return 0;
}
