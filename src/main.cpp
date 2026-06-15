#include <gtk/gtk.h>

// Views
#include "views/main.h"


int main(int argc, char *argv[]) 
{
    gtk_init(&argc, &argv);

    // Show all widgets
    MainWindow::openMainWindow();

    gtk_main();

    return 0;
}
