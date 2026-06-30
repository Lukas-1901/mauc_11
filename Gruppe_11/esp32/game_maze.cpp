#include "game_maze.h"
#include <stdlib.h>
#include <Arduino.h>

namespace GameMaze {
    static uint8_t* grid = nullptr;
    static int cols = 0;
    static int rows = 0;

    // Interne Hilfsfunktion für den 1D-Array-Zugriff
    static inline int getIndex(int x, int y) {
        return y * cols + x;
    }

    // Richtungs-Arrays (N, E, S, W) für einfaches Iterieren
    static const int DX[] = { 0, 1, 0, -1 };
    static const int DY[] = { -1, 0, 1, 0 };
    static const uint8_t DIR[] = { WALL_N, WALL_E, WALL_S, WALL_W };
    static const uint8_t OPPOSITE[] = { WALL_S, WALL_W, WALL_N, WALL_E };

    void init(int cellSize, int screenWidth, int screenHeight) {
        cleanup(); // Sicherheitshalber alten Speicher freigeben

        cols = screenWidth / cellSize;
        rows = screenHeight / cellSize;
        
        int totalCells = cols * rows;
        grid = new (std::nothrow) uint8_t[totalCells];
        
        if (grid) {
            // Alle Zellen initialisieren: Alle Wände stehen, noch nicht besucht
            for (int i = 0; i < totalCells; i++) {
                grid[i] = WALL_N | WALL_E | WALL_S | WALL_W;
            }
        } else {
            Serial.println("[MAZE] Fehler: Nicht genug RAM für Grid");
        }
    }

    void cleanup() {
        if (grid) {
            delete[] grid;
            grid = nullptr;
            cols = 0;
            rows = 0;
        }
    }

    static void carvePassagesFrom(int cx, int cy) {
        grid[getIndex(cx, cy)] |= VISITED;

        // Startrichtungen zufällig anordnen
        int directions[] = { 0, 1, 2, 3 };
        for (int i = 0; i < 4; i++) {
            int r = random(4);
            int temp = directions[i];
            directions[i] = directions[r];
            directions[r] = temp;
        }

        // Durch Nachbarn iterieren
        for (int i = 0; i < 4; i++) {
            int dirIndex = directions[i];
            int nx = cx + DX[dirIndex];
            int ny = cy + DY[dirIndex];

            // Prüfen, ob Nachbar im Grid liegt und unbesucht ist
            if (nx >= 0 && ny >= 0 && nx < cols && ny < rows) {
                if ((grid[getIndex(nx, ny)] & VISITED) == 0) {
                    // Wand zwischen aktueller Zelle und Nachbar einreißen
                    grid[getIndex(cx, cy)] &= ~DIR[dirIndex];
                    grid[getIndex(nx, ny)] &= ~OPPOSITE[dirIndex];
                    
                    // Rekursion
                    carvePassagesFrom(nx, ny);
                }
            }
        }
    }

    void generate() {
        if (!grid) return;
        randomSeed(analogRead(0) + millis()); // Etwas Entropie für Zufall
        
        // Start der Generierung in der Mitte oder oben links (hier 0,0)
        carvePassagesFrom(0, 0);
    }

    uint8_t getCell(int cx, int cy) {
        if (cx < 0 || cy < 0 || cx >= cols || cy >= rows || !grid) return 0;
        return grid[getIndex(cx, cy)];
    }

    int getCols() { return cols; }
    int getRows() { return rows; }
}