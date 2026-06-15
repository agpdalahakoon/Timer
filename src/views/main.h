#include <gtk/gtk.h>
#include <gdk/gdkx.h>
#include <X11/extensions/scrnsaver.h>

#include <chrono>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>

#define APP_MAIN_WINDOW_WIDTH 270
#define APP_MAIN_WINDOW_HEIGHT 130

namespace MainWindow
{
    GtkWidget *mainWindow = nullptr;
    GtkWidget *timeLabel = nullptr;
    GtkWidget *startPauseBtn = nullptr;
    GtkWidget *stopBtn = nullptr;

    bool isRunning = false;
    guint timerSourceId = 0;
    long long totalElapsedSeconds = 0;
    long long lastAutoSavedElapsedSeconds = 0;
    std::chrono::steady_clock::time_point runStartTime;

    constexpr unsigned long DESKTOP_IDLE_PAUSE_MS = 5UL * 60UL * 1000UL;

    const std::string stateFilePath = "/home/praveen/soft/Timer/timer_state.txt";
    const std::string pauseLogFilePath = "/home/praveen/soft/Timer/pause_log.txt";

    long long loadSavedElapsedSeconds()
    {
        std::ifstream stateFile(stateFilePath);
        long long seconds = 0;
        if (stateFile.is_open())
        {
            stateFile >> seconds;
        }
        return seconds >= 0 ? seconds : 0;
    }

    void saveElapsedSeconds(long long seconds)
    {
        std::ofstream stateFile(stateFilePath, std::ios::trunc);
        if (stateFile.is_open())
        {
            stateFile << seconds;
        }
    }

    std::string formatElapsed(long long seconds)
    {
        long long hours = seconds / 3600;
        long long minutes = (seconds % 3600) / 60;
        long long remainingSeconds = seconds % 60;

        std::ostringstream out;
        out << std::setfill('0') << std::setw(2) << hours << ":"
            << std::setfill('0') << std::setw(2) << minutes << ":"
            << std::setfill('0') << std::setw(2) << remainingSeconds;
        return out.str();
    }

    bool getDesktopIdleMilliseconds(unsigned long &idleMilliseconds)
    {
        GdkDisplay *gdkDisplay = gdk_display_get_default();
        if (gdkDisplay == nullptr || !GDK_IS_X11_DISPLAY(gdkDisplay))
        {
            return false;
        }

        Display *xDisplay = gdk_x11_display_get_xdisplay(gdkDisplay);
        if (xDisplay == nullptr)
        {
            return false;
        }

        int eventBase = 0;
        int errorBase = 0;
        if (!XScreenSaverQueryExtension(xDisplay, &eventBase, &errorBase))
        {
            return false;
        }

        XScreenSaverInfo *idleInfo = XScreenSaverAllocInfo();
        if (idleInfo == nullptr)
        {
            return false;
        }

        bool hasIdleInfo = XScreenSaverQueryInfo(xDisplay, DefaultRootWindow(xDisplay), idleInfo);
        if (hasIdleInfo)
        {
            idleMilliseconds = idleInfo->idle;
        }

        XFree(idleInfo);
        return hasIdleInfo;
    }

    long long currentDisplayedElapsedSeconds()
    {
        if (!isRunning)
        {
            return totalElapsedSeconds;
        }

        auto now = std::chrono::steady_clock::now();
        auto segment = std::chrono::duration_cast<std::chrono::seconds>(now - runStartTime).count();
        return totalElapsedSeconds + segment;
    }

    void refreshElapsedLabel()
    {
        std::string text = formatElapsed(currentDisplayedElapsedSeconds());
        gchar *escapedText = g_markup_escape_text(text.c_str(), -1);
        gchar *markup = g_strdup_printf("<span font_desc=\"Monospace Bold 36\">%s</span>", escapedText);
        gtk_label_set_markup(GTK_LABEL(timeLabel), markup);
        g_free(markup);
        g_free(escapedText);
    }

    void appendPauseLog(long long elapsedSeconds)
    {
        std::ofstream logFile(pauseLogFilePath, std::ios::app);
        if (!logFile.is_open())
        {
            return;
        }

        std::time_t now = std::time(nullptr);
        std::tm *localTime = std::localtime(&now);
        char timestamp[32] = {0};
        if (localTime != nullptr)
        {
            std::strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", localTime);
        }
        else
        {
            std::snprintf(timestamp, sizeof(timestamp), "unknown-time");
        }

        logFile << timestamp << " elapsed=" << formatElapsed(elapsedSeconds) << " (" << elapsedSeconds
                << "s)" << std::endl;
    }

    void setStartPauseButtonLabel()
    {
        gtk_button_set_label(GTK_BUTTON(startPauseBtn), isRunning ? "Pause" : "Start");
    }

    void stopTickingIfNeeded()
    {
        if (timerSourceId != 0)
        {
            g_source_remove(timerSourceId);
            timerSourceId = 0;
        }
    }

    void pauseTimerAndSave(bool writePauseLog, bool removeTickSource = true);

    gboolean onTick(gpointer)
    {
        unsigned long idleMilliseconds = 0;
        bool hasDesktopIdle = getDesktopIdleMilliseconds(idleMilliseconds);
        // if (hasDesktopIdle)
        // {
        //     std::cout << "X server idle time: " << idleMilliseconds << " ms ("
        //               << idleMilliseconds / 1000 << " s)" << std::endl;
        // }
        // else
        // {
        //     std::cout << "X server idle time: unavailable" << std::endl;
        // }

        if (isRunning && hasDesktopIdle && idleMilliseconds >= DESKTOP_IDLE_PAUSE_MS)
        {
            pauseTimerAndSave(true, false);
            timerSourceId = 0;
            return FALSE;
        }

        long long elapsed = currentDisplayedElapsedSeconds();
        if (isRunning && (elapsed - lastAutoSavedElapsedSeconds) >= 30)
        {
            saveElapsedSeconds(elapsed);
            lastAutoSavedElapsedSeconds = elapsed;
        }

        gchar *escapedText = g_markup_escape_text(formatElapsed(elapsed).c_str(), -1);
        gchar *markup = g_strdup_printf("<span font_desc=\"Monospace Bold 36\">%s</span>", escapedText);
        gtk_label_set_markup(GTK_LABEL(timeLabel), markup);
        g_free(markup);
        g_free(escapedText);
        return TRUE;
    }

    void pauseTimerAndSave(bool writePauseLog, bool removeTickSource)
    {
        if (!isRunning)
        {
            return;
        }

        auto now = std::chrono::steady_clock::now();
        auto segment = std::chrono::duration_cast<std::chrono::seconds>(now - runStartTime).count();
        totalElapsedSeconds += segment;
        isRunning = false;

        if (removeTickSource)
        {
            stopTickingIfNeeded();
        }
        saveElapsedSeconds(totalElapsedSeconds);
        if (writePauseLog)
        {
            appendPauseLog(totalElapsedSeconds);
        }
        lastAutoSavedElapsedSeconds = totalElapsedSeconds;
        setStartPauseButtonLabel();
        refreshElapsedLabel();
    }

    void onStartPauseClicked(GtkWidget *, gpointer)
    {
        if (!isRunning)
        {
            runStartTime = std::chrono::steady_clock::now();
            isRunning = true;
            setStartPauseButtonLabel();
            refreshElapsedLabel();
            timerSourceId = g_timeout_add(1000, onTick, nullptr);
            return;
        }

        pauseTimerAndSave(true);
    }

    void onStopClicked(GtkWidget *, gpointer)
    {
        if (isRunning)
        {
            pauseTimerAndSave(false);
        }

        totalElapsedSeconds = 0;
        saveElapsedSeconds(totalElapsedSeconds);
        lastAutoSavedElapsedSeconds = totalElapsedSeconds;
        setStartPauseButtonLabel();
        refreshElapsedLabel();
    }

    void onWindowDestroy(GtkWidget *, gpointer)
    {
        if (isRunning)
        {
            pauseTimerAndSave(false);
            appendPauseLog(totalElapsedSeconds);
        }
        else
        {
            saveElapsedSeconds(totalElapsedSeconds);
            appendPauseLog(totalElapsedSeconds);
        }

        gtk_main_quit();
    }

    void moveWindowToBottomRight(GtkWidget *widget, gpointer)
    {
        GdkDisplay *display = gtk_widget_get_display(widget);
        if (display == nullptr)
        {
            return;
        }

        GdkMonitor *monitor = nullptr;
        const int monitorCount = gdk_display_get_n_monitors(display);

        // Prefer the second monitor when available.
        if (monitorCount >= 2)
        {
            monitor = gdk_display_get_monitor(display, 1);
        }

        if (monitor == nullptr)
        {
            monitor = gdk_display_get_primary_monitor(display);
        }

        if (monitor == nullptr && monitorCount > 0)
        {
            monitor = gdk_display_get_monitor(display, 0);
        }

        if (monitor == nullptr)
        {
            return;
        }

        GdkRectangle workArea;
        gdk_monitor_get_workarea(monitor, &workArea);

        gint x = workArea.x + workArea.width - APP_MAIN_WINDOW_WIDTH;
        gint y = workArea.y + workArea.height - APP_MAIN_WINDOW_HEIGHT;
        gtk_window_move(GTK_WINDOW(widget), x, y);
    }

    GtkWidget *createMainWindowView()
    {
        if (mainWindow == nullptr)
        {
            mainWindow = gtk_window_new(GTK_WINDOW_TOPLEVEL);
            gtk_window_set_title(GTK_WINDOW(mainWindow), "Elapsed Time");
            gtk_window_set_default_size(GTK_WINDOW(mainWindow), APP_MAIN_WINDOW_WIDTH, APP_MAIN_WINDOW_HEIGHT);
            gtk_window_set_keep_above(GTK_WINDOW(mainWindow), TRUE);
            gtk_window_set_resizable(GTK_WINDOW(mainWindow), FALSE);

            GdkGeometry hints;
            hints.min_width = APP_MAIN_WINDOW_WIDTH;
            hints.min_height = APP_MAIN_WINDOW_HEIGHT;
            hints.max_width = APP_MAIN_WINDOW_WIDTH;
            hints.max_height = APP_MAIN_WINDOW_HEIGHT;
            gtk_window_set_geometry_hints(
                GTK_WINDOW(mainWindow),
                NULL,
                &hints,
                static_cast<GdkWindowHints>(
                    GDK_HINT_MIN_SIZE | GDK_HINT_MAX_SIZE));
            g_signal_connect(mainWindow, "destroy", G_CALLBACK(onWindowDestroy), nullptr);
            g_signal_connect(mainWindow, "realize", G_CALLBACK(moveWindowToBottomRight), nullptr);
        }

        GtkWidget *windowContainer = gtk_box_new(GTK_ORIENTATION_VERTICAL, 14);
        gtk_container_set_border_width(GTK_CONTAINER(windowContainer), 16);
        gtk_container_add(GTK_CONTAINER(mainWindow), windowContainer);

        timeLabel = gtk_label_new("00:00:00");
        gtk_box_pack_start(GTK_BOX(windowContainer), timeLabel, FALSE, FALSE, 0);

        GtkWidget *buttonRow = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
        gtk_box_pack_start(GTK_BOX(windowContainer), buttonRow, FALSE, FALSE, 0);

        startPauseBtn = gtk_button_new_with_label("Start");
        gtk_box_pack_start(GTK_BOX(buttonRow), startPauseBtn, TRUE, TRUE, 0);
        g_signal_connect(startPauseBtn, "clicked", G_CALLBACK(onStartPauseClicked), nullptr);

        stopBtn = gtk_button_new_with_label("Stop");
        gtk_box_pack_start(GTK_BOX(buttonRow), stopBtn, TRUE, TRUE, 0);
        g_signal_connect(stopBtn, "clicked", G_CALLBACK(onStopClicked), nullptr);

        totalElapsedSeconds = loadSavedElapsedSeconds();
        lastAutoSavedElapsedSeconds = totalElapsedSeconds;

        // Auto-start the timer when the window opens.
        runStartTime = std::chrono::steady_clock::now();

        long long elapsed = currentDisplayedElapsedSeconds();
        appendPauseLog(elapsed);
        isRunning = true;

        refreshElapsedLabel();
        setStartPauseButtonLabel();
        if (timerSourceId == 0)
        {
            timerSourceId = g_timeout_add(1000, onTick, nullptr);
        }

        return mainWindow;
    }

    void openMainWindow()
    {
        gtk_widget_show_all(createMainWindowView());
    }
}
