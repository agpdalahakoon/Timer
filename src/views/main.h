#include <gtk/gtk.h>
#include <gdk/gdkx.h>
#include <glib/gstdio.h>
#include <X11/extensions/scrnsaver.h>

#include <chrono>
#include <cstdio>
#include <ctime>
#include <fcntl.h>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <sys/file.h>
#include <unistd.h>

#define APP_MAIN_WINDOW_WIDTH 270
#define APP_MAIN_WINDOW_HEIGHT 130

namespace MainWindow
{
    GtkWidget *mainWindow = nullptr;
    GtkWidget *inactiveTimeLabel = nullptr;
    GtkWidget *timeLabel = nullptr;
    GtkWidget *startPauseBtn = nullptr;
    GtkWidget *stopBtn = nullptr;

    bool isRunning = false;
    bool timerInitialized = false;
    guint timerSourceId = 0;
    guint commandSourceId = 0;
    int appLockFd = -1;
    long long totalElapsedSeconds = 0;
    long long lastAutoSavedElapsedSeconds = 0;
    long long idlePauseGraceSeconds = 0;
    std::time_t runStartedEpoch = 0;
    std::chrono::steady_clock::time_point runStartTime;
    std::chrono::steady_clock::time_point ignoreIdleUntilTime;

    constexpr unsigned long DESKTOP_IDLE_PAUSE_MS = 5UL * 60UL * 1000UL;

    const std::string timerDataDir = "/home/praveen/soft/Timer";
    const std::string stateFilePath = "/home/praveen/soft/Timer/timer_state.txt";
    const std::string settingsFilePath = "/home/praveen/soft/Timer/timer_settings.txt";
    const std::string pauseLogFilePath = "/home/praveen/soft/Timer/pause_log.txt";
    const std::string statusFilePath = "/home/praveen/soft/Timer/timer_status.ini";
    const std::string commandFilePath = "/home/praveen/soft/Timer/timer_command.txt";
    const std::string lockFilePath = "/home/praveen/soft/Timer/timer.lock";
    const char *graceRemainingTimerDataKey = "grace-remaining-timer-id";

    void ensureTimerDataDir()
    {
        g_mkdir_with_parents(timerDataDir.c_str(), 0755);
    }

    long long loadSavedElapsedSeconds()
    {
        ensureTimerDataDir();
        std::ifstream stateFile(stateFilePath);
        long long seconds = 0;
        if (stateFile.is_open())
        {
            stateFile >> seconds;
        }
        return seconds >= 0 ? seconds : 0;
    }

    long long loadSavedIdleGraceSeconds()
    {
        ensureTimerDataDir();
        std::ifstream settingsFile(settingsFilePath);
        long long minutes = 0;
        if (settingsFile.is_open())
        {
            settingsFile >> minutes;
        }

        if (minutes < 0)
        {
            minutes = 0;
        }

        return minutes * 60;
    }

    void saveIdleGraceSeconds(long long seconds)
    {
        ensureTimerDataDir();
        std::ofstream settingsFile(settingsFilePath, std::ios::trunc);
        if (settingsFile.is_open())
        {
            settingsFile << seconds / 60;
        }
    }

    void saveElapsedSeconds(long long seconds)
    {
        ensureTimerDataDir();
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

    void resetIdleGraceWindow()
    {
        ignoreIdleUntilTime = std::chrono::steady_clock::now() + std::chrono::seconds(idlePauseGraceSeconds);
    }

    bool isIdleGraceActive()
    {
        return idlePauseGraceSeconds > 0 && std::chrono::steady_clock::now() < ignoreIdleUntilTime;
    }

    std::string formatMinutesSeconds(long long seconds)
    {
        if (seconds < 0)
        {
            seconds = 0;
        }

        long long minutes = seconds / 60;
        long long remainingSeconds = seconds % 60;

        std::ostringstream out;
        out << std::setfill('0') << std::setw(2) << minutes << ":"
            << std::setfill('0') << std::setw(2) << remainingSeconds;
        return out.str();
    }

    std::string graceRemainingText()
    {
        if (idlePauseGraceSeconds <= 0)
        {
            return "Disabled";
        }

        if (!isRunning)
        {
            return "Not running";
        }

        auto now = std::chrono::steady_clock::now();
        auto remaining = std::chrono::duration_cast<std::chrono::seconds>(ignoreIdleUntilTime - now).count();
        return formatMinutesSeconds(remaining);
    }

    void refreshGraceRemainingLabel(GtkWidget *label)
    {
        std::string text = "Grace remaining: " + graceRemainingText();
        gtk_label_set_text(GTK_LABEL(label), text.c_str());
    }

    long long remainingGraceSeconds()
    {
        if (!isRunning || idlePauseGraceSeconds <= 0)
        {
            return 0;
        }

        auto now = std::chrono::steady_clock::now();
        auto remaining = std::chrono::duration_cast<std::chrono::seconds>(ignoreIdleUntilTime - now).count();
        return remaining > 0 ? remaining : 0;
    }

    long long remainingIdlePauseSeconds(unsigned long idleMilliseconds)
    {
        if (idleMilliseconds >= DESKTOP_IDLE_PAUSE_MS)
        {
            return 0;
        }

        unsigned long remainingMilliseconds = DESKTOP_IDLE_PAUSE_MS - idleMilliseconds;
        return static_cast<long long>((remainingMilliseconds + 999) / 1000);
    }

    long long visiblePauseCountdownSeconds(bool hasDesktopIdle, unsigned long idleMilliseconds)
    {
        if (!isRunning || !hasDesktopIdle)
        {
            return -1;
        }

        long long graceSeconds = remainingGraceSeconds();
        long long pauseSeconds = remainingIdlePauseSeconds(idleMilliseconds);
        return graceSeconds > pauseSeconds ? graceSeconds : pauseSeconds;
    }

    void writeStatusFile(bool hasDesktopIdle, unsigned long idleMilliseconds)
    {
        ensureTimerDataDir();
        long long elapsed = currentDisplayedElapsedSeconds();
        long long pauseRemaining = visiblePauseCountdownSeconds(hasDesktopIdle, idleMilliseconds);

        std::ofstream statusFile(statusFilePath, std::ios::trunc);
        if (!statusFile.is_open())
        {
            return;
        }

        statusFile << "running=" << (isRunning ? 1 : 0) << "\n";
        statusFile << "elapsed_seconds=" << elapsed << "\n";
        statusFile << "run_started_epoch=" << static_cast<long long>(runStartedEpoch) << "\n";
        statusFile << "idle_available=" << (hasDesktopIdle ? 1 : 0) << "\n";
        statusFile << "pause_remaining_seconds=" << pauseRemaining << "\n";
        statusFile << "idle_grace_seconds=" << idlePauseGraceSeconds << "\n";
        statusFile << "updated_epoch=" << static_cast<long long>(std::time(nullptr)) << "\n";
    }

    void writeStatusFile()
    {
        unsigned long idleMilliseconds = 0;
        bool hasDesktopIdle = getDesktopIdleMilliseconds(idleMilliseconds);
        writeStatusFile(hasDesktopIdle, idleMilliseconds);
    }

    void refreshInactiveTimeLabel(bool hasDesktopIdle, unsigned long idleMilliseconds)
    {
        if (inactiveTimeLabel == nullptr)
        {
            return;
        }

        std::string text;
        if (!isRunning)
        {
            text = "Paused";
        }
        else if (!hasDesktopIdle)
        {
            text = "Idle unavailable";
        }
        else
        {
            text = formatMinutesSeconds(visiblePauseCountdownSeconds(hasDesktopIdle, idleMilliseconds));
        }

        gchar *escapedText = g_markup_escape_text(text.c_str(), -1);
        gchar *markup = g_strdup_printf("<span font_desc=\"Monospace 9\">%s</span>", escapedText);
        gtk_label_set_markup(GTK_LABEL(inactiveTimeLabel), markup);
        g_free(markup);
        g_free(escapedText);
    }

    void refreshInactiveTimeLabel()
    {
        unsigned long idleMilliseconds = 0;
        bool hasDesktopIdle = getDesktopIdleMilliseconds(idleMilliseconds);
        refreshInactiveTimeLabel(hasDesktopIdle, idleMilliseconds);
    }

    void refreshElapsedLabel()
    {
        if (timeLabel == nullptr)
        {
            return;
        }

        std::string text = formatElapsed(currentDisplayedElapsedSeconds());
        gchar *escapedText = g_markup_escape_text(text.c_str(), -1);
        gchar *markup = g_strdup_printf("<span font_desc=\"Monospace Bold 36\">%s</span>", escapedText);
        gtk_label_set_markup(GTK_LABEL(timeLabel), markup);
        g_free(markup);
        g_free(escapedText);
    }

    void appendPauseLog(long long elapsedSeconds)
    {
        ensureTimerDataDir();
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
        if (startPauseBtn == nullptr)
        {
            return;
        }

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
    void startTimer();
    void stopTimer();
    GtkWidget *createMainWindowView();
    void showMainWindow();
    void showSettingsWindow();

    bool writeCommandFile(const std::string &command)
    {
        ensureTimerDataDir();
        std::ofstream commandFile(commandFilePath, std::ios::trunc);
        if (!commandFile.is_open())
        {
            return false;
        }

        commandFile << command << "\n";
        return true;
    }

    bool acquireAppLock(bool showExistingWindow)
    {
        ensureTimerDataDir();
        appLockFd = open(lockFilePath.c_str(), O_CREAT | O_RDWR, 0644);
        if (appLockFd < 0)
        {
            return true;
        }

        if (flock(appLockFd, LOCK_EX | LOCK_NB) != 0)
        {
            close(appLockFd);
            appLockFd = -1;

            if (showExistingWindow)
            {
                writeCommandFile("show");
            }

            return false;
        }

        ftruncate(appLockFd, 0);
        std::string pid = std::to_string(getpid()) + "\n";
        write(appLockFd, pid.c_str(), pid.size());
        return true;
    }

    void handleCommand(const std::string &command)
    {
        if (command == "start")
        {
            if (!isRunning)
            {
                startTimer();
            }
        }
        else if (command == "pause")
        {
            pauseTimerAndSave(true);
        }
        else if (command == "toggle")
        {
            if (isRunning)
            {
                pauseTimerAndSave(true);
            }
            else
            {
                startTimer();
            }
        }
        else if (command == "stop")
        {
            stopTimer();
        }
        else if (command == "show")
        {
            showMainWindow();
        }
        else if (command == "settings")
        {
            showSettingsWindow();
        }
    }

    gboolean onCommandPoll(gpointer)
    {
        std::ifstream commandFile(commandFilePath);
        if (!commandFile.is_open())
        {
            return TRUE;
        }

        std::string command;
        commandFile >> command;
        commandFile.close();
        std::remove(commandFilePath.c_str());

        if (!command.empty())
        {
            handleCommand(command);
            writeStatusFile();
        }

        return TRUE;
    }

    gboolean onTick(gpointer)
    {
        unsigned long idleMilliseconds = 0;
        bool hasDesktopIdle = getDesktopIdleMilliseconds(idleMilliseconds);
        refreshInactiveTimeLabel(hasDesktopIdle, idleMilliseconds);
        // if (hasDesktopIdle)
        // {
        //     std::cout << "X server idle time: " << idleMilliseconds << " ms ("
        //               << idleMilliseconds / 1000 << " s)" << std::endl;
        // }
        // else
        // {
        //     std::cout << "X server idle time: unavailable" << std::endl;
        // }

        if (isRunning && hasDesktopIdle && !isIdleGraceActive() && idleMilliseconds >= DESKTOP_IDLE_PAUSE_MS)
        {
            pauseTimerAndSave(true, false);
            timerSourceId = 0;
            writeStatusFile();
            return FALSE;
        }

        long long elapsed = currentDisplayedElapsedSeconds();
        if (isRunning && (elapsed - lastAutoSavedElapsedSeconds) >= 30)
        {
            saveElapsedSeconds(elapsed);
            lastAutoSavedElapsedSeconds = elapsed;
        }

        refreshElapsedLabel();
        writeStatusFile(hasDesktopIdle, idleMilliseconds);
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
        refreshInactiveTimeLabel();
        writeStatusFile();
    }

    void startTimer()
    {
        if (isRunning)
        {
            return;
        }

        runStartTime = std::chrono::steady_clock::now();
        runStartedEpoch = std::time(nullptr);
        resetIdleGraceWindow();
        isRunning = true;
        setStartPauseButtonLabel();
        refreshElapsedLabel();
        refreshInactiveTimeLabel();
        writeStatusFile();

        if (timerSourceId == 0)
        {
            timerSourceId = g_timeout_add(1000, onTick, nullptr);
        }
    }

    void stopTimer()
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
        refreshInactiveTimeLabel();
        writeStatusFile();
    }

    void onStartPauseClicked(GtkWidget *, gpointer)
    {
        if (!isRunning)
        {
            startTimer();
            return;
        }

        pauseTimerAndSave(true);
    }

    void onStopClicked(GtkWidget *, gpointer)
    {
        stopTimer();
    }

    gboolean onGraceRemainingTick(gpointer userData)
    {
        refreshGraceRemainingLabel(GTK_WIDGET(userData));
        return TRUE;
    }

    void onIdleGraceMinutesChanged(GtkSpinButton *spinButton, gpointer userData)
    {
        gint minutes = gtk_spin_button_get_value_as_int(spinButton);
        idlePauseGraceSeconds = static_cast<long long>(minutes) * 60;
        saveIdleGraceSeconds(idlePauseGraceSeconds);
        if (isRunning)
        {
            resetIdleGraceWindow();
        }
        refreshInactiveTimeLabel();

        if (userData != nullptr)
        {
            refreshGraceRemainingLabel(GTK_WIDGET(userData));
        }
    }

    void onSettingsDialogResponse(GtkDialog *dialog, gint, gpointer)
    {
        gtk_widget_destroy(GTK_WIDGET(dialog));
    }

    void onSettingsDialogDestroy(GtkWidget *dialog, gpointer)
    {
        guint sourceId = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(dialog), graceRemainingTimerDataKey));
        if (sourceId != 0)
        {
            g_source_remove(sourceId);
            g_object_set_data(G_OBJECT(dialog), graceRemainingTimerDataKey, GUINT_TO_POINTER(0));
        }
    }

    void openSettingsDialog()
    {
        GtkWidget *dialog = gtk_dialog_new_with_buttons(
            "Timer Settings",
            GTK_WINDOW(mainWindow),
            static_cast<GtkDialogFlags>(GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT),
            "_Close",
            GTK_RESPONSE_CLOSE,
            nullptr);

        GtkWidget *contentArea = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
        GtkWidget *settingsBox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
        gtk_container_set_border_width(GTK_CONTAINER(settingsBox), 16);
        gtk_container_add(GTK_CONTAINER(contentArea), settingsBox);

        GtkWidget *titleLabel = gtk_label_new("Ignore inactivity after start/resume");
        gtk_widget_set_halign(titleLabel, GTK_ALIGN_START);
        gtk_box_pack_start(GTK_BOX(settingsBox), titleLabel, FALSE, FALSE, 0);

        GtkWidget *inputRow = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
        gtk_box_pack_start(GTK_BOX(settingsBox), inputRow, FALSE, FALSE, 0);

        GtkAdjustment *adjustment = gtk_adjustment_new(idlePauseGraceSeconds / 60, 0, 240, 1, 5, 0);
        GtkWidget *minutesSpin = gtk_spin_button_new(adjustment, 1, 0);
        gtk_spin_button_set_numeric(GTK_SPIN_BUTTON(minutesSpin), TRUE);
        gtk_box_pack_start(GTK_BOX(inputRow), minutesSpin, FALSE, FALSE, 0);

        GtkWidget *minutesLabel = gtk_label_new("minutes");
        gtk_box_pack_start(GTK_BOX(inputRow), minutesLabel, FALSE, FALSE, 0);

        GtkWidget *graceRemainingLabel = gtk_label_new(nullptr);
        gtk_widget_set_halign(graceRemainingLabel, GTK_ALIGN_START);
        refreshGraceRemainingLabel(graceRemainingLabel);
        gtk_box_pack_start(GTK_BOX(settingsBox), graceRemainingLabel, FALSE, FALSE, 0);

        guint graceRemainingTimerId = g_timeout_add(1000, onGraceRemainingTick, graceRemainingLabel);
        g_object_set_data(G_OBJECT(dialog), graceRemainingTimerDataKey, GUINT_TO_POINTER(graceRemainingTimerId));

        g_signal_connect(minutesSpin, "value-changed", G_CALLBACK(onIdleGraceMinutesChanged), graceRemainingLabel);
        g_signal_connect(dialog, "response", G_CALLBACK(onSettingsDialogResponse), nullptr);
        g_signal_connect(dialog, "destroy", G_CALLBACK(onSettingsDialogDestroy), nullptr);
        gtk_widget_show_all(dialog);
    }

    gboolean onTimeLabelClicked(GtkWidget *, GdkEventButton *event, gpointer)
    {
        if (event->type == GDK_BUTTON_PRESS && event->button == 1)
        {
            openSettingsDialog();
            return TRUE;
        }

        return FALSE;
    }

    void showMainWindow()
    {
        GtkWidget *window = createMainWindowView();
        gtk_widget_show_all(window);
        gtk_window_present(GTK_WINDOW(window));
    }

    void showSettingsWindow()
    {
        showMainWindow();
        openSettingsDialog();
    }

    gboolean onWindowDelete(GtkWidget *widget, GdkEvent *, gpointer)
    {
        gtk_widget_hide(widget);
        return TRUE;
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

        if (commandSourceId != 0)
        {
            g_source_remove(commandSourceId);
            commandSourceId = 0;
        }

        writeStatusFile();
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

    void initializeTimerBackend()
    {
        if (timerInitialized)
        {
            return;
        }

        totalElapsedSeconds = loadSavedElapsedSeconds();
        idlePauseGraceSeconds = loadSavedIdleGraceSeconds();
        lastAutoSavedElapsedSeconds = totalElapsedSeconds;

        // Keep the previous behavior: opening the app starts/resumes the timer.
        startTimer();
        appendPauseLog(currentDisplayedElapsedSeconds());

        if (commandSourceId == 0)
        {
            commandSourceId = g_timeout_add(500, onCommandPoll, nullptr);
        }

        timerInitialized = true;
        writeStatusFile();
    }

    GtkWidget *createMainWindowView()
    {
        initializeTimerBackend();

        if (mainWindow != nullptr)
        {
            return mainWindow;
        }

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
            g_signal_connect(mainWindow, "delete-event", G_CALLBACK(onWindowDelete), nullptr);
            g_signal_connect(mainWindow, "destroy", G_CALLBACK(onWindowDestroy), nullptr);
            g_signal_connect(mainWindow, "realize", G_CALLBACK(moveWindowToBottomRight), nullptr);
        }

        GtkWidget *overlay = gtk_overlay_new();
        gtk_container_add(GTK_CONTAINER(mainWindow), overlay);

        GtkWidget *windowContainer = gtk_box_new(GTK_ORIENTATION_VERTICAL, 14);
        gtk_container_set_border_width(GTK_CONTAINER(windowContainer), 16);
        gtk_container_add(GTK_CONTAINER(overlay), windowContainer);

        inactiveTimeLabel = gtk_label_new("Paused");
        gtk_widget_set_halign(inactiveTimeLabel, GTK_ALIGN_END);
        gtk_widget_set_valign(inactiveTimeLabel, GTK_ALIGN_START);
        gtk_widget_set_margin_top(inactiveTimeLabel, 2);
        gtk_widget_set_margin_end(inactiveTimeLabel, 8);
        gtk_overlay_add_overlay(GTK_OVERLAY(overlay), inactiveTimeLabel);

        GtkWidget *timeLabelEventBox = gtk_event_box_new();
        gtk_widget_set_tooltip_text(timeLabelEventBox, "Timer settings");
        g_signal_connect(timeLabelEventBox, "button-press-event", G_CALLBACK(onTimeLabelClicked), nullptr);
        gtk_box_pack_start(GTK_BOX(windowContainer), timeLabelEventBox, FALSE, FALSE, 0);

        timeLabel = gtk_label_new("00:00:00");
        gtk_container_add(GTK_CONTAINER(timeLabelEventBox), timeLabel);

        GtkWidget *buttonRow = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
        gtk_box_pack_start(GTK_BOX(windowContainer), buttonRow, FALSE, FALSE, 0);

        startPauseBtn = gtk_button_new_with_label("Start");
        gtk_box_pack_start(GTK_BOX(buttonRow), startPauseBtn, TRUE, TRUE, 0);
        g_signal_connect(startPauseBtn, "clicked", G_CALLBACK(onStartPauseClicked), nullptr);

        stopBtn = gtk_button_new_with_label("Stop");
        gtk_box_pack_start(GTK_BOX(buttonRow), stopBtn, TRUE, TRUE, 0);
        g_signal_connect(stopBtn, "clicked", G_CALLBACK(onStopClicked), nullptr);

        refreshElapsedLabel();
        refreshInactiveTimeLabel();
        setStartPauseButtonLabel();

        return mainWindow;
    }

    void openMainWindow(bool hidden = false)
    {
        initializeTimerBackend();
        if (!hidden)
        {
            showMainWindow();
        }
    }
}
