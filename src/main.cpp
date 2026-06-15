#include <gtk/gtk.h>

#include <string>

// Views
#include "views/main.h"


int main(int argc, char *argv[]) 
{
    bool hidden = false;
    std::string command;

    for (int i = 1; i < argc; ++i)
    {
        std::string arg = argv[i];
        if (arg == "--hidden")
        {
            hidden = true;
        }
        else if (arg == "--command" && i + 1 < argc)
        {
            command = argv[++i];
        }
        else if (arg == "--show")
        {
            command = "show";
        }
    }

    if (!command.empty())
    {
        return MainWindow::writeCommandFile(command) ? 0 : 1;
    }

    if (!MainWindow::acquireAppLock(!hidden))
    {
        return 0;
    }

    int gtkArgc = 1;
    char **gtkArgv = argv;
    gtk_init(&gtkArgc, &gtkArgv);

    MainWindow::openMainWindow(hidden);

    gtk_main();

    return 0;
}
