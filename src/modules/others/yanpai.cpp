#include "yanpai.h"

#if defined(HAS_NS4168_SPKR)

#include "core/display.h"
#include "core/mykeyboard.h"
#include "core/sd_functions.h"
#include "core/utils.h"
#include "modules/others/audio.h"
#include <Arduino.h>
#include <LittleFS.h>
#include <SD.h>

#define YANPAI_DIR "/yanpai"
#define YANPAI_LFS_DIR "/yanpai"

struct YanpaiState {
    bool isPlaying;
    String currentFile;
    unsigned long lastPressTime;
};

struct PlaybackHistory {
    static const int MAX_HISTORY = 10;
    int history[MAX_HISTORY];
    int historySize;
    int historyCount;

    PlaybackHistory() : historySize(0), historyCount(0) {
        for (int i = 0; i < MAX_HISTORY; i++) history[i] = -1;
    }

    void add(int index) {
        for (int i = MAX_HISTORY - 1; i > 0; i--) {
            history[i] = history[i - 1];
        }
        history[0] = index;
        if (historySize < MAX_HISTORY) historySize++;
        historyCount++;
    }

    int getWeight(int index) const {
        for (int i = 0; i < historySize; i++) {
            if (history[i] == index) {
                return 1 << i;
            }
        }
        return 1 << (MAX_HISTORY + 1);
    }
};

struct UILayout {
    int marginX;
    int marginY;
    int headerHeight;
    int buttonSize;
    int fileInfoHeight;
    int textSizeLarge;
    int textSizeSmall;

    void calculate() {
        bool isLarge = (tftWidth > 200 && tftHeight > 200);
        marginX = isLarge ? 20 : 10;
        marginY = isLarge ? 15 : 8;
        headerHeight = isLarge ? 35 : 25;
        buttonSize = isLarge ? 80 : 55;
        fileInfoHeight = isLarge ? 50 : 35;
        textSizeLarge = isLarge ? 2 : 1;
        textSizeSmall = 1;
    }
};

std::vector<String> getAudioFiles(FS *fs, const char *dirPath) {
    std::vector<String> files;

    if (!fs || !fs->exists(dirPath)) { return files; }

    File root = fs->open(dirPath);
    if (!root || !root.isDirectory()) { return files; }

    File file = root.openNextFile();
    while (file) {
        if (!file.isDirectory()) {
            String filename = file.name();
            if (isAudioFile(filename)) { files.push_back(String(dirPath) + "/" + filename); }
        }
        file = root.openNextFile();
    }
    root.close();

    return files;
}

// Draw play icon centered in button
void drawPlayIcon(int x, int y, int size, uint16_t color) {
    int cx = x + size / 2;
    int cy = y + size / 2;
    int r = size / 2 - 8;
    // Triangle play icon
    tft.fillTriangle(cx - r / 2, cy - r, cx - r / 2, cy + r, cx + r, cy, color);
}

// Draw stop icon centered in button
void drawStopIcon(int x, int y, int size, uint16_t color) {
    int padding = 8;
    tft.fillRoundRect(x + padding, y + padding, size - 2 * padding, size - 2 * padding, 4, color);
}

static String extractFilename(const String &filepath) {
    int lastSlash = filepath.lastIndexOf('/');
    int lastDot = filepath.lastIndexOf('.');
    String name = filepath.substring(lastSlash + 1);
    if (lastDot > lastSlash) { name = name.substring(0, lastDot - lastSlash - 1); }
    return name;
}

void drawYanpaiUI(YanpaiState &state, const UILayout &ui) {
    tft.fillScreen(bruceConfig.bgColor);

    // Header with theme color
    tft.fillRect(0, 0, tftWidth, ui.headerHeight, bruceConfig.priColor);
    tft.setTextColor(bruceConfig.bgColor, bruceConfig.priColor);
    tft.setTextSize(ui.textSizeLarge);

    String title = "YANPAI";
    int titleW = title.length() * 6 * ui.textSizeLarge;
    tft.setCursor((tftWidth - titleW) / 2, (ui.headerHeight - 8 * ui.textSizeLarge) / 2);
    tft.print(title);

    // Calculate button position (centered)
    int btnX = (tftWidth - ui.buttonSize) / 2;
    int btnY = ui.headerHeight + ui.marginY + ui.fileInfoHeight + 10;

    // File info area (above button)
    int fileInfoY = ui.headerHeight + ui.marginY;
    tft.fillRect(ui.marginX, fileInfoY, tftWidth - 2 * ui.marginX, ui.fileInfoHeight, bruceConfig.bgColor);
    tft.drawRoundRect(ui.marginX, fileInfoY, tftWidth - 2 * ui.marginX, ui.fileInfoHeight, 5, TFT_DARKGREY);

    if (!state.currentFile.isEmpty()) {
        tft.setTextColor(TFT_WHITE, bruceConfig.bgColor);
        tft.setTextSize(ui.textSizeSmall);
        String displayName = extractFilename(state.currentFile);
        if (displayName.length() > 35) { displayName = displayName.substring(0, 32) + "..."; }

        int maxChars = (tftWidth - 2 * ui.marginX - 20) / 6;
        if (displayName.length() > maxChars) {
            displayName = displayName.substring(0, maxChars - 3) + "...";
        }

        int textW = displayName.length() * 6;
        tft.setCursor((tftWidth - textW) / 2, fileInfoY + (ui.fileInfoHeight - 8) / 2);
        tft.print(displayName);
    } else {
        tft.setTextColor(TFT_DARKGREY, bruceConfig.bgColor);
        tft.setTextSize(ui.textSizeSmall);
        String hint = "Press to play";
        int textW = hint.length() * 6;
        tft.setCursor((tftWidth - textW) / 2, fileInfoY + (ui.fileInfoHeight - 8) / 2);
        tft.print(hint);
    }

    // Main play/stop button
    uint16_t iconColor = TFT_WHITE;

    if (state.isPlaying) {
        tft.fillRoundRect(btnX + 3, btnY + 3, ui.buttonSize, ui.buttonSize, 10, TFT_BLACK);
        tft.fillRoundRect(btnX, btnY, ui.buttonSize, ui.buttonSize, 10, bruceConfig.priColor);
        tft.drawRoundRect(btnX, btnY, ui.buttonSize, ui.buttonSize, 10, TFT_WHITE);
    } else {
        tft.drawRoundRect(btnX, btnY, ui.buttonSize, ui.buttonSize, 10, bruceConfig.priColor);
        tft.drawRoundRect(btnX + 1, btnY + 1, ui.buttonSize - 2, ui.buttonSize - 2, 9, bruceConfig.priColor);
    }

    // Draw icon
    if (state.isPlaying) {
        drawStopIcon(btnX, btnY, ui.buttonSize, iconColor);
    } else {
        drawPlayIcon(btnX, btnY, ui.buttonSize, iconColor);
    }

    // Status text below button
    int statusY = btnY + ui.buttonSize + 15;
    tft.setTextSize(ui.textSizeSmall);
    if (state.isPlaying) {
        tft.setTextColor(bruceConfig.priColor, bruceConfig.bgColor);
        String status = "Playing...";
        int textW = status.length() * 6;
        tft.setCursor((tftWidth - textW) / 2, statusY);
        tft.print(status);
    } else {
        tft.setTextColor(TFT_DARKGREY, bruceConfig.bgColor);
        String status = "Ready";
        int textW = status.length() * 6;
        tft.setCursor((tftWidth - textW) / 2, statusY);
        tft.print(status);
    }

    String escHint = "ESC: Exit";
    int escW = escHint.length() * 6;
    tft.setCursor(tftWidth - ui.marginX - escW, tftHeight - 15);
    tft.print(escHint);
}

int selectWeightedRandom(const std::vector<String> &audioFiles, PlaybackHistory &history) {
    int totalWeight = 0;
    int weights[audioFiles.size()];

    for (size_t i = 0; i < audioFiles.size(); i++) {
        weights[i] = history.getWeight(i);
        totalWeight += weights[i];
    }

    int randomValue = random(totalWeight);
    int cumulativeWeight = 0;

    for (size_t i = 0; i < audioFiles.size(); i++) {
        cumulativeWeight += weights[i];
        if (randomValue < cumulativeWeight) {
            return i;
        }
    }

    return random(audioFiles.size());
}

void playRandomAudio(std::vector<String> &audioFiles, YanpaiState &state, FS *fs, PlaybackHistory &history) {
    if (audioFiles.empty()) {
        displayError("No audio files found", true);
        return;
    }

    if (state.isPlaying) {
        stopAudioPlayback();
        delay(100);
    }

    int randomIndex = selectWeightedRandom(audioFiles, history);
    String selectedFile = audioFiles[randomIndex];
    history.add(randomIndex);

    bool success = false;
    for (int retry = 0; retry < 3 && !success; retry++) {
        if (retry > 0) delay(50);
        success = playAudioFile(fs, selectedFile, PLAYBACK_ASYNC);
    }

    if (!success) {
        success = playAudioFile(fs, selectedFile, PLAYBACK_BLOCKING);
    }

    if (success) {
        state.isPlaying = true;
        state.currentFile = selectedFile;
    } else {
        state.isPlaying = false;
        state.currentFile = "";
        displayError("Playback failed", true);
    }
}

void yanpaiSetup(void) {
    randomSeed(millis());

    std::vector<String> audioFiles;
    FS *fs = nullptr;

    if (sdcardMounted && SD.exists(YANPAI_DIR)) {
        fs = &SD;
        audioFiles = getAudioFiles(fs, YANPAI_DIR);
    }

    if (audioFiles.empty() && LittleFS.exists(YANPAI_LFS_DIR)) {
        fs = &LittleFS;
        audioFiles = getAudioFiles(fs, YANPAI_LFS_DIR);
    }

    if (audioFiles.empty()) {
        displayError("Place audio files in " YANPAI_DIR " folder", true);
        return;
    }

    UILayout ui;
    ui.calculate();

    // Calculate button position for touch detection
    int btnX = (tftWidth - ui.buttonSize) / 2;
    int btnY = ui.headerHeight + ui.marginY + ui.fileInfoHeight + 10;

    YanpaiState state;
    state.isPlaying = false;
    state.currentFile = "";
    state.lastPressTime = 0;

    PlaybackHistory history;

    drawYanpaiUI(state, ui);

    while (true) {
        InputHandler();
        wakeUpScreen();

        if (check(EscPress)) {
            if (state.isPlaying) { stopAudioPlayback(); }
            break;
        }

        bool buttonPressed = false;

        if (check(SelPress)) { buttonPressed = true; }

        if (touchPoint.pressed) {
            if (touchPoint.x >= btnX && touchPoint.x <= btnX + ui.buttonSize &&
                touchPoint.y >= btnY && touchPoint.y <= btnY + ui.buttonSize) {
                buttonPressed = true;
                touchPoint.Clear();
            }
        }

        if (buttonPressed) {
            if (millis() - state.lastPressTime > 300) {
                state.lastPressTime = millis();
                playRandomAudio(audioFiles, state, fs, history);
                drawYanpaiUI(state, ui);
            }
        }

        if (state.isPlaying) {
            AudioPlaybackInfo info = getAudioPlaybackInfo();
            if (info.state == PLAYBACK_IDLE || info.state == PLAYBACK_STOPPING) {
                state.isPlaying = false;
                state.currentFile = "";
                drawYanpaiUI(state, ui);
            }
        }

        delay(30);
    }

    tft.fillScreen(bruceConfig.bgColor);
}

#else

void yanpaiSetup(void) { displayError("Audio not supported on this device", true); }

#endif // HAS_NS4168_SPKR
