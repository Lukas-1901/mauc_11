#pragma once
#include <stdint.h>

namespace GameMaze {
    // Bitmasken für die Wände (und Status) einer Zelle
    constexpr uint8_t WALL_N  = 0x01;
    constexpr uint8_t WALL_E  = 0x02;
    constexpr uint8_t WALL_S  = 0x04;
    constexpr uint8_t WALL_W  = 0x08;
    constexpr uint8_t VISITED = 0x10;

    // Initialisiert den Speicher für das Grid basierend auf der Zellengröße
    void init(int cellSize, int screenWidth, int screenHeight);
    
    // Gibt den allokierten Speicher frei
    void cleanup();
    
    // Startet die DFS-Generierung
    void generate();
    
    // Gibt den Zustand einer spezifischen Zelle zurück
    uint8_t getCell(int cx, int cy);
    
    // Getter für die Grid-Dimensionen (später wichtig fürs Rendering)
    int getCols();
    int getRows();
}