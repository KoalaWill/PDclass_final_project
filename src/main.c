#include "raylib.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "resource_dir.h"   // utility header for SearchAndSetResourceDir

#define MAX_COLS 30
#define MAX_ROWS 30

// Maneuver array
const int Mode_Movement_Fuel[4][8][4]={/*mode 0*/{{0,0,1,1},{0,0,-1,1},{0,1,0,3},{0,-1,0,3},{1,2,0,3},{1,1,-1,3},{3,0,-2,3},{3,-1,-1,3}},
									   /*mode 1*/{{1,1,0,1},{1,-1,0,1},{1,0,-1,3},{1,0,1,3},{2,0,-2,3},{2,-1,-1,3},{0,-2,0,3},{0,-1,1,3}},
									   /*mode 2*/{{2,0,-1,1},{2,0,1,1},{2,-1,0,3},{2,1,0,3},{3,-2,0,3},{3,-1,1,3},{1,0,2,3},{1,1,1,3}},
									   /*mode 3*/{{3,-1,0,1},{3,1,0,1},{3,0,1,3},{3,0,-1,3},{0,0,2,3},{0,1,1,3},{2,2,0,3},{2,1,-1,3}}
}; // mode, dx, dy, fuel cost
									   
									   // MODE 0     MODE 1       MODE 2      MODE 3
									   // X O        O O X        O O         O O O
									   // O O        O O O        O O         X O O
									   // O O                     O X

                                       // App States
typedef enum AppScreen { 
	StartMenu = 0, 
	MazeConfirm,
    AccessibilityCheck
} AppScreen;

// Start menu display variables
// Window
const int screenWidth = 1280;
const int screenHeight = 800;
int currentX;
int currentY;
// Maze variables
int **maze = NULL;
int rows = 0;
int cols = 0;
bool mazeLoaded = false;
// Maze display variables
int mazeDisplayMargin;
int availableWidth;
int availableHeight;
int cellSize;
int mazePixelWidth;
int mazePixelHeight;
int offsetX;
int offsetY;
// Accessibility check variables
// queue
typedef struct {
    int x, y, mode;
} State;
typedef struct {
    State *items;
    int head, tail;
    int capacity;
} Queue;
Queue* createQueue(int capacity) {
    Queue* q = (Queue*)malloc(sizeof(Queue));
    q->items = (State*)malloc(sizeof(State) * capacity);
    q->head = 0;
    q->tail = 0;
    q->capacity = capacity;
    return q;
}
int isQueueEmpty(Queue* q) { return q->head == q->tail; }
void enqueue(Queue* q, State s) {
    q->items[q->tail++] = s;
}
State dequeue(Queue* q) {
    return q->items[q->head++];
}
Queue* q;
State start_state;
// A simple list to remember where objectives are
struct { int x, y; bool reachable; } objectives[100]; 
int objCount = 0;
bool visited[MAX_ROWS][MAX_COLS][4];
int reachableCount = 0;
int nextMode;
int dx;
int dy;
int nx;
int ny;

// Start menu
// ASCII art
const char *ascii_art[] = {
    " ________  ________  _________  ___  ___  ________ ___  ________   ________  _______   ________   ",
    "|\\   __  \\|\\   __  \\|\\___   ___\\\\  \\|\\  \\|\\  _____\\\\  \\|\\   ___  \\|\\   ___ \\|\\   ____\\|\\   __  \\  ",
    "\\ \\  \\|\\  \\ \\  \\|\\  \\|___ \\  \\_\\ \\  \\\\\\  \\ \\  \\___| \\  \\ \\  \\\\ \\  \\ \\  \\_|\\ \\ \\  \\___|\\ \\  \\|\\  \\ ",
    " \\ \\   ____\\ \\   __  \\   \\ \\  \\ \\ \\   __  \\ \\   __\\\\ \\  \\ \\  \\\\ \\  \\ \\  \\ \\\\ \\ \\   ____\\ \\  __  _\\",
    "  \\ \\  \\___|\\ \\  \\ \\  \\   \\ \\  \\ \\ \\  \\ \\  \\ \\  \\_| \\ \\  \\ \\  \\\\ \\  \\ \\  \\_\\\\ \\ \\  \\___|\\ \\ \\ \\ \\|",
    "   \\ \\__\\    \\ \\__\\ \\__\\   \\ \\__\\ \\ \\__\\ \\__\\ \\__\\   \\ \\__\\ \\__\\\\ \\__\\ \\_______\\ \\_______\\ \\_\\ \\_\\",
    "    \\ |__|     \\|__|\\|__|    \\|__|  \\|__|\\|__|\\|__|    \\|__|\\|__| \\|__|\\|_______|\\|_______|\\|_|\\|_|"
};
void DrawGradientTitle() {
    // Gradient blue to orange
    Color startColor = {0, 100, 255, 255};
    Color endColor = {255, 140, 0, 255};
	// text var
    int artRows = sizeof(ascii_art) / sizeof(ascii_art[0]);
    int fontSize = 16;
    int charSpacing = 12;
    int lineHeight = fontSize + 2;
    // Center title
    int TitletotalWidth = strlen(ascii_art[0]) * charSpacing;
    int TitletotalHeight = artRows * lineHeight;
    int startX = (screenWidth - TitletotalWidth) / 2;
    int startY = (screenHeight - TitletotalHeight) / 2 - 80; // nudge up little
    // Draw title
    for (int i = 0; i < artRows; i++) {
		int len = strlen(ascii_art[i]);
        for (int j = 0; j < len; j++) {
			float t = (float)j / len;
            unsigned char r = (unsigned char)(startColor.r + t * (endColor.r - startColor.r));
            unsigned char g = (unsigned char)(startColor.g + t * (endColor.g - startColor.g));
            unsigned char b = (unsigned char)(startColor.b + t * (endColor.b - startColor.b));
            Color drawColor = {r, g, b, 255};
            char tempStr[2] = { ascii_art[i][j], '\0' };
            DrawText(tempStr, startX + (j * charSpacing), startY + (i * lineHeight), fontSize, drawColor);
        }
    }
	// Move cursor
	currentY = startY + TitletotalHeight + 30;
}
void DrawCredits() {
	const char* sub1 = "Programming Final Project | Time: December 2025 | Author: William Lin | ID: 114033213 | Dept: PME";
	const char* sub2 = "Using Raylib";
	
	int subSize = 20; 
	int subWidth1 = MeasureText(sub1, subSize);
	int subWidth2 = MeasureText(sub2, subSize);
	
    DrawText(sub1, (screenWidth - subWidth1)/2, currentY, subSize, WHITE);
    currentY += 25;
    DrawText(sub2, (screenWidth - subWidth2)/2, currentY, subSize, WHITE);
}
void DrawBlinkHint() {
    // Draw Instruction Text below (Blinking)
    const char* msg = "PRESS [ENTER] TO VIEW MAZE";
    int textWidth = MeasureText(msg, 20);
    if (((int)(GetTime() * 2)) % 2 == 0) {
        // Positioned further down below the subtitles
        DrawText(msg, (screenWidth - textWidth)/2, currentY + 50, 20, WHITE);
    }
    
}

// Maze page 
bool LoadMaze(const char *filename) {
	rows = 0; cols = 0;
    bool col_calculated = false;
    int ch;
	
    FILE *inf = fopen(filename, "r");
    if (inf == NULL) return false;
    // Calculate rows and cols
    while (1) {
        ch = fgetc(inf);
        if (ch != '\n' && ch != EOF) {
            if (!col_calculated && ch != ' ') cols++;
        } else {
            if (ch == EOF) {
                if(cols > 0) rows++;
                break;
            }
            if(!col_calculated) col_calculated = true;
            rows++;
        }
    }
	printf("\n");
    // Make memory space
    maze = (int **)malloc(rows * sizeof(int *));
    for (int i = 0; i < rows; i++) {
        maze[i] = (int *)malloc(cols * sizeof(int));
    }
    // rewind and read in data
    rewind(inf);
    for (int i = 0; i < rows; i++) {
        for (int j = 0; j < cols; j++) {
            if (fscanf(inf, "%1d", &maze[i][j]) != 1) {
                maze[i][j] = 5; 
            }
            if(maze[i][j] == 2){
                start_state.x = j;
                start_state.y = i;
                if(maze[i][j-1] == 2 && maze[i][j-2] == 2) {
                    start_state.mode = 3;
                    start_state.x = j-2;
                }
                if(maze[i][j-1] == 2) {
                    start_state.mode = 2;
                }
            }
        }
    }
	fclose(inf);
    return true;
}
void DrawMazeGrid() {
	// Check maze
    if (!mazeLoaded || rows == 0 || cols == 0) {
        DrawText("Error: Maze not loaded or empty.", 100, 100, 20, ORANGE);
        return;
    }

	// Size calculations
    mazeDisplayMargin = 40;
    availableWidth = screenWidth - (mazeDisplayMargin*2);
    availableHeight = screenHeight - (mazeDisplayMargin*2);
    cellSize = (availableWidth/cols < availableHeight/rows)? availableWidth/cols : availableHeight/rows; 
	mazePixelWidth = cols * cellSize;
    mazePixelHeight = rows * cellSize;
    offsetX = (screenWidth-mazePixelWidth) / 2;
    offsetY = (screenHeight-mazePixelHeight) / 2;

    for (int i = 0; i < rows; i++) {
        for (int j = 0; j < cols; j++) {
            int val = maze[i][j];
            int x = offsetX + j * cellSize;
            int y = offsetY + i * cellSize;

            Color c;
            switch(val) {
                case 0: c = DARKGRAY; break;// Wall
                case 1: c = LIME; break;// Accessible
				case 2: c = MAROON; break;// Starting point
				case 3: c = BLUE; break;// Objective
				default: c = BLACK;
            }

            DrawRectangle(x, y, cellSize, cellSize, c);
            
            // border
            DrawRectangleLines(x, y, cellSize, cellSize, BLACK);
        }
    }
    DrawText(TextFormat("Maze input detected, maze size: %d(rows) x %d(columns)", rows, cols), 10, screenHeight - 30, 20, GRAY);
    DrawText("PRESS [ENTER] TO CONFIRM MAZE MAP", 10, 10, 20, LIGHTGRAY);
}
// collision checker
int CheckCarCollision(int x, int y, int mode) {
    int body[6][2]; 

    // Define car shape based on mode
    switch(mode) {
        case 0: { int b[6][2]={{0,0},{1,0},{0,1},{1,1},{0,2},{1,2}}; for(int k=0;k<6;k++){body[k][0]=b[k][0]; body[k][1]=b[k][1];} break; }
        case 1: { int b[6][2]={{0,0},{-1,0},{-2,0},{0,1},{-1,1},{-2,1}}; for(int k=0;k<6;k++){body[k][0]=b[k][0]; body[k][1]=b[k][1];} break; }
        case 2: { int b[6][2]={{0,0},{-1,0},{0,-1},{-1,-1},{0,-2},{-1,-2}}; for(int k=0;k<6;k++){body[k][0]=b[k][0]; body[k][1]=b[k][1];} break; }
        case 3: { int b[6][2]={{0,0},{1,0},{2,0},{0,-1},{1,-1},{2,-1}}; for(int k=0;k<6;k++){body[k][0]=b[k][0]; body[k][1]=b[k][1];} break; }
    }

    for (int i = 0; i < 6; i++) {
        int cx = x + body[i][0];
        int cy = y + body[i][1];
        if (cx < 0 || cx >= cols || cy < 0 || cy >= rows) return 0; // out of bounds
        if (maze[cy][cx] == 0) return 0; // wall
    }
    return 1; // safe
}

// accessibility check via BFS
void CheckAccessibility() {
    Queue* q = createQueue(MAX_ROWS * MAX_COLS * 4);
    memset(visited, 0, sizeof(visited));
    for(int r=0; r<rows; r++){
        for(int c=0; c<cols; c++){
            if(maze[r][c] == 3) {
                objectives[objCount].x = c;
                objectives[objCount].y = r;
                objectives[objCount].reachable = false;
                objCount++;
            }
        }
    }
    if (CheckCarCollision(start_state.x, start_state.y, start_state.mode)) {
        visited[start_state.y][start_state.x][0] = true;
        enqueue(q, (State){start_state.x, start_state.y, 0});
    } else {
        printf("Error: Car cannot fit at start position in Mode%d.\n", start_state.mode);
        return;
    }

    // BFS
    while (!isQueueEmpty(q)) {
        State current = dequeue(q);
        if (maze[current.y][current.x] == 3) {
            // mark objective reachable
            for(int i=0; i<objCount; i++) {
                if(objectives[i].x == current.x && objectives[i].y == current.y) {
                    objectives[i].reachable = true;
                }
            }
        }

        // try all directions
        for (int i = 0; i < 8; i++) {
            nextMode = Mode_Movement_Fuel[current.mode][i][0];
            dx = Mode_Movement_Fuel[current.mode][i][1];
            dy = Mode_Movement_Fuel[current.mode][i][2];
            nx = current.x + dx;
            ny = current.y + dy;

            // check bounds
            if (nx < 0 || nx >= cols || ny < 0 || ny >= rows) continue;

            // If reachable
            if (!visited[ny][nx][nextMode]) {
                if (CheckCarCollision(nx, ny, nextMode)) {
                    visited[ny][nx][nextMode] = true;
                    enqueue(q, (State){nx, ny, nextMode});
                }
            }
        }
    }
    // print it out.
    printf("Reachable analysis via BFS:\n");
    objCount = 0;
    for(int i=0; i<objCount; i++) {
        if(objectives[i].reachable) {
            printf("Objective at (%d, %d) is ACCESSIBLE.\n", objectives[i].x, objectives[i].y);
            reachableCount++;
        } else {
            printf("Objective at (%d, %d) is UNREACHABLE.\n", objectives[i].x, objectives[i].y);
        }
    }
    
    printf("\nSummary: %d / %d objectives reachable.\n", reachableCount, objCount);

    // Cleanup
    free(q->items);
    free(q);
    free(visited);
}

int main(void) {
    SetConfigFlags(FLAG_VSYNC_HINT | FLAG_WINDOW_HIGHDPI);
    InitWindow(screenWidth, screenHeight, "Pathfinder GUI");
	mazeLoaded = false;
	// read input
    if(!mazeLoaded) mazeLoaded = LoadMaze("input.txt");
	// init state
    AppScreen currentScreen = StartMenu;

    while (!WindowShouldClose()) {
		// State control
        if (currentScreen == StartMenu) {
            if (IsKeyPressed(KEY_ENTER) || IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                currentScreen = MazeConfirm;
            }
        }
		else if (currentScreen == MazeConfirm) {
			if (IsKeyPressed(KEY_ENTER) || IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
				currentScreen = AccessibilityCheck;
			}
		}

        BeginDrawing();
            ClearBackground(BLACK);
            switch(currentScreen) {
                case StartMenu:
                    ClearBackground(BLACK);
                    DrawGradientTitle();
					DrawCredits();
					DrawBlinkHint();
                    break;
                case MazeConfirm:
                    ClearBackground(BLACK);
                    DrawMazeGrid();
                    break;
                case AccessibilityCheck:
                    ClearBackground(BLACK);
                    CheckAccessibility();
            }

        EndDrawing();
    }

    CloseWindow();

    return 0;
}