#include "raylib.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <stdint.h> 

#include "resource_dir.h"   // utility header for SearchAndSetResourceDir

#define MAX_COLS 30
#define MAX_ROWS 30

#define GET_IDX(r, c, m, mk) ( ((size_t)(r) * cols * 4 * maxMask) + ((size_t)(c) * 4 * maxMask) + ((size_t)(m) * maxMask) + (mk) )


// Maneuver array
const int Mode_Movement_Fuel[4][8][4]={/*mode 0*/{{0,0,1,1},{0,0,-1,1},{0,1,0,3},{0,-1,0,3},{1,2,0,3},{1,1,1,3},{3,0,2,3},{3,-1,1,3}},
                                       /*mode 1*/{{1,1,0,1},{1,-1,0,1},{1,0,-1,3},{1,0,1,3},{2,0,2,3},{2,-1,1,3},{0,-2,0,3},{0,-1,-1,3}},
                                       /*mode 2*/{{2,0,-1,1},{2,0,1,1},{2,-1,0,3},{2,1,0,3},{3,-2,0,3},{3,-1,-1,3},{1,0,-2,3},{1,1,-1,3}},
                                       /*mode 3*/{{3,-1,0,1},{3,1,0,1},{3,0,1,3},{3,0,-1,3},{0,0,-2,3},{0,1,-1,3},{2,2,0,3},{2,1,1,3}}
}; // mode, dx, dy, fuel cost
									   // MODE 0     MODE 1       MODE 2      MODE 3
									   // X O        O O X        O O         O O O
									   // O O        O O O        O O         X O O
									   // O O                     O X
// App States
typedef enum AppScreen { 
    StartMenu = 0, 
    MazeConfirm,
    AccessibilityCheck,
    PathPlayback // Added new state
} AppScreen;

// Window variables
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

// Playback variables
int currentPlaybackStep = 0;
int playbackFrameCounter = 0;
bool playbackFinished = false;

// Accessibility check variables
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
struct { int x, y; bool reachable; } objectives[100]; 
int objCount = 0;
bool visited[MAX_ROWS][MAX_COLS][4];
int reachableCount = 0;
int nextMode;
int dxdy[2];
int nx, ny;
bool accessChecked;

// TSP calculation variables
bool solvedTSP;
int totalFuelCost = 0; // Global variable to store total fuel cost
// priority queue
typedef struct {
    int x, y;
    int mode;
    int mask; // Bitmask representing visited objectives
    int cost; // Accumulated fuel cost
} PQNode;
//min-heap for Dijkstra
typedef struct {
    PQNode *nodes;
    int size;
    int capacity;
} MinHeap;
MinHeap* createMinHeap(int capacity) {
    MinHeap* h = (MinHeap*)malloc(sizeof(MinHeap));
    h->nodes = (PQNode*)malloc(sizeof(PQNode) * capacity);
    h->size = 0;
    h->capacity = capacity;
    return h;
}
void resizeHeap(MinHeap* h) {
    h->capacity *= 2;
    h->nodes = (PQNode*)realloc(h->nodes, sizeof(PQNode) * h->capacity);
}
void pushHeap(MinHeap* h, PQNode n) {
    if (h->size == h->capacity) resizeHeap(h);
    int i = h->size++;
    while (i > 0) {
        int p = (i - 1) / 2;
        if (h->nodes[p].cost <= n.cost) break;
        h->nodes[i] = h->nodes[p];
        i = p;
    }
    h->nodes[i] = n;
}
PQNode popHeap(MinHeap* h) {
    PQNode ret = h->nodes[0];
    PQNode n = h->nodes[--h->size];
    int i = 0;
    while (i * 2 + 1 < h->size) {
        int a = i * 2 + 1;
        int b = i * 2 + 2;
        if (b < h->size && h->nodes[b].cost < h->nodes[a].cost) a = b;
        if (h->nodes[a].cost >= n.cost) break;
        h->nodes[i] = h->nodes[a];
        i = a;
    }
    h->nodes[i] = n;
    return ret;
}
void freeHeap(MinHeap* h) {
    free(h->nodes);
    free(h);
}
int *tspDist = NULL;
size_t *tspParent = NULL;
typedef struct { int x, y, m; } PathStep;
PathStep* tspPathTrace = NULL;
int tspStepCount = 0;
// bitmask bits to actual coordinates
typedef struct { 
    int x, y; 
    int originalIdx; 
} ActiveTarget;
ActiveTarget activeTargets[MAX_COLS * MAX_ROWS]; // Max possible size
int activeCount = 0;

// Start menu
const char *ascii_art[] = {
    " ________  ________  _________  ___  ___  ________ ___  ________   ________  _______   ________   ",
    "|\\   __  \\|\\   __  \\|\\___   ___\\\\  \\|\\  \\|\\  _____\\\\  \\|\\   ___  \\|\\   ___ \\|\\   ____\\|\\   __  \\  ",
    "\\ \\  \\|\\  \\ \\  \\|\\  \\|___ \\  \\_\\ \\  \\\\\\  \\ \\  \\___| \\  \\ \\  \\\\ \\  \\ \\  \\_|\\ \\ \\  \\___|\\ \\  \\|\\  \\ ",
    " \\ \\   ____\\ \\   __  \\   \\ \\  \\ \\ \\   __  \\ \\   __\\\\ \\  \\ \\  \\\\ \\  \\ \\  \\ \\\\ \\ \\   ____\\ \\   __  _\\",
    "  \\ \\  \\___|\\ \\  \\ \\  \\   \\ \\  \\ \\ \\  \\ \\  \\ \\  \\_| \\ \\  \\ \\  \\\\ \\  \\ \\  \\_\\\\ \\ \\  \\___|\\ \\  \\ \\|",
    "   \\ \\__\\    \\ \\__\\ \\__\\   \\ \\__\\ \\ \\__\\ \\__\\ \\__\\   \\ \\__\\ \\__\\\\ \\__\\ \\_______\\ \\_______\\ \\_\\ \\_\\",
    "    \\ |__|     \\|__|\\|__|    \\|__|  \\|__|\\|__|\\|__|    \\|__|\\|__| \\|__|\\|_______|\\|_______|\\|_|\\|_|"
};

// Start menu
void DrawGradientTitle() {
    Color startColor = {0, 100, 255, 255};
    Color endColor = {255, 140, 0, 255};
    
    int artRows = sizeof(ascii_art) / sizeof(ascii_art[0]);
    int fontSize = 16;
    int charSpacing = 12;
    int lineHeight = fontSize + 2;
    
    int TitletotalWidth = strlen(ascii_art[0]) * charSpacing;
    int TitletotalHeight = artRows * lineHeight;
    int startX = (screenWidth - TitletotalWidth) / 2;
    int startY = (screenHeight - TitletotalHeight) / 2 - 80;

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
    const char* msg = "PRESS [ENTER] TO VIEW MAZE";
    int textWidth = MeasureText(msg, 20);
    if (((int)(GetTime() * 2)) % 2 == 0) {
        DrawText(msg, (screenWidth - textWidth)/2, currentY + 50, 20, WHITE);
    }
}

// Maze confirmation
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
    
    maze = (int **)malloc(rows * sizeof(int *));
    for (int i = 0; i < rows; i++) {
        maze[i] = (int *)malloc(cols * sizeof(int));
    }
    
    rewind(inf);
    for (int i = 0; i < rows; i++) {
        for (int j = 0; j < cols; j++) {
            if (fscanf(inf, "%1d", &maze[i][j]) != 1) {
                maze[i][j] = 5; 
            }
            if(maze[i][j] == 2){
                start_state.x = j;
                start_state.y = i;
                start_state.mode = 0; // Default mode 0
                // Heuristic check for mode based on vehicle size/orientation
                if (j >= 2 && maze[i][j-1] == 2 && maze[i][j-2] == 2) {
                      start_state.mode = 3;
                      start_state.x = j-2;
                }
                else if (j >= 1 && maze[i][j-1] == 2) {
                      start_state.mode = 2; // Assuming
                }
            }
        }
    }
    fclose(inf);
    return true;
}
void DrawMazeGrid() {
    if (!mazeLoaded || rows == 0 || cols == 0) {
        DrawText("Error: Maze not loaded or empty.", 100, 100, 20, ORANGE);
        return;
    }

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
            DrawRectangleLines(x, y, cellSize, cellSize, BLACK);
        }
    }
    DrawText(TextFormat("Maze input detected, maze size: %d(rows) x %d(columns)", rows, cols), 10, screenHeight - 30, 20, GRAY);
    DrawText("PRESS [ENTER] TO CONFIRM MAZE MAP", 10, 10, 20, LIGHTGRAY);
}
// get car shape coordinates relative to (0,0)
void GetCarBody(int mode, int body[6][2]) {
    // Copying the same definitions from your CheckCarCollision function
    switch(mode) {
        case 0: { int b[6][2]={{0,0},{1,0},{0,1},{1,1},{0,2},{1,2}}; memcpy(body, b, sizeof(b)); break; }
        case 1: { int b[6][2]={{0,0},{-1,0},{-2,0},{0,1},{-1,1},{-2,1}}; memcpy(body, b, sizeof(b)); break; }
        case 2: { int b[6][2]={{0,0},{-1,0},{0,-1},{-1,-1},{0,-2},{-1,-2}}; memcpy(body, b, sizeof(b)); break; }
        case 3: { int b[6][2]={{0,0},{1,0},{2,0},{0,-1},{1,-1},{2,-1}}; memcpy(body, b, sizeof(b)); break; }
    }
}
// Accessibility check
int CheckCarCollision(int x, int y, int mode) {
    int body[6][2]; 

    GetCarBody(mode, body);

    for (int i = 0; i < 6; i++) {
        int cx = x + body[i][0];
        int cy = y + body[i][1];
        if (cx < 0 || cx >= cols || cy < 0 || cy >= rows) return 0; 
        if (maze[cy][cx] == 0) return 0; 
    }
    return 1;
}
void CheckAccessibility() {
    q = createQueue(MAX_ROWS * MAX_COLS * 4);
    objCount=0;
    memset(visited, 0, sizeof(visited));
    
    // Reset Reachable Count
    reachableCount = 0;

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
        enqueue(q, (State){start_state.x, start_state.y, start_state.mode});
    }
    
    // BFS
    while (!isQueueEmpty(q)) {
        State current = dequeue(q);
        if (maze[current.y][current.x] == 3) {
             for(int i=0; i<objCount; i++) {
                if(objectives[i].x == current.x && objectives[i].y == current.y) {
                    if(!objectives[i].reachable) {
                        objectives[i].reachable = true;
                    }
                }
            }
        }

        // try all directions
        int dx, dy;
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
    
    for(int i=0; i<objCount; i++) {
        if(objectives[i].reachable) reachableCount++;
        else{
            maze[objectives[i].y][objectives[i].x] = 1;
        }
    }

    // Cleanup
    free(q->items);
    free(q);
}
void DrawAccessibilityResults() {
    int startY = 100;
    int spacing = 30;
    int titleSize = 40;
    int textSize = 24;

    const char* title = "ACCESSIBILITY ANALYSIS RESULTS";
    int titleWidth = MeasureText(title, titleSize);
    DrawText(title, (screenWidth - titleWidth)/2, 40, titleSize, SKYBLUE);

    DrawLine(100, 85, screenWidth-100, 85, DARKGRAY);

    for(int i=0; i<objCount; i++) {
        // Build the result string
        const char* status = objectives[i].reachable ? "ACCESSIBLE" : "UNREACHABLE";
        Color statusColor = objectives[i].reachable ? LIME : RED;
        
        char buffer[100];
        sprintf(buffer, "Objective at (%d, %d): ", objectives[i].x, objectives[i].y);
        
        // Draw coordinate text
        int textW = MeasureText(buffer, textSize);
        DrawText(buffer, 150, startY + (i*spacing), textSize, LIGHTGRAY);
        
        // Draw status text next to it
        DrawText(status, 150 + textW + 10, startY + (i*spacing), textSize, statusColor);
    }

    // Draw Summary
    int summaryY = startY + (objCount * spacing) + 40;
    DrawLine(100, summaryY - 20, screenWidth-100, summaryY - 20, DARKGRAY);
    
    char summary[100];
    sprintf(summary, "SUMMARY: %d / %d objectives reachable.", reachableCount, objCount);
    DrawText(summary, 150, summaryY, 30, WHITE);

    // Bottom prompt
    const char* prompt = "PRESS [ENTER] TO PLAY PATH";
    if (((int)(GetTime() * 2)) % 2 == 0) {
        DrawText(prompt, (screenWidth - MeasureText(prompt, 20))/2, screenHeight - 50, 20, GREEN);
    }
}

// Core algorithm functions
void DecodeIndex(size_t idx, int *r, int *c, int *m, int *mk, int cols, int maxMask) {
    size_t temp = idx;
    // idx = (r * cols * 4 * maxMask) + (c * 4 * maxMask) + (m * maxMask) + mk
    
    size_t stride_row = (size_t)cols * 4 * maxMask;
    size_t stride_col = (size_t)4 * maxMask;
    size_t stride_mode = (size_t)maxMask;

    *r = (int)(temp / stride_row);
    temp = temp % stride_row;

    *c = (int)(temp / stride_col);
    temp = temp % stride_col;

    *m = (int)(temp / stride_mode);
    *mk = (int)(temp % stride_mode);
}
void SolveTSP_Exact() {
    printf("\n--- Starting Path Calculation (Reachable Only) ---\n");
    ActiveTarget activeTargets[MAX_COLS * MAX_ROWS];
    int activeCount = 0;

    for (int i = 0; i < objCount; i++) {
        if (objectives[i].reachable) {
            activeTargets[activeCount].x = objectives[i].x;
            activeTargets[activeCount].y = objectives[i].y;
            activeTargets[activeCount].originalIdx = i;
            activeCount++;
        }
    }

    if (activeCount == 0) { printf("No reachable objectives.\n"); return; }
    // if (activeCount >= 15) { printf("WARNING: Count >= 15. Skipping.\n"); return; }

    // 2. Setup Memory
    if (tspDist) free(tspDist);
    if (tspParent) free(tspParent);
    if (tspPathTrace) { free(tspPathTrace); tspPathTrace = NULL; }
    tspStepCount = 0;
    
    printf("Calculating Shortest Path.\n");
    
    int maxMask = (1 << activeCount);
    size_t totalStates = (size_t)rows * cols * 4 * maxMask;
    
    tspDist = (int*)malloc(totalStates * sizeof(int));
    tspParent = (size_t*)malloc(totalStates * sizeof(size_t));

    if (!tspDist || !tspParent) return;

    for (size_t i = 0; i < totalStates; i++) {
        tspDist[i] = INT_MAX;
        tspParent[i] = SIZE_MAX;
    }
    MinHeap* pq = createMinHeap(10000);

    int startMask = 0;
    int startBody[6][2];
    GetCarBody(start_state.mode, startBody);

    for (int b = 0; b < 6; b++) {
        int cx = start_state.x + startBody[b][0];
        int cy = start_state.y + startBody[b][1];

        for (int i = 0; i < activeCount; i++) {
            if (activeTargets[i].x == cx && activeTargets[i].y == cy) {
                startMask |= (1 << i);
            }
        }
    }

    size_t startIdx = GET_IDX(start_state.y, start_state.x, start_state.mode, startMask);
    tspDist[startIdx] = 0;
    
    PQNode startNode = {start_state.x, start_state.y, start_state.mode, startMask, 0};
    pushHeap(pq, startNode);

    size_t finalStateIdx = SIZE_MAX;
    int finalMinCost = -1;

    // 3. Dijkstra Loop
    while (pq->size > 0) {
        PQNode u = popHeap(pq);
        size_t uIdx = GET_IDX(u.y, u.x, u.mode, u.mask);

        if (u.cost > tspDist[uIdx]) continue;

        if (u.mask == (maxMask - 1)) {
            finalMinCost = u.cost;
            finalStateIdx = uIdx;
            break; 
        }

        // Try 8 Movement Directions
        for (int i = 0; i < 8; i++) {
            int nextMode = Mode_Movement_Fuel[u.mode][i][0];
            int dx = Mode_Movement_Fuel[u.mode][i][1];
            int dy = Mode_Movement_Fuel[u.mode][i][2];
            int fuel = Mode_Movement_Fuel[u.mode][i][3];

            int nx = u.x + dx;
            int ny = u.y + dy;

            if (nx >= 0 && nx < cols && ny >= 0 && ny < rows && maze[ny][nx] != 0) {
                if (CheckCarCollision(nx, ny, nextMode)) {
                    
                    int newCost = u.cost + fuel;
                    int newMask = u.mask;

                    int body[6][2];
                    GetCarBody(nextMode, body);
                    for (int b = 0; b < 6; b++) {
                        int cx = nx + body[b][0];
                        int cy = ny + body[b][1];

                        for (int k = 0; k < activeCount; k++) {
                            if (activeTargets[k].x == cx && activeTargets[k].y == cy) {
                                newMask |= (1 << k);
                            }
                        }
                    }

                    size_t vIdx = GET_IDX(ny, nx, nextMode, newMask);
                    
                    if (newCost < tspDist[vIdx]) {
                        tspDist[vIdx] = newCost;
                        tspParent[vIdx] = uIdx;
                        PQNode v = {nx, ny, nextMode, newMask, newCost};
                        pushHeap(pq, v);
                    }
                }
            }
        }
    }

    // 4. Output & Path Reconstruction
    if (finalMinCost != -1) {
        totalFuelCost = finalMinCost; // Save the cost to the global variable
        printf("SUCCESS: Optimal path found! Total Fuel: %d\n", finalMinCost);
        
        tspPathTrace = (PathStep*)malloc(sizeof(PathStep) * (rows * cols * 4 * activeCount)); 
        int tempCount = 0;
        size_t curr = finalStateIdx;

        while (curr != SIZE_MAX) {
            int r, c, m, mk;
            DecodeIndex(curr, &r, &c, &m, &mk, cols, maxMask);
            tspPathTrace[tempCount].x = c;
            tspPathTrace[tempCount].y = r;
            tspPathTrace[tempCount].m = m;
            tempCount++;
            curr = tspParent[curr];
        }

        tspStepCount = tempCount;
    } else {
        printf("FAILURE: Could not reach all active targets.\n");
    }
    printf("-------------------------------\n");

    freeHeap(pq);
}
void DrawPathPlayback() {
    // 1. Draw the base maze
    DrawMazeGrid(); 
    
    // Hide the previous instruction text from DrawMazeGrid by drawing a black box over it
    DrawRectangle(0, 0, screenWidth, 40, BLACK);
    DrawText("PATH VISUALIZATION", 10, 10, 20, YELLOW);

    if (!solvedTSP || tspPathTrace == NULL || tspStepCount == 0) {
        return;
    }

    // 2. Determine which step to draw
    // tspPathTrace is stored End -> Start, so we iterate in reverse for playback
    int traceIndex = tspStepCount - 1 - currentPlaybackStep;

    if (traceIndex >= 0) {
        PathStep step = tspPathTrace[traceIndex];
        
        // 3. Draw the Car Body
        int body[6][2];
        GetCarBody(step.m, body);

        for(int i=0; i<6; i++) {
            int cx = step.x + body[i][0];
            int cy = step.y + body[i][1];
            
            int drawX = offsetX + cx * cellSize;
            int drawY = offsetY + cy * cellSize;
            
            Color carColor;
            if (body[i][0] == 0 && body[i][1] == 0) {
                carColor = DARKPURPLE;   // Driver/Reference point marked in Red
            } else {
                carColor = PURPLE; // Rest of the body
            }

            DrawRectangle(drawX + 2, drawY + 2, cellSize - 4, cellSize - 4, carColor);
        }
        
        DrawText(TextFormat("Step: %d / %d", currentPlaybackStep + 1, tspStepCount), 10, 40, 20, WHITE);
        DrawText(TextFormat("Total Fuel Cost: %d", totalFuelCost), 10, 70, 20, WHITE);
    } else {
        DrawText("Path Completed!", 10, 40, 20, GREEN);
    }

    const char* prompt = "PRESS [ENTER] TO EXIT";
    DrawText(prompt, screenWidth - MeasureText(prompt, 20) - 20, 10, 20, GRAY);
}


int main(void) {
    SetConfigFlags(FLAG_VSYNC_HINT | FLAG_WINDOW_HIGHDPI);
    InitWindow(screenWidth, screenHeight, "Pathfinder GUI");
    
    mazeLoaded = false;
    accessChecked = false;
    solvedTSP = false;
    
    // read input
    if(!mazeLoaded) mazeLoaded = LoadMaze("input.txt");
    
    // init state
    AppScreen currentScreen = StartMenu;

    while (!WindowShouldClose()) {
        // State control logic
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
        else if (currentScreen == AccessibilityCheck) {
            if(!accessChecked) {
                CheckAccessibility();
                accessChecked = true;
                if (!solvedTSP) {
                    if (reachableCount > 0) {
                        SolveTSP_Exact();
                    } else {
                        printf("No reachable objectives to solve.\n");
                    }
                    solvedTSP = true;
                }
            }
            if (IsKeyPressed(KEY_ENTER)) {
                // Modified: Transition to Playback instead of breaking
                if (solvedTSP && tspStepCount > 0) {
                    currentScreen = PathPlayback;
                    currentPlaybackStep = 0;
                    playbackFrameCounter = 0;
                    playbackFinished = false;
                } else {
                    break;
                }
            }
        }
        else if (currentScreen == PathPlayback) {
            // Animation logic
            if (!playbackFinished) {
                playbackFrameCounter++;
                if (playbackFrameCounter >= 10) { // Update every 10 frames
                    if (currentPlaybackStep < tspStepCount - 1) {
                        currentPlaybackStep++;
                    } else {
                        playbackFinished = true;
                    }
                    playbackFrameCounter = 0;
                }
            }
            
            if (IsKeyPressed(KEY_ENTER)) {
                break; // Exit app
            }
        }

        BeginDrawing();
            ClearBackground(BLACK);
            
            switch(currentScreen) {
                case StartMenu:
                    DrawGradientTitle();
                    DrawCredits();
                    DrawBlinkHint();
                    break;
                    
                case MazeConfirm:
                    DrawMazeGrid();
                    break;
                    
                case AccessibilityCheck:
                    DrawAccessibilityResults();
                    break;
                    
                case PathPlayback:
                    DrawPathPlayback();
                    break;
            }

        EndDrawing();
    }

    free(tspDist);
    free(tspParent);
    free(tspPathTrace);
    CloseWindow();

    return 0;
}