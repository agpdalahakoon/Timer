'use strict';

const ByteArray = imports.byteArray;
const { Clutter, GLib, GObject, St } = imports.gi;
const Main = imports.ui.main;
const PanelMenu = imports.ui.panelMenu;
const PopupMenu = imports.ui.popupMenu;

const STATUS_FILE = '/home/praveen/soft/Timer/timer_status.ini';
const TIMER_BINARY = '/home/praveen/soft/Timer/timer';
const UUID = 'timer-panel@praveen.local';

let indicator = null;

function pad2(value) {
    return value.toString().padStart(2, '0');
}

function formatElapsed(seconds) {
    seconds = Math.max(0, Number(seconds) || 0);
    const hours = Math.floor(seconds / 3600);
    const minutes = Math.floor((seconds % 3600) / 60);
    const remainingSeconds = seconds % 60;
    return `${pad2(hours)}:${pad2(minutes)}:${pad2(remainingSeconds)}`;
}

function formatPause(seconds) {
    seconds = Math.max(0, Number(seconds) || 0);
    const minutes = Math.floor(seconds / 60);
    const remainingSeconds = seconds % 60;
    return `${pad2(minutes)}:${pad2(remainingSeconds)}`;
}

function loadStatus() {
    try {
        const [, contents] = GLib.file_get_contents(STATUS_FILE);
        const text = ByteArray.toString(contents);
        const status = {};

        for (const line of text.split('\n')) {
            const separator = line.indexOf('=');
            if (separator <= 0)
                continue;

            const key = line.slice(0, separator).trim();
            const value = line.slice(separator + 1).trim();
            status[key] = value;
        }

        return status;
    } catch (error) {
        return null;
    }
}

function spawnCommand(commandLine) {
    try {
        GLib.spawn_command_line_async(commandLine);
        return true;
    } catch (error) {
        log(`Timer Panel: failed to run "${commandLine}": ${error.message}`);
        return false;
    }
}

const TimerPanelIndicator = GObject.registerClass(
class TimerPanelIndicator extends PanelMenu.Button {
    _init() {
        super._init(0.0, 'Timer Panel');

        this._label = new St.Label({
            text: '--:--:--  P --:--',
            style_class: 'timer-panel-label',
            y_align: Clutter.ActorAlign.CENTER,
        });
        this.add_child(this._label);

        this._toggleItem = new PopupMenu.PopupMenuItem('Start');
        this._stopItem = new PopupMenu.PopupMenuItem('Stop');
        this._showItem = new PopupMenu.PopupMenuItem('Show Timer Window');
        this._settingsItem = new PopupMenu.PopupMenuItem('Settings');
        this._exitItem = new PopupMenu.PopupMenuItem('Exit');

        this._toggleItem.connect('activate', () => this._sendCommand('toggle'));
        this._stopItem.connect('activate', () => this._sendCommand('stop'));
        this._showItem.connect('activate', () => this._sendCommand('show'));
        this._settingsItem.connect('activate', () => this._sendCommand('settings'));
        this._exitItem.connect('activate', () => this._sendCommand('exit'));

        this.menu.addMenuItem(this._toggleItem);
        this.menu.addMenuItem(this._stopItem);
        this.menu.addMenuItem(new PopupMenu.PopupSeparatorMenuItem());
        this.menu.addMenuItem(this._showItem);
        this.menu.addMenuItem(this._settingsItem);
        this.menu.addMenuItem(new PopupMenu.PopupSeparatorMenuItem());
        this.menu.addMenuItem(this._exitItem);

        this._startBackend();
        this._refresh();
        this._timeoutId = GLib.timeout_add_seconds(GLib.PRIORITY_DEFAULT, 1, () => {
            this._refresh();
            return GLib.SOURCE_CONTINUE;
        });
    }

    _startBackend() {
        spawnCommand(`${TIMER_BINARY} --hidden`);
    }

    _sendCommand(command) {
        this._startBackend();
        spawnCommand(`${TIMER_BINARY} --command ${command}`);
        GLib.timeout_add(GLib.PRIORITY_DEFAULT, 250, () => {
            this._refresh();
            return GLib.SOURCE_REMOVE;
        });
    }

    _refresh() {
        const status = loadStatus();
        if (status === null) {
            this._label.set_text('--:--:--  P --:--');
            this._toggleItem.label.set_text('Start');
            return;
        }

        const elapsedText = formatElapsed(status.elapsed_seconds);
        const running = status.running === '1';
        this._toggleItem.label.set_text(running ? 'Pause' : 'Start');

        if (status.exited === '1') {
            this._label.set_text(`${elapsedText}  Exited`);
            return;
        }

        if (!running) {
            this._label.set_text(`${elapsedText}  Paused`);
            return;
        }

        if (status.idle_available !== '1' || Number(status.pause_remaining_seconds) < 0) {
            this._label.set_text(`${elapsedText}  P --:--`);
            return;
        }

        this._label.set_text(`${elapsedText}  P ${formatPause(status.pause_remaining_seconds)}`);
    }

    destroy() {
        if (this._timeoutId) {
            GLib.source_remove(this._timeoutId);
            this._timeoutId = 0;
        }

        super.destroy();
    }
});

function init() {
}

function enable() {
    indicator = new TimerPanelIndicator();
    Main.panel.addToStatusArea(UUID, indicator, 0, 'right');
}

function disable() {
    if (indicator !== null) {
        indicator.destroy();
        indicator = null;
    }
}
