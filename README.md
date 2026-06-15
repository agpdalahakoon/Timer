# Timer App (GTK3 + C++)

This is a simple elapsed-time timer built with GTK3 and C++.

It also includes a GNOME Shell 42 extension that adds a separate right-side top-bar timer. GNOME's built-in clock remains unchanged. The panel timer shows elapsed time plus the inactivity auto-pause countdown:

```
HH:MM:SS  P MM:SS
```

## Requirements

A linux machine with `gcc g++ build-essential make cmake libgtk-3-dev libx11-dev libxss-dev` installed.

The top-bar extension targets GNOME Shell 42 on X11.

## Compiling

```
mkdir build
cd build
cmake ../
make
```

The generated binary name is `timer`.

## Installing the Top-Bar Timer

Run:

```
./install.sh
```

The installer builds the app, copies the backend binary to `/home/praveen/soft/Timer/timer`, installs the GNOME extension as `timer-panel@praveen.local`, and tries to enable it with `gnome-extensions`.

If the panel timer is not visible after installation on X11, press `Alt+F2`, type `r`, and press Enter to reload GNOME Shell.

## Runtime Commands

The GNOME extension uses these commands:

```
/home/praveen/soft/Timer/timer --hidden
/home/praveen/soft/Timer/timer --command toggle
/home/praveen/soft/Timer/timer --command stop
/home/praveen/soft/Timer/timer --command show
/home/praveen/soft/Timer/timer --command settings
/home/praveen/soft/Timer/timer --command exit
```

Timer state for the panel is written to `/home/praveen/soft/Timer/timer_status.ini`.
