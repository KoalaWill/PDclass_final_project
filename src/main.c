#include "raylib.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <stdint.h> 

#include "resource_dir.h"

// Constant definitions
#define MAX_COLS 30
#define MAX_ROWS 30
#define MAX_OBJ_COUNT 100
#define INIT_HEAP_CAPACITY 4000
#define PLAYBACK_FRAME_INTERVAL 10

// Coordinate index macros (reused)
#define GET_IDX(r, c, m, mk, cols, maxMask) \
    ((size_t)(r) * cols * 4 * maxMask + (size_t)(c) * 4 * maxMask + (size_t)(m) * maxMask + mk)
#define IDX_POS(r, c, m, cols) \
    ((size_t)(r) * cols * 4 + (size_t)(c) * 4 + m)

// Movement rules: [mode][dir][0:new_mode, 1:dx, 2:dy, 3:fuel]
const int Mode_Movement_Fuel[4][8][4] = {
    {{0,0,1,1},{0,0,-1,1},{0,1,0,3},{0,-1,0,3},{1,2,0,3},{1,1,1,3},{3,0,2,3},{3,-1,1,3}},
    {{1,1,0,1},{1,-1,0,1},{1,0,-1,3},{1,0,1,3},{2,0,2,3},{2,-1,1,3},{0,-2,0,3},{0,-1,-1,3}},
    {{2,0,-1,1},{2,0,1,1},{2,-1,0,3},{2,1,0,3},{3,-2,0,3},{3,-1,-1,3},{1,0,-2,3},{1,1,-1,3}},
    {{3,-1,0,1},{3,1,0,1},{3,0,1,3},{3,0,-1,3},{0,0,-2,3},{0,1,-1,3},{2,2,0,3},{2,1,1,3}}
};

// Application state machine
typedef enum { 
    StartMenu = 0, 
    MazeConfirm,
    AccessibilityCheck,
    PathPlayback
} AppScreen;

// Data structure definitions
// Queue for BFS accessibility check
typedef struct {
    int x, y, mode;
} State;
typedef struct {
    State *items;
    int head, tail;
    int capacity;
} Queue;

// Priority queue node for Dijkstra/TSP
typedef struct {
    int x, y;
    int mode;
    int mask;   // Bitmask: marks visited targets
    int cost;   // Accumulated fuel cost
} PQNode;

// Min-Heap implementation
typedef struct {
    PQNode *nodes;
    int size;
    int capacity;
} MinHeap;

// Path step for playback/stitching
typedef struct { int x, y, m; } PathStep;

// Objective structure
typedef struct { int x, y; bool reachable; } Objective;

// Active target for TSP
typedef struct { 
    int x, y; 
    int originalIdx; 
} ActiveTarget;

// Global variables (grouped by function)
// Window related
const int screenWidth = 1280;
const int screenHeight = 800;
int currentX, currentY;

// Maze related
int **maze = NULL;
int rows = 0, cols = 0;
bool mazeLoaded = false;
int mazeDisplayMargin, availableWidth, availableHeight, cellSize;
int mazePixelWidth, mazePixelHeight, offsetX, offsetY;

// Accessibility check related
Queue* q = NULL;
State start_state;
Objective objectives[MAX_OBJ_COUNT]; 
int objCount = 0;
bool visited[MAX_ROWS][MAX_COLS][4];
int reachableCount = 0;
bool accessChecked = false;

// TSP related
int *tspDist = NULL;
size_t *tspParent = NULL;
PathStep* tspPathTrace = NULL;
int tspStepCount = 0;
bool solvedTSP = false;
int totalFuelCost = 0;

// Playback related
int currentPlaybackStep = 0;
int playbackFrameCounter = 0;
bool playbackFinished = false;

// Helper: Queue operations
Queue* createQueue(int capacity) {
    Queue* q = (Queue*)malloc(sizeof(Queue));
    q->items = (State*)malloc(sizeof(State) * capacity);
    q->head = q->tail = 0;
    q->capacity = capacity;
    return q;
}
bool isQueueEmpty(Queue* q) { return q->head == q->tail; }
void enqueue(Queue* q, State s) { q->items[q->tail++] = s; }
State dequeue(Queue* q) { return q->items[q->head++]; }
void freeQueue(Queue* q) { free(q->items); free(q); }

// Helper: Min-Heap operations
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
void freeHeap(MinHeap* h) { free(h->nodes); free(h); }

// Helper: Get car body coordinates
void GetCarBody(int mode, int body[6][2]) {
    switch(mode) {
        case 0: { int b[6][2]={{0,0},{1,0},{0,1},{1,1},{0,2},{1,2}}; memcpy(body, b, sizeof(b)); break; }
        case 1: { int b[6][2]={{0,0},{-1,0},{-2,0},{0,1},{-1,1},{-2,1}}; memcpy(body, b, sizeof(b)); break; }
        case 2: { int b[6][2]={{0,0},{-1,0},{0,-1},{-1,-1},{0,-2},{-1,-2}}; memcpy(body, b, sizeof(b)); break; }
        case 3: { int b[6][2]={{0,0},{1,0},{2,0},{0,-1},{1,-1},{2,-1}}; memcpy(body, b, sizeof(b)); break; }
    }
}

// Helper: Check car collision
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

// Helper: Generic Dijkstra
int Dijkstra(int startX, int startY, int startMode, int targetX, int targetY, PathStep** outPath, int* outStepCount) {
    size_t totalStates = (size_t)rows * cols * 4;
    int *dist = (int*)malloc(totalStates * sizeof(int));
    size_t *parent = (size_t*)malloc(totalStates * sizeof(size_t));
    
    for(size_t i=0; i<totalStates; i++) {
        dist[i] = INT_MAX;
        parent[i] = SIZE_MAX;
    }

    MinHeap* pq = createMinHeap(INIT_HEAP_CAPACITY);
    size_t startIdx = IDX_POS(startY, startX, startMode, cols);
    dist[startIdx] = 0;
    pushHeap(pq, (PQNode){startX, startY, startMode, 0, 0});

    int finalCost = -1;
    size_t endStateIdx = SIZE_MAX;

    while(pq->size > 0) {
        PQNode u = popHeap(pq);
        size_t uIdx = IDX_POS(u.y, u.x, u.mode, cols);
        if(u.cost > dist[uIdx]) continue;

        // Check if target reached (body covers target)
        int body[6][2];
        GetCarBody(u.mode, body);
        bool hit = false;
        for(int b=0; b<6; b++) {
            if((u.x + body[b][0]) == targetX && (u.y + body[b][1]) == targetY) {
                hit = true; break;
            }
        }

        if(hit) {
            finalCost = u.cost;
            endStateIdx = uIdx;
            break; 
        }

        // Iterate all movement directions
        for(int i=0; i<8; i++) {
            int nextMode = Mode_Movement_Fuel[u.mode][i][0];
            int dx = Mode_Movement_Fuel[u.mode][i][1];
            int dy = Mode_Movement_Fuel[u.mode][i][2];
            int fuel = Mode_Movement_Fuel[u.mode][i][3];
            int nx = u.x + dx;
            int ny = u.y + dy;

            if(nx >= 0 && nx < cols && ny >= 0 && ny < rows && maze[ny][nx] != 0) {
                if(CheckCarCollision(nx, ny, nextMode)) {
                    int newCost = u.cost + fuel;
                    size_t vIdx = IDX_POS(ny, nx, nextMode, cols);
                    if(newCost < dist[vIdx]) {
                        dist[vIdx] = newCost;
                        parent[vIdx] = uIdx;
                        pushHeap(pq, (PQNode){nx, ny, nextMode, 0, newCost});
                    }
                }
            }
        }
    }

    // Reconstruct path (if output needed)
    if(outPath && outStepCount && finalCost != -1) {
        PathStep tempBuff[2000]; 
        int steps = 0;
        size_t curr = endStateIdx;

        // Backtrack path (end to start)
        while(curr != startIdx && curr != SIZE_MAX) {
            int r = (curr / 4) / cols;
            int c = (curr / 4) % cols;
            int m = curr % 4;
            tempBuff[steps++] = (PathStep){c, r, m};
            curr = parent[curr];
        }
        
        // Reverse path (start to end)
        *outStepCount = steps;
        *outPath = (PathStep*)malloc(sizeof(PathStep) * steps);
        for(int k = steps - 1; k >= 0; k--) {
            (*outPath)[steps - 1 - k] = tempBuff[k];
        }
    }

    free(dist);
    free(parent);
    freeHeap(pq);
    return finalCost;
}

// UI drawing functions
void DrawGradientTitle() {
    const char *ascii_art[] = {
        " ________  ________  _________  ___  ___  ________ ___  ________   ________  _______   ________   ",
        "|\\   __  \\|\\   __  \\|\\___   ___\\\\  \\|\\  \\|\\  _____\\\\  \\|\\   ___  \\|\\   ___ \\|\\   ____\\|\\   __  \\  ",
        "\\ \\  \\|\\  \\ \\  \\|\\  \\|___ \\  \\_\\ \\  \\\\\\  \\ \\  \\___| \\  \\ \\  \\\\ \\  \\ \\  \\_|\\ \\ \\  \\___|\\ \\  \\|\\  \\ ",
        " \\ \\   ____\\ \\   __  \\   \\ \\  \\ \\ \\   __  \\ \\   __\\\\ \\  \\ \\  \\\\ \\  \\ \\  \\ \\\\ \\ \\   ____\\ \\   __  _\\",
        "  \\ \\  \\___|\\ \\  \\ \\  \\   \\ \\  \\ \\ \\  \\ \\  \\ \\  \\_| \\ \\  \\ \\  \\\\ \\  \\ \\  \\_\\\\ \\ \\  \\___|\\ \\  \\ \\|",
        "   \\ \\__\\    \\ \\__\\ \\__\\   \\ \\__\\ \\ \\__\\ \\__\\ \\__\\   \\ \\__\\ \\__\\\\ \\__\\ \\_______\\ \\_______\\ \\_\\ \\_\\",
        "    \\ |__|     \\|__|\\|__|    \\|__|  \\|__|\\|__|\\|__|    \\|__|\\|__| \\|__|\\|_______|\\|_______|\\|_|\\|_|"
    };
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
            Color drawColor = {
                (unsigned char)(startColor.r + t * (endColor.r - startColor.r)),
                (unsigned char)(startColor.g + t * (endColor.g - startColor.g)),
                (unsigned char)(startColor.b + t * (endColor.b - startColor.b)),
                255
            };
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

// Maze loading & drawing
bool LoadMaze(const char *filename) {
    rows = 0; cols = 0;
    bool col_calculated = false;
    int ch;
    
    FILE *inf = fopen(filename, "r");
    if (inf == NULL) return false;
    
    // Count rows and columns
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
    
    // Allocate maze memory
    maze = (int **)malloc(rows * sizeof(int *));
    for (int i = 0; i < rows; i++) {
        maze[i] = (int *)malloc(cols * sizeof(int));
    }
    
    // Read maze data
    rewind(inf);
    for (int i = 0; i < rows; i++) {
        for (int j = 0; j < cols; j++) {
            if (fscanf(inf, "%1d", &maze[i][j]) != 1) maze[i][j] = 5;
            if(maze[i][j] == 2) {
                start_state.x = j;
                start_state.y = i;
                start_state.mode = 0;
                // Adjust start mode based on car length
                if (j >= 2 && maze[i][j-1] == 2 && maze[i][j-2] == 2) {
                    start_state.mode = 3;
                    start_state.x = j-2;
                } else if (j >= 1 && maze[i][j-1] == 2) {
                    start_state.mode = 2;
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
                case 0: c = DARKGRAY; break;
                case 1: c = LIME; break;
                case 2: c = MAROON; break;
                case 3: c = BLUE; break;
                default: c = BLACK;
            }

            DrawRectangle(x, y, cellSize, cellSize, c);
            DrawRectangleLines(x, y, cellSize, cellSize, BLACK);
        }
    }
    DrawText(TextFormat("Maze input detected, maze size: %d(rows) x %d(columns)", rows, cols), 10, screenHeight - 30, 20, GRAY);
    DrawText("PRESS [ENTER] TO CONFIRM MAZE MAP", 10, 10, 20, LIGHTGRAY);
}

// Accessibility check (BFS)
void CheckAccessibility() {
    q = createQueue(MAX_ROWS * MAX_COLS * 4);
    objCount=0;
    memset(visited, 0, sizeof(visited));
    reachableCount = 0;

    // Collect all objectives
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
    
    // Enqueue initial state
    if (CheckCarCollision(start_state.x, start_state.y, start_state.mode)) {
        visited[start_state.y][start_state.x][0] = true;
        enqueue(q, start_state);
    }
    
    // BFS core logic
    while (!isQueueEmpty(q)) {
        State current = dequeue(q);
        
        // Mark reachable objectives
        if (maze[current.y][current.x] == 3) {
             for(int i=0; i<objCount; i++) {
                if(objectives[i].x == current.x && objectives[i].y == current.y && !objectives[i].reachable) {
                    objectives[i].reachable = true;
                }
            }
        }

        // Iterate all movement directions
        for (int i = 0; i < 8; i++) {
            int nextMode = Mode_Movement_Fuel[current.mode][i][0];
            int dx = Mode_Movement_Fuel[current.mode][i][1];
            int dy = Mode_Movement_Fuel[current.mode][i][2];
            int nx = current.x + dx;
            int ny = current.y + dy;

            if (nx < 0 || nx >= cols || ny < 0 || ny >= rows) continue;
            if (!visited[ny][nx][nextMode] && CheckCarCollision(nx, ny, nextMode)) {
                visited[ny][nx][nextMode] = true;
                enqueue(q, (State){nx, ny, nextMode});
            }
        }
    }
    
    // Count reachable objectives
    for(int i=0; i<objCount; i++) {
        if(objectives[i].reachable) reachableCount++;
        else maze[objectives[i].y][objectives[i].x] = 1;
    }

    freeQueue(q);
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
        int posX = i%2 == 0 ? 150 : 700;
        const char* status = objectives[i].reachable ? "ACCESSIBLE" : "UNREACHABLE";
        Color statusColor = objectives[i].reachable ? LIME : RED;
        
        char buffer[100];
        sprintf(buffer, "Objective at (%d, %d): ", objectives[i].x, objectives[i].y);
        
        int textW = MeasureText(buffer, textSize);
        DrawText(buffer, posX, startY + (i/2*spacing), textSize, LIGHTGRAY);
        DrawText(status, posX + textW + 10, startY + (i/2*spacing), textSize, statusColor);
    }

    int summaryY = startY + ((objCount/2+1) * spacing) + 40;
    DrawLine(100, summaryY - 20, screenWidth-100, summaryY - 20, DARKGRAY);
    
    char summary[100];
    sprintf(summary, "SUMMARY: %d / %d objectives reachable.", reachableCount, objCount);
    DrawText(summary, 150, summaryY, 30, WHITE);

    const char* prompt = "PRESS [ENTER] TO PLAY PATH";
    if (((int)(GetTime() * 2)) % 2 == 0) {
        DrawText(prompt, (screenWidth - MeasureText(prompt, 20))/2, screenHeight - 50, 20, GREEN);
    }
}

// Exact TSP (Bitmask Dijkstra)
void DecodeIndex(size_t idx, int *r, int *c, int *m, int *mk, int cols, int maxMask) {
    size_t temp = idx;
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
    printf("\n--- Starting Exact TSP (Reachable Only) ---\n");
    ActiveTarget activeTargets[MAX_COLS * MAX_ROWS];
    int activeCount = 0;

    // Collect reachable objectives
    for (int i = 0; i < objCount; i++) {
        if (objectives[i].reachable) {
            activeTargets[activeCount].x = objectives[i].x;
            activeTargets[activeCount].y = objectives[i].y;
            activeTargets[activeCount].originalIdx = i;
            activeCount++;
        }
    }

    if (activeCount == 0) { printf("No reachable objectives.\n"); return; }

    // Free old data
    if (tspDist) free(tspDist);
    if (tspParent) free(tspParent);
    if (tspPathTrace) { free(tspPathTrace); tspPathTrace = NULL; }
    tspStepCount = 0;
    
    // Init bitmask arrays
    int maxMask = (1 << activeCount);
    size_t totalStates = (size_t)rows * cols * 4 * maxMask;
    tspDist = (int*)malloc(totalStates * sizeof(int));
    tspParent = (size_t*)malloc(totalStates * sizeof(size_t));

    if (!tspDist || !tspParent) return;

    for (size_t i = 0; i < totalStates; i++) {
        tspDist[i] = INT_MAX;
        tspParent[i] = SIZE_MAX;
    }

    // Init priority queue
    MinHeap* pq = createMinHeap(INIT_HEAP_CAPACITY);
    int startMask = 0;
    int startBody[6][2];
    GetCarBody(start_state.mode, startBody);

    // Calculate initial mask (start covers targets)
    for (int b = 0; b < 6; b++) {
        int cx = start_state.x + startBody[b][0];
        int cy = start_state.y + startBody[b][1];
        for (int i = 0; i < activeCount; i++) {
            if (activeTargets[i].x == cx && activeTargets[i].y == cy) {
                startMask |= (1 << i);
            }
        }
    }

    // Enqueue start
    size_t startIdx = GET_IDX(start_state.y, start_state.x, start_state.mode, startMask, cols, maxMask);
    tspDist[startIdx] = 0;
    pushHeap(pq, (PQNode){start_state.x, start_state.y, start_state.mode, startMask, 0});

    size_t finalStateIdx = SIZE_MAX;
    int finalMinCost = -1;

    // Dijkstra core (Bitmask)
    while (pq->size > 0) {
        PQNode u = popHeap(pq);
        size_t uIdx = GET_IDX(u.y, u.x, u.mode, u.mask, cols, maxMask);

        if (u.cost > tspDist[uIdx]) continue;
        if (u.mask == (maxMask - 1)) { // All targets visited
            finalMinCost = u.cost;
            finalStateIdx = uIdx;
            break; 
        }

        // Iterate all movement directions
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

                    // Update mask (new position covers targets)
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

                    size_t vIdx = GET_IDX(ny, nx, nextMode, newMask, cols, maxMask);
                    if (newCost < tspDist[vIdx]) {
                        tspDist[vIdx] = newCost;
                        tspParent[vIdx] = uIdx;
                        pushHeap(pq, (PQNode){nx, ny, nextMode, newMask, newCost});
                    }
                }
            }
        }
    }

    // Reconstruct path
    if (finalMinCost != -1) {
        totalFuelCost = finalMinCost;
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

    freeHeap(pq);
}

// Approx TSP (Christofides Algorithm)
int GetSimpleDistance(int sx, int sy, int tx, int ty) {
    int startMode = 0;
    if(!CheckCarCollision(sx, sy, 0)) {
        for(int m=0; m<4; m++) if(CheckCarCollision(sx, sy, m)) { startMode=m; break; }
    }
    return Dijkstra(sx, sy, startMode, tx, ty, NULL, NULL);
}

void GetMST(int nodeCount, int *costMatrix, int *parentOut) {
    int *key = (int*)malloc(nodeCount * sizeof(int));
    bool *mstSet = (bool*)malloc(nodeCount * sizeof(bool));
    for (int i = 0; i < nodeCount; i++) { key[i] = INT_MAX; mstSet[i] = false; }
    key[0] = 0; parentOut[0] = -1;

    for (int count = 0; count < nodeCount - 1; count++) {
        int min = INT_MAX, u = -1;
        for (int v = 0; v < nodeCount; v++) {
            if (!mstSet[v] && key[v] < min) { min = key[v]; u = v; }
        }
        if (u == -1) break; 
        mstSet[u] = true;
        for (int v = 0; v < nodeCount; v++) {
            int weight = costMatrix[u * nodeCount + v];
            if (weight && !mstSet[v] && weight < key[v]) {
                parentOut[v] = u; key[v] = weight;
            }
        }
    }
    free(key); free(mstSet);
}

void FindEulerTour(int nodeCount, int *adjMatrix, int *circuit, int *circuitSize) {
    int *stack = (int*)malloc(nodeCount * nodeCount * sizeof(int)); 
    int top = -1;
    stack[++top] = 0;
    int pathIdx = 0;
    
    int *tempAdj = (int*)malloc(nodeCount * nodeCount * sizeof(int));
    memcpy(tempAdj, adjMatrix, nodeCount * nodeCount * sizeof(int));

    while(top >= 0) {
        int curr_v = stack[top];
        int neighbor = -1;
        for(int i=0; i<nodeCount; i++) {
            if(tempAdj[curr_v * nodeCount + i] > 0) { neighbor = i; break; }
        }
        if(neighbor != -1) {
            tempAdj[curr_v * nodeCount + neighbor]--;
            tempAdj[neighbor * nodeCount + curr_v]--;
            stack[++top] = neighbor;
        } else {
            circuit[pathIdx++] = stack[top--];
        }
    }
    *circuitSize = pathIdx;
    free(stack);
    free(tempAdj);
}

int StitchPath(int startX, int startY, int startMode, int targetX, int targetY) {
    PathStep* tempPath = NULL;
    int tempStepCount = 0;
    int cost = Dijkstra(startX, startY, startMode, targetX, targetY, &tempPath, &tempStepCount);

    if(cost != -1 && tempPath) {
        // Stitch path to global trace
        tspPathTrace = (PathStep*)realloc(tspPathTrace, sizeof(PathStep) * (tspStepCount + tempStepCount));
        memcpy(tspPathTrace + tspStepCount, tempPath, sizeof(PathStep) * tempStepCount);
        tspStepCount += tempStepCount;
        free(tempPath);
    }

    return cost;
}

void SolveTSP_Approx() {
    printf("\n--- Starting Approximate TSP ---\n");
    ActiveTarget activeTargets[MAX_COLS * MAX_ROWS];
    int activeCount = 0;

    // Collect reachable objectives
    for (int i = 0; i < objCount; i++) {
        if (objectives[i].reachable) {
            activeTargets[activeCount].x = objectives[i].x;
            activeTargets[activeCount].y = objectives[i].y;
            activeTargets[activeCount].originalIdx = i;
            activeCount++;
        }
    }
    if(activeCount == 0) return;

    // Build node list (start + targets + dummy)
    int numRealNodes = activeCount + 1;
    int totalNodes = numRealNodes + 1;
    ActiveTarget allNodes[MAX_COLS * MAX_ROWS];
    allNodes[0].x = start_state.x; 
    allNodes[0].y = start_state.y;
    for(int i=0; i<activeCount; i++) allNodes[i+1] = activeTargets[i];

    // Build cost matrix
    int *costMat = (int*)malloc(totalNodes * totalNodes * sizeof(int));
    memset(costMat, 0, totalNodes * totalNodes * sizeof(int));
    for(int i=0; i<numRealNodes; i++) {
        for(int j=i+1; j<numRealNodes; j++) {
            int c = GetSimpleDistance(allNodes[i].x, allNodes[i].y, allNodes[j].x, allNodes[j].y);
            costMat[i*totalNodes + j] = c;
            costMat[j*totalNodes + i] = c;
        }
    }

    // Connect dummy node (handle odd degrees)
    int dummy = totalNodes - 1;
    costMat[0 * totalNodes + dummy] = 999999;
    costMat[dummy * totalNodes + 0] = 999999;
    for(int i=1; i<numRealNodes; i++) {
        costMat[i * totalNodes + dummy] = 0;
        costMat[dummy * totalNodes + i] = 0;
    }

    // Build MST
    int *mstParent = (int*)malloc(totalNodes * sizeof(int));
    GetMST(totalNodes, costMat, mstParent);

    // Build multigraph (MST + Min Matching)
    int *multiGraph = (int*)calloc(totalNodes * totalNodes, sizeof(int));
    int *degrees = (int*)calloc(totalNodes, sizeof(int));
    for(int i=1; i<totalNodes; i++) {
        int u = i; int v = mstParent[i];
        if(v != -1) {
            multiGraph[u*totalNodes + v]++; 
            multiGraph[v*totalNodes + u]++;
            degrees[u]++; 
            degrees[v]++;
        }
    }

    // Min matching (handle odd degree nodes)
    int *odds = (int*)malloc(totalNodes * sizeof(int));
    int oddCount = 0;
    for(int i=0; i<totalNodes; i++) if(degrees[i] % 2 != 0) odds[oddCount++] = i;
    
    bool *matched = (bool*)calloc(totalNodes, sizeof(bool));
    for(int i=0; i<oddCount; i++) {
        int u = odds[i];
        if(matched[u]) continue;
        int minW = INT_MAX, bestV = -1;
        for(int j=i+1; j<oddCount; j++) {
            int v = odds[j];
            if(!matched[v] && costMat[u*totalNodes + v] < minW) {
                minW = costMat[u*totalNodes + v]; 
                bestV = v;
            }
        }
        if(bestV != -1) {
            matched[u] = true; 
            matched[bestV] = true;
            multiGraph[u*totalNodes + bestV]++; 
            multiGraph[bestV*totalNodes + u]++;
        }
    }

    // Find Euler Tour
    int *circuit = (int*)malloc(totalNodes * 2 * sizeof(int));
    int circuitSize = 0;
    FindEulerTour(totalNodes, multiGraph, circuit, &circuitSize);

    // Extract TSP path (remove duplicates)
    int *visitOrder = (int*)malloc(totalNodes * sizeof(int));
    bool *visitedMap = (bool*)calloc(totalNodes, sizeof(bool));
    int orderCount = 0;
    for(int i=circuitSize-1; i>=0; i--) {
        int node = circuit[i];
        if(node == dummy) continue;
        if(!visitedMap[node]) {
            visitedMap[node] = true;
            visitOrder[orderCount++] = node;
        }
    }

    // Stitch physical path
    tspPathTrace = (PathStep*)malloc(sizeof(PathStep) * (rows * cols * 4 * activeCount * 5)); 
    tspStepCount = 0;
    totalFuelCost = 0;

    int curX = start_state.x;
    int curY = start_state.y;
    int curM = start_state.mode;
    tspPathTrace[tspStepCount++] = (PathStep){curX, curY, curM};

    for(int i=1; i<orderCount; i++) {
        int targetNodeIdx = visitOrder[i]; 
        int tX = allNodes[targetNodeIdx].x;
        int tY = allNodes[targetNodeIdx].y;
        int legCost = StitchPath(curX, curY, curM, tX, tY);
        
        if(legCost != -1) {
            totalFuelCost += legCost;
            PathStep last = tspPathTrace[tspStepCount-1];
            curX = last.x;
            curY = last.y;
            curM = last.m;
        }
    }

    // Reverse path (fit playback logic)
    for(int i = 0; i < tspStepCount / 2; i++) {
        PathStep temp = tspPathTrace[i];
        tspPathTrace[i] = tspPathTrace[tspStepCount - 1 - i];
        tspPathTrace[tspStepCount - 1 - i] = temp;
    }

    printf("Approximation Complete. Total Steps: %d, Cost: %d\n", tspStepCount, totalFuelCost);

    // Free memory
    free(costMat);
    free(mstParent);
    free(multiGraph);
    free(degrees);
    free(odds);
    free(matched);
    free(circuit);
    free(visitOrder);
    free(visitedMap);
}

// Draw path playback
void DrawPathPlayback() {
    DrawMazeGrid(); 
    DrawRectangle(0, 0, screenWidth, 40, BLACK);
    DrawText("PATH VISUALIZATION", 10, 10, 20, YELLOW);

    if (!solvedTSP || tspPathTrace == NULL || tspStepCount == 0) return;

    // Calculate current playback step index
    int traceIndex = tspStepCount - 1 - currentPlaybackStep;
    if (traceIndex >= 0) {
        PathStep step = tspPathTrace[traceIndex];
        int body[6][2];
        GetCarBody(step.m, body);

        // Draw vehicle
        for(int i=0; i<6; i++) {
            int cx = step.x + body[i][0];
            int cy = step.y + body[i][1];
            int drawX = offsetX + cx * cellSize;
            int drawY = offsetY + cy * cellSize;
            
            Color carColor = (body[i][0] == 0 && body[i][1] == 0) ? DARKPURPLE : PURPLE;
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

// Main function
int main(void) {
    SetConfigFlags(FLAG_VSYNC_HINT | FLAG_WINDOW_HIGHDPI);
    InitWindow(screenWidth, screenHeight, "Pathfinder GUI");
    
    // Load maze
    mazeLoaded = LoadMaze("input.txt");
    AppScreen currentScreen = StartMenu;

    while (!WindowShouldClose()) {
        // State machine control
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
                if (reachableCount > 0) {
                    // Select algorithm based on objective count
                    if (reachableCount < 15) SolveTSP_Exact();
                    else SolveTSP_Approx();
                } else {
                    printf("No reachable objectives to solve.\n");
                }
                solvedTSP = true;
            }
            if (IsKeyPressed(KEY_ENTER)) {
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
            // Playback animation logic
            if (!playbackFinished) {
                playbackFrameCounter++;
                if (playbackFrameCounter >= PLAYBACK_FRAME_INTERVAL) {
                    currentPlaybackStep = (currentPlaybackStep < tspStepCount - 1) ? currentPlaybackStep + 1 : currentPlaybackStep;
                    playbackFinished = (currentPlaybackStep >= tspStepCount - 1);
                    playbackFrameCounter = 0;
                }
            }
            if (IsKeyPressed(KEY_ENTER)) break;
        }

        // Drawing logic
        BeginDrawing();
            ClearBackground(BLACK);
            switch(currentScreen) {
                case StartMenu:
                    DrawGradientTitle();
                    DrawCredits();
                    DrawBlinkHint();
                    break;
                case MazeConfirm: DrawMazeGrid(); break;
                case AccessibilityCheck: DrawAccessibilityResults(); break;
                case PathPlayback: DrawPathPlayback(); break;
            }
        EndDrawing();
    }

    // Memory cleanup
    free(tspDist);
    free(tspParent);
    free(tspPathTrace);
    for (int i = 0; i < rows; i++) free(maze[i]);
    free(maze);
    CloseWindow();

    return 0;
}