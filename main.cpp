// dxball_normal_ball_updated_ui.cpp
#include <GL/glut.h>
#include <cmath>
#include <vector>
#include <string>
#include <cstdlib>
#include <algorithm>
#include <windows.h>
#include <mmsystem.h>
#include <ctime>
#include <fstream>   // for checking file existence
#include <iostream>

#pragma comment(lib, "winmm.lib")

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

struct Color { float r,g,b,a; };
struct Block {
    float x,y,w,h;
    bool alive;
    Color color;
};

enum class GameState { MENU, PLAYING, GAME_OVER, WIN };
enum class MenuScreen { MAIN, PLAYER_NAME, SCORE_BOARD };

static const int WIN_W = 900;
static const int WIN_H = 700;

static GameState gState = GameState::MENU;
static MenuScreen currentScreen = MenuScreen::MAIN;
static int menuSelection = 0;

// Player info
static int currentPlayer = 0;
static std::string playerNames[3] = {"Player1", "Player2", "Player3"};
static std::string playerName = playerNames[currentPlayer];
static bool nameInputMode = false;
static std::string tempName = "";

// Per-player best scores
static int playerScores[3] = {0, 0, 0};

// Global scoreboard: stores all finished runs (name, score)
static std::vector<std::pair<std::string,int>> scoreboard;

// Flag to ensure a round's score is recorded only once
static bool scoreRecordedThisRound = false;

// Paddle
static float padW = 120, padH = 20;
static float padX = (WIN_W - padW)/2.0f;
static float padY = 60.0f;
static float padSpeed = 15.0f;

// Ball (normal) - changed color later in draw
static float ballX = WIN_W/2.0f;
static float ballY = padY + padH + 18.0f;
static float ballVX = 8.0f;
static float ballVY = 10.0f;
static float ballSize = 10.0f;

// Gameplay
static int lives = 3;
static int score = 0;
static bool ballStuckToPaddle = true;
static std::vector<Block> blocks;

// UI pulse for menu selection
static float menuPulse = 0.0f;

// ------------------- SOUND (UPDATED) -------------------
// Behavior:
// - Prefer external cartoon .wav files if present in exe directory:
//     cartoon_hit.wav, cartoon_paddle.wav, cartoon_lose.wav, cartoon_win.wav, cartoon_menu.wav
// - If files not present, fallback to Windows system aliases defined below.
// - Press 'M' to toggle sound ON/OFF. Sound state shown in HUD/footer.
// - If sound is disabled, no PlaySound calls are made.

static bool soundEnabled = true;

// External filenames (place your .wav files next to exe to use them)
static const char* CART_HIT_FILE    = "cartoon_hit.wav";
static const char* CART_PADDLE_FILE = "cartoon_paddle.wav";
static const char* CART_LOSE_FILE   = "cartoon_lose.wav";
static const char* CART_WIN_FILE    = "cartoon_win.wav";
static const char* CART_MENU_FILE   = "cartoon_menu.wav";

// Fallback aliases (Windows)
static const char* SND_HIT_BRICK   = "SystemAsterisk";
static const char* SND_PADDLE_HIT  = "SystemExclamation";
static const char* SND_LOSE_LIFE   = "SystemHand";
static const char* SND_GAME_WIN    = "SystemExit";
static const char* SND_MENU_NAV    = "SystemStart";

inline bool fileExists(const char* path) {
    std::ifstream f(path);
    return f.good();
}

// Play a cartoon file if available, otherwise fallback to alias.
// If both unavailable or sound disabled, do nothing.
inline void playSfxFileOrAlias(const char* filePath, const char* alias) {
    if(!soundEnabled) return;
    if(filePath && fileExists(filePath)) {
        PlaySoundA(filePath, NULL, SND_ASYNC | SND_FILENAME);
    } else if(alias) {
        PlaySoundA(alias, NULL, SND_ASYNC | SND_ALIAS);
    }
}
// --------------------------------------------

void drawText(float x, float y, const std::string& s, void* font = GLUT_BITMAP_HELVETICA_18, Color c = {1,1,1,1}) {
    glColor4f(c.r, c.g, c.b, c.a);
    glRasterPos2f(x, y);
    for(char ch : s) glutBitmapCharacter(font, ch);
}

// Update best score for currentPlayer (keeps per-player best)
void saveBestForCurrentPlayer() {
    if(currentPlayer >= 0 && currentPlayer < 3) {
        playerScores[currentPlayer] = std::max(playerScores[currentPlayer], score);
    }
}

// Record this finished round (name,score) into the global scoreboard once per round
void recordScoreboardEntryIfNeeded() {
    if(scoreRecordedThisRound) return;
    if(playerName.empty()) return;
    scoreboard.push_back({playerName, score});
    scoreRecordedThisRound = true;
}

// Reset position + velocities and attach ball to paddle
void resetBallOnPaddle() {
    ballX = padX + padW/2.0f;
    ballY = padY + padH + 18.0f;
    ballVX = 8.0f * ((std::rand()%2)?1:-1);
    ballVY = 10.0f;
    ballStuckToPaddle = true;
}

// Reset a level / start a new round
void resetLevel() {
    blocks.clear();
    int rows = 4, cols = 8;
    float marginX = 80, marginY = 100, gapX = 10, gapY = 8;
    float bw = (WIN_W - 2*marginX - (cols-1)*gapX) / cols;
    float bh = 35.0f;

    for(int r=0;r<rows;r++){
        for(int c=0;c<cols;c++){
            Block b;
            b.x = marginX + c*(bw+gapX);
            b.y = WIN_H - marginY - (r+1)*(bh+gapY);
            b.w = bw; b.h = bh;
            b.alive = true;
            float fr = 0.15f + 0.7f * (float(c) / float(std::max(1, cols-1)));
            float fg = 0.15f + 0.6f * (float((r + c) % cols) / float(std::max(1, cols-1)));
            float fb = 0.35f + 0.5f * (float(r) / float(std::max(1, rows-1)));
            b.color = {fr, fg, fb, 1.0f};
            blocks.push_back(b);
        }
    }
    score = 0;
    lives = 3;
    resetBallOnPaddle();
    scoreRecordedThisRound = false;
}

void nextPlayer() {
    currentPlayer = (currentPlayer + 1) % 3;
    playerName = playerNames[currentPlayer];
    padSpeed += 2.0f;
    resetLevel();
}

// Draw a simple filled circle (normal ball) - changed to bluish ball with subtle gloss
void drawBall(float cx, float cy, float r) {
    const int segments = 40;
    // glossy gradient: center lighter, rim darker
    glBegin(GL_TRIANGLE_FAN);
    glColor4f(0.6f, 0.85f, 1.0f, 1.0f); // center: light blue
    glVertex2f(cx, cy);
    for(int i=0;i<=segments;i++){
        float theta = 2.0f * M_PI * float(i) / float(segments);
        float x = cx + cosf(theta) * r;
        float y = cy + sinf(theta) * r;
        glColor4f(0.15f, 0.5f, 0.9f, 1.0f);
        glVertex2f(x,y);
    }
    glEnd();

    // subtle bright highlight
    glBegin(GL_TRIANGLE_FAN);
    glColor4f(1.0f,1.0f,1.0f,0.35f);
    glVertex2f(cx - r*0.3f, cy + r*0.35f);
    for(int i=0;i<=16;i++){
        float theta = M_PI * float(i) / 16.0f;
        float x = (cx - r*0.3f) + cosf(theta) * r*0.35f;
        float y = (cy + r*0.35f) + sinf(theta) * r*0.35f;
        glVertex2f(x,y);
    }
    glEnd();

    // outline
    glColor4f(0.03f,0.08f,0.15f,1.0f);
    glLineWidth(1.0f);
    glBegin(GL_LINE_LOOP);
    for(int i=0;i<=segments;i++){
        float theta = 2.0f * M_PI * float(i) / float(segments);
        float x = cx + cosf(theta) * r;
        float y = cy + sinf(theta) * r;
        glVertex2f(x,y);
    }
    glEnd();
}

// Draw a simple paddle (green with subtle shine)
void drawPaddle(float x, float y, float w, float h) {
    // Base rectangle (green)
    glBegin(GL_QUADS);
    glColor4f(0.12f, 0.7f, 0.3f, 1.0f); // new paddle color: green
    glVertex2f(x, y); glVertex2f(x+w, y);
    glVertex2f(x+w, y+h); glVertex2f(x, y+h);
    glEnd();

    // Top glossy strip
    glBegin(GL_QUADS);
    glColor4f(1.0f,1.0f,1.0f,0.12f);
    glVertex2f(x+2, y+h-6); glVertex2f(x+w-2, y+h-6);
    glVertex2f(x+w-2, y+h-2); glVertex2f(x+2, y+h-2);
    glEnd();

    // Simple stripes (light)
    glLineWidth(2.0f);
    glBegin(GL_LINES);
    glColor4f(0.9f,0.95f,0.9f,0.25f);
    for(float sx = x + 12.0f; sx < x + w - 12.0f; sx += 24.0f) {
        glVertex2f(sx, y+6.0f);
        glVertex2f(sx+10.0f, y+h-6.0f);
    }
    glEnd();

    // Outline
    glColor4f(0.02f,0.05f,0.03f,1.0f);
    glLineWidth(2.0f);
    glBegin(GL_LINE_LOOP);
    glVertex2f(x, y); glVertex2f(x+w, y);
    glVertex2f(x+w, y+h); glVertex2f(x, y+h);
    glEnd();
}

// Draw a simple brick rectangle (kept but with slight bevel effect)
void drawBrick(const Block &b) {
    if(!b.alive) return;
    // base
    glBegin(GL_QUADS);
    glColor4f(b.color.r, b.color.g, b.color.b, b.color.a);
    glVertex2f(b.x, b.y); glVertex2f(b.x + b.w, b.y);
    glVertex2f(b.x + b.w, b.y + b.h); glVertex2f(b.x, b.y + b.h);
    glEnd();

    // light top strip to simulate bevel
    glBegin(GL_QUADS);
    glColor4f(1.0f,1.0f,1.0f,0.08f);
    glVertex2f(b.x+2, b.y + b.h - 8);
    glVertex2f(b.x + b.w - 2, b.y + b.h - 8);
    glVertex2f(b.x + b.w - 2, b.y + b.h);
    glVertex2f(b.x+2, b.y + b.h);
    glEnd();

    // outline
    glColor4f(0.02f,0.02f,0.02f,0.7f);
    glLineWidth(1.0f);
    glBegin(GL_LINE_LOOP);
    glVertex2f(b.x, b.y); glVertex2f(b.x + b.w, b.y);
    glVertex2f(b.x + b.w, b.y + b.h); glVertex2f(b.x, b.y + b.h);
    glEnd();
}

void drawMainMenu() {
    // New background gradient: dark teal -> deep purple
    glBegin(GL_QUADS);
    glColor4f(0.02f, 0.08f, 0.10f, 1.0f); // bottom
    glVertex2f(0, 0); glVertex2f(WIN_W, 0);
    glColor4f(0.08f, 0.02f, 0.12f, 1.0f); // top
    glVertex2f(WIN_W, WIN_H); glVertex2f(0, WIN_H);
    glEnd();

    // Title with a new color (soft cyan)
    drawText(WIN_W/2-100, WIN_H-100, "DX BALL", GLUT_BITMAP_TIMES_ROMAN_24, {0.55f,0.95f,0.98f,1});

    // Menu options
    std::string menuItems[] = {
        "1. START GAME",
        "2. PLAYER NAME: " + playerName,
        "3. SCORE BOARD",
        "4. EXIT"
    };

    for(int i=0; i<4; i++) {
        // pulsing highlight for selected option
        Color c;
        if(i == menuSelection) {
            float pulse = 0.6f + 0.4f * (0.5f * (1.0f + sinf(menuPulse))); // between 0.6 and 1.0
            c = Color{pulse*0.6f, pulse*0.95f, 1.0f, 1.0f}; // cyan-ish highlight
        } else {
            c = Color{0.9f,0.9f,0.95f,1};
        }
        drawText(WIN_W/2-120, WIN_H-180-i*50, menuItems[i], GLUT_BITMAP_HELVETICA_18, c);
    }

    // Small footer lines (subtle) + sound status
    std::string footer = "Use NUMBER KEYS 1-4 to select menu  |  ENTER to confirm  |  ESC to go back";
    drawText(WIN_W/2-210, 110, footer, GLUT_BITMAP_9_BY_15, {0.7f,0.8f,0.9f,0.7f});

    std::string soundStatus = std::string("Sound: ") + (soundEnabled ? "ON (Press M to mute)" : "OFF (Press M to unmute)");
    drawText(WIN_W/2-160, 80, soundStatus, GLUT_BITMAP_9_BY_15, {0.8f,0.85f,1.0f,0.9f});
}

void drawPlayerNameScreen() {
    // Background: slightly lighter than main menu
    glBegin(GL_QUADS);
    glColor4f(0.03f, 0.06f, 0.08f, 1.0f);
    glVertex2f(0, 0); glVertex2f(WIN_W, 0);
    glColor4f(0.06f, 0.03f, 0.09f, 1.0f);
    glVertex2f(WIN_W, WIN_H); glVertex2f(0, WIN_H);
    glEnd();

    drawText(WIN_W/2-150, WIN_H-100, "CHANGE PLAYER NAME", GLUT_BITMAP_TIMES_ROMAN_24, {0.9f,0.7f,0.2f,1});

    // Current player info
    drawText(WIN_W/2-100, WIN_H-160, "Current Player: " + playerName, GLUT_BITMAP_HELVETICA_18, {0.95f,0.95f,0.95f,1});
    drawText(WIN_W/2-120, WIN_H-200, "Player " + std::to_string(currentPlayer+1) + " of 3", GLUT_BITMAP_HELVETICA_18, {0.95f,0.95f,0.95f,1});

    // Name input
    drawText(WIN_W/2-80, WIN_H-260, "Enter new name:", GLUT_BITMAP_HELVETICA_18, {0.95f,0.95f,0.95f,1});

    // Input box (accented)
    glBegin(GL_QUADS);
    glColor4f(0.15f, 0.15f, 0.22f, 0.95f);
    glVertex2f(WIN_W/2-100, WIN_H-300);
    glVertex2f(WIN_W/2+100, WIN_H-300);
    glVertex2f(WIN_W/2+100, WIN_H-270);
    glVertex2f(WIN_W/2-100, WIN_H-270);
    glEnd();

    // Input text (yellowish)
    drawText(WIN_W/2-90, WIN_H-285, tempName + "_", GLUT_BITMAP_HELVETICA_18, {1,0.95f,0.45f,1});

    // Instructions
    drawText(WIN_W/2-120, WIN_H-350, "Type name and press ENTER", GLUT_BITMAP_9_BY_15, {0.8f,0.8f,1,1});
    drawText(WIN_W/2-80, WIN_H-370, "ESC to cancel", GLUT_BITMAP_9_BY_15, {0.8f,0.8f,1,1});
}

void drawScoreBoard() {
    // Background with faint vignette
    glBegin(GL_QUADS);
    glColor4f(0.02f, 0.05f, 0.06f, 1.0f);
    glVertex2f(0, 0); glVertex2f(WIN_W, 0);
    glColor4f(0.05f, 0.02f, 0.07f, 1.0f);
    glVertex2f(WIN_W, WIN_H); glVertex2f(0, WIN_H);
    glEnd();

    drawText(WIN_W/2-80, WIN_H-100, "SCORE BOARD", GLUT_BITMAP_TIMES_ROMAN_24, {0.9f,0.9f,0.2f,1});

    // Show current selected player & best
    drawText(WIN_W/2-260, WIN_H-150, "Current Player: " + playerName, GLUT_BITMAP_HELVETICA_18, {0.95f,0.95f,0.95f,1});
    drawText(WIN_W/2-260, WIN_H-180, "Current Round Score: " + std::to_string(score), GLUT_BITMAP_HELVETICA_18, {0.95f,0.95f,0.95f,1});
    drawText(WIN_W/2-260, WIN_H-210, "Best Score (saved): " + std::to_string(playerScores[currentPlayer]), GLUT_BITMAP_HELVETICA_18, {0.95f,0.95f,0.95f,1});

    // Draw the global scoreboard list (sorted by score desc for display)
    std::vector<std::pair<std::string,int>> sorted = scoreboard;
    std::sort(sorted.begin(), sorted.end(), [](const auto &a, const auto &b){
        if(a.second != b.second) return a.second > b.second; // higher first
        return a.first < b.first;
    });

    drawText(WIN_W/2-80, WIN_H-260, "All Recorded Runs (Top entries):", GLUT_BITMAP_HELVETICA_18, {0.9f,0.9f,0.9f,1});

    int startY = WIN_H-300;
    int idx = 0;
    // show up to top 12 entries
    for(const auto &entry : sorted) {
        if(idx >= 12) break;
        std::string line = std::to_string(idx+1) + ". " + entry.first + "  -  " + std::to_string(entry.second);
        drawText(WIN_W/2-160, startY - idx*24, line, GLUT_BITMAP_HELVETICA_18, {0.9f,0.9f,0.95f,1});
        idx++;
    }
    if(sorted.empty()) {
        drawText(WIN_W/2-140, startY, "(No recorded runs yet)", GLUT_BITMAP_HELVETICA_18, {0.9f,0.9f,0.95f,0.9f});
    }

    // Instructions
    drawText(WIN_W/2-80, 80, "Press ESC to go back", GLUT_BITMAP_9_BY_15, {0.8f,0.8f,1,1});
}

void drawGameScreen() {
    // Game background gradient: deep ocean blues
    glBegin(GL_QUADS);
    glColor4f(0.02f, 0.06f, 0.12f, 1.0f);
    glVertex2f(0, 0); glVertex2f(WIN_W, 0);
    glColor4f(0.03f, 0.12f, 0.18f, 1.0f);
    glVertex2f(WIN_W, WIN_H); glVertex2f(0, WIN_H);
    glEnd();

    // Draw blocks (simple bricks)
    for(const auto& b: blocks) {
        if(b.alive) drawBrick(b);
    }

    // Draw player paddle (simple design)
    drawPaddle(padX, padY, padW, padH);

    // Draw ball (normal)
    drawBall(ballX, ballY, ballSize);

    // Draw player name above paddle
   // drawText(padX + padW/2 - 30, padY + padH + 10, playerName, GLUT_BITMAP_9_BY_15, {0.95f,0.95f,0.95f,1});

    // Draw score and lives (new small HUD box)
    glBegin(GL_QUADS);
    glColor4f(0.05f,0.05f,0.06f,0.65f);
    glVertex2f(10, WIN_H-40); glVertex2f(320, WIN_H-40);
    glVertex2f(320, WIN_H-10); glVertex2f(10, WIN_H-10);
    glEnd();

    drawText(20, WIN_H-28, "Score: " + std::to_string(score), GLUT_BITMAP_HELVETICA_18, {0.9f,0.9f,0.95f,1});
    drawText(20, WIN_H-48, "Lives: " + std::to_string(lives), GLUT_BITMAP_9_BY_15, {0.9f,0.9f,0.95f,1});
    // additional info
    drawText(140, WIN_H-48, "Player: " + playerName, GLUT_BITMAP_9_BY_15, {0.9f,0.9f,0.95f,1});
    drawText(140, WIN_H-28, "Speed: " + std::to_string((int)padSpeed), GLUT_BITMAP_9_BY_15, {0.9f,0.9f,0.95f,1});

    // Sound status in HUD
    drawText(240, WIN_H-28, std::string("Sound: ") + (soundEnabled ? "ON" : "OFF"), GLUT_BITMAP_9_BY_15, {0.95f,0.9f,0.6f,1});

    // Draw controls help
    drawText(WIN_W-300, WIN_H-30, "Arrow Keys/Mouse: Move", GLUT_BITMAP_9_BY_15, {0.9f,0.9f,0.95f,0.9f});
    drawText(WIN_W-300, WIN_H-50, "SPACE: Release Ball", GLUT_BITMAP_9_BY_15, {0.9f,0.9f,0.95f,0.9f});
    drawText(WIN_W-300, WIN_H-70, "P: Pause", GLUT_BITMAP_9_BY_15, {0.9f,0.9f,0.95f,0.9f});
    drawText(WIN_W-300, WIN_H-90, "ESC: Menu | M: Toggle Sound", GLUT_BITMAP_9_BY_15, {0.9f,0.9f,0.95f,0.9f});
}

void drawGameOver() {
    // Semi-transparent overlay
    glBegin(GL_QUADS);
    glColor4f(0.0f, 0.0f, 0.0f, 0.7f);
    glVertex2f(0, 0); glVertex2f(WIN_W, 0);
    glVertex2f(WIN_W, WIN_H); glVertex2f(0, WIN_H);
    glEnd();

    drawText(WIN_W/2-60, WIN_H/2+20, "GAME OVER", GLUT_BITMAP_HELVETICA_18, {1,0.3f,0.3f,1});
    drawText(WIN_W/2-80, WIN_H/2-10, "Score: " + std::to_string(score), GLUT_BITMAP_HELVETICA_18, {1,1,1,1});
    drawText(WIN_W/2-100, WIN_H/2-40, "Press ENTER for next player", GLUT_BITMAP_9_BY_15, {0.8f,0.8f,1,1});
    drawText(WIN_W/2-80, WIN_H/2-60, "ESC for Menu", GLUT_BITMAP_9_BY_15, {0.8f,0.8f,1,1});
}

void drawWinScreen() {
    // Semi-transparent overlay
    glBegin(GL_QUADS);
    glColor4f(0.0f, 0.0f, 0.0f, 0.6f);
    glVertex2f(0, 0); glVertex2f(WIN_W, 0);
    glVertex2f(WIN_W, WIN_H); glVertex2f(0, WIN_H);
    glEnd();

    drawText(WIN_W/2-40, WIN_H/2+20, "YOU WIN!", GLUT_BITMAP_HELVETICA_18, {0.4f,1.0f,0.6f,1});
    drawText(WIN_W/2-80, WIN_H/2-10, "Score: " + std::to_string(score), GLUT_BITMAP_HELVETICA_18, {1,1,1,1});
    drawText(WIN_W/2-120, WIN_H/2-40, "Press ENTER for next player", GLUT_BITMAP_9_BY_15, {0.8f,0.8f,1,1});
    drawText(WIN_W/2-100, WIN_H/2-60, "ESC for Menu", GLUT_BITMAP_9_BY_15, {0.8f,0.8f,1,1});
}

void renderScene() {
    glClear(GL_COLOR_BUFFER_BIT);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    if(gState == GameState::MENU) {
        switch(currentScreen) {
            case MenuScreen::MAIN:
                drawMainMenu();
                break;
            case MenuScreen::PLAYER_NAME:
                drawPlayerNameScreen();
                break;
            case MenuScreen::SCORE_BOARD:
                drawScoreBoard();
                break;
        }
    } else if(gState == GameState::PLAYING) {
        drawGameScreen();
    } else if(gState == GameState::GAME_OVER) {
        drawGameScreen();
        drawGameOver();
    } else if(gState == GameState::WIN) {
        drawGameScreen();
        drawWinScreen();
    }

    glutSwapBuffers();
}

bool checkCollision(float ax, float ay, float aw, float ah, float bx, float by, float bw, float bh) {
    return ax < bx+bw && ax+aw > bx && ay < by+bh && ay+ah > by;
}

void updateBall() {
    if(ballStuckToPaddle) {
        ballX = padX + padW/2.0f;
        return;
    }

    ballX += ballVX;
    ballY += ballVY;

    // Wall collision (left/right)
    if(ballX - ballSize < 0) {
        ballX = ballSize;
        ballVX = -ballVX;
    }
    if(ballX + ballSize > WIN_W) {
        ballX = WIN_W - ballSize;
        ballVX = -ballVX;
    }
    // Top
    if(ballY + ballSize > WIN_H) {
        ballY = WIN_H - ballSize;
        ballVY = -ballVY;
    }

    // Paddle collision
    if(ballY - ballSize < padY + padH && ballY > padY &&
       ballX > padX - ballSize && ballX < padX + padW + ballSize) {
        ballVY = fabs(ballVY);
        float hit = (ballX - (padX + padW/2)) / (padW/2);
        ballVX = hit * 12.0f;
        // nudge ball above paddle
        ballY = padY + padH + ballSize + 1.0f;
        // Play paddle sound (cartoon or fallback)
        playSfxFileOrAlias(CART_PADDLE_FILE, SND_PADDLE_HIT);
    }

    // Block collision
    for(auto &b : blocks) {
        if(b.alive && checkCollision(ballX-ballSize, ballY-ballSize, ballSize*2, ballSize*2, b.x, b.y, b.w, b.h)) {
            b.alive = false;
            // reflect based on hit: if hit on sides, change vx else vy
            float overlapLeft = (ballX+ballSize) - b.x;
            float overlapRight = (b.x + b.w) - (ballX-ballSize);
            float overlapTop = (b.y + b.h) - (ballY-ballSize);
            float overlapBottom = (ballY+ballSize) - b.y;
            float minOverlap = std::min({overlapLeft, overlapRight, overlapTop, overlapBottom});
            if(minOverlap == overlapLeft || minOverlap == overlapRight) {
                ballVX = -ballVX;
            } else {
                ballVY = -ballVY;
            }

            score += 10;
            playSfxFileOrAlias(CART_HIT_FILE, SND_HIT_BRICK);
            break;
        }
    }

    // Bottom boundary
    if(ballY - ballSize < 0) {
        lives--;
        playSfxFileOrAlias(CART_LOSE_FILE, SND_LOSE_LIFE);
        if(lives > 0) {
            resetBallOnPaddle();
        } else {
            // Round ended by losing all lives -> record run once, update best, and go to GAME_OVER
            saveBestForCurrentPlayer();
            recordScoreboardEntryIfNeeded();
            gState = GameState::GAME_OVER;
        }
    }

    // Check win condition
    bool blocksLeft = false;
    for(auto &b : blocks) {
        if(b.alive) { blocksLeft = true; break; }
    }
    if(!blocksLeft) {
        // Round ended by clearing all blocks -> record run once, update best, and go to WIN
        saveBestForCurrentPlayer();
        recordScoreboardEntryIfNeeded();
        gState = GameState::WIN;
        playSfxFileOrAlias(CART_WIN_FILE, SND_GAME_WIN);
    }
}

void update(int) {
    // update pulse for menu highlight
    menuPulse += 0.08f;
    if(menuPulse > 10000.0f) menuPulse = 0.0f;

    if(gState == GameState::PLAYING) {
        updateBall();
    }
    glutPostRedisplay();
    glutTimerFunc(16, update, 0);
}

void handleMenuAction() {
    switch(menuSelection) {
        case 0: // START GAME
            gState = GameState::PLAYING;
            resetLevel();
            playSfxFileOrAlias(CART_MENU_FILE, SND_MENU_NAV);
            break;
        case 1: // PLAYER NAME
            currentScreen = MenuScreen::PLAYER_NAME;
            tempName = playerName;
            playSfxFileOrAlias(CART_MENU_FILE, SND_MENU_NAV);
            break;
        case 2: // SCORE BOARD
            saveBestForCurrentPlayer();
            currentScreen = MenuScreen::SCORE_BOARD;
            playSfxFileOrAlias(CART_MENU_FILE, SND_MENU_NAV);
            break;
        case 3: // EXIT
            exit(0);
            break;
    }
}

void keyboard(unsigned char key, int, int) {
    if(gState == GameState::MENU) {

        // Name entry screen input
        if(currentScreen == MenuScreen::PLAYER_NAME) {
            if(key == 13) { // ENTER
                if(!tempName.empty()) {
                    playerName = tempName;
                    playerNames[currentPlayer] = tempName;
                }
                currentScreen = MenuScreen::MAIN;
                playSfxFileOrAlias(CART_MENU_FILE, SND_MENU_NAV);
            } else if(key == 27) { // ESC
                currentScreen = MenuScreen::MAIN;
                playSfxFileOrAlias(CART_MENU_FILE, SND_MENU_NAV);
            } else if(key == 8) { // BACKSPACE
                if(!tempName.empty()) tempName.pop_back();
            } else if(key >= 32 && key <= 126) { // Printable characters
                if(tempName.length() < 15) tempName += key;
            }
            return;
        }

        // Generic ESC = Back for submenus (e.g., SCORE_BOARD)
        if(key == 27) {
            if(currentScreen != MenuScreen::MAIN) {
                currentScreen = MenuScreen::MAIN;
                playSfxFileOrAlias(CART_MENU_FILE, SND_MENU_NAV);
                return;
            }
        }

        // Main menu navigation
        if(key >= '1' && key <= '4') {
            menuSelection = key - '1';
            playSfxFileOrAlias(CART_MENU_FILE, SND_MENU_NAV);
        } else if(key == 13) { // ENTER
            handleMenuAction();
        } else if(key == 'm' || key == 'M') { // toggle sound
            soundEnabled = !soundEnabled;
            // Play a tiny menu sound only if enabling sound (to confirm). If disabling, obviously don't play.
            if(soundEnabled) playSfxFileOrAlias(CART_MENU_FILE, SND_MENU_NAV);
        }
    } else if(gState == GameState::PLAYING) {
        if(key == 27) { // ESC to menu
            saveBestForCurrentPlayer();
            gState = GameState::MENU;
            playSfxFileOrAlias(CART_MENU_FILE, SND_MENU_NAV);
        } else if(key == ' ') { // SPACE to release ball
            ballStuckToPaddle = false;
        } else if(key == 'p' || key == 'P') {
            // Pause can be added here
        } else if(key == 'm' || key == 'M') { // sound toggle while playing
            soundEnabled = !soundEnabled;
            if(soundEnabled) playSfxFileOrAlias(CART_MENU_FILE, SND_MENU_NAV);
        }
    } else if(gState == GameState::GAME_OVER || gState == GameState::WIN) {
        if(key == 13) { // ENTER
            // Ensure best saved and scoreboard entry recorded (should already be), then progress to next player + new round
            saveBestForCurrentPlayer();
            recordScoreboardEntryIfNeeded();
            nextPlayer();
            gState = GameState::PLAYING;
            resetLevel();
        } else if(key == 27) { // ESC to menu
            saveBestForCurrentPlayer();
            recordScoreboardEntryIfNeeded();
            gState = GameState::MENU;
            playSfxFileOrAlias(CART_MENU_FILE, SND_MENU_NAV);
        } else if(key == 'm' || key == 'M') {
            soundEnabled = !soundEnabled;
            if(soundEnabled) playSfxFileOrAlias(CART_MENU_FILE, SND_MENU_NAV);
        }
    }
}

void specialKeys(int key, int, int) {
    if(gState != GameState::PLAYING) return;
    switch(key) {
        case GLUT_KEY_LEFT:
            padX -= padSpeed;
            if(padX < 10) padX = 10;
            break;
        case GLUT_KEY_RIGHT:
            padX += padSpeed;
            if(padX > WIN_W - padW - 10) padX = WIN_W - padW - 10;
            break;
    }
}

void mouseMotion(int x, int y) {
    if(gState == GameState::PLAYING) {
        padX = x - padW/2;
        if(padX < 10) padX = 10;
        if(padX > WIN_W - padW - 10) padX = WIN_W - padW - 10;
    }
}

void mouseClick(int button, int state, int, int) {
    if(gState == GameState::PLAYING && button == GLUT_LEFT_BUTTON && state == GLUT_DOWN) {
        ballStuckToPaddle = false;
    }
}

void reshape(int w, int h) {
    glViewport(0, 0, w, h);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    gluOrtho2D(0, WIN_W, 0, WIN_H);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
}

int main(int argc, char** argv) {
    std::srand((unsigned)std::time(nullptr));

    glutInit(&argc, argv);
    glutInitDisplayMode(GLUT_DOUBLE | GLUT_RGBA);
    glutInitWindowSize(WIN_W, WIN_H);
    glutCreateWindow("DX Ball");

    glutDisplayFunc(renderScene);
    glutKeyboardFunc(keyboard);
    glutSpecialFunc(specialKeys);
    glutPassiveMotionFunc(mouseMotion);
    glutMouseFunc(mouseClick);
    glutReshapeFunc(reshape);
    glutTimerFunc(16, update, 0);

    // Initialize game
    playerName = playerNames[currentPlayer];
    resetLevel();

    // Print to console about sound files presence (helpful for debugging)
    std::cout << "Sound enabled: " << (soundEnabled ? "YES" : "NO") << "\n";
    std::cout << "cartoon_hit.wav present: " << (fileExists(CART_HIT_FILE) ? "YES" : "NO") << "\n";
    std::cout << "cartoon_paddle.wav present: " << (fileExists(CART_PADDLE_FILE) ? "YES" : "NO") << "\n";
    std::cout << "cartoon_lose.wav present: " << (fileExists(CART_LOSE_FILE) ? "YES" : "NO") << "\n";
    std::cout << "cartoon_win.wav present: " << (fileExists(CART_WIN_FILE) ? "YES" : "NO") << "\n";
    std::cout << "cartoon_menu.wav present: " << (fileExists(CART_MENU_FILE) ? "YES" : "NO") << "\n";
    std::cout << "If you want cartoon sounds, place the above .wav files next to the executable.\n";

    glutMainLoop();
    return 0;
}
