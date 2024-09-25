#include "SDL/SDL.h"
#include "SDL/SDL_image.h"
#include <string.h>
#include <stdio.h>
#include <math.h>

#define CHAR_WIDTH 15
#define CHAR_HEIGHT 18
#define CHAR_SPACING 3
#define LINE_SPACING 3
#define NLINE_SPACING 4
#define FONT_COLUMNS 16
#define VP_WIDTH 22
#define CO_WIDTH 10
#define UP_SHARE 20
#define DN_SHARE 10
#define RESO_X 1024
#define RESO_Y 768
#define MAP_WIDTH 30
#define MAP_HEIGHT 24
#define VIEW_DEPTH 3
#define VIEW_WIDTH 9
#define TARGET_FPS 60
#define MOVE_DURATION 200    
#define ROTATE_DURATION 200
#define FRAME_DELAY (1000 / TARGET_FPS)
#define M_PI 3.14159265358979323846
#define ATLAS_COLUMNS 8
#define TILE_SIZE 32
#define NUM_TEX 62

// Tile byte masks
#define TILE_TYPE_MASK        0xC0 // Bits 7-6
#define TILE_TYPE_FLOOR       0x00 // 00 in bits 6-7
#define TILE_TYPE_WALL        0x40 // 01 in bits 6-7
#define TILE_TYPE_HALF_FLOOR  0x80 // 10 in bits 6-7
#define TILE_TYPE_HALF_WALL   0xC0 // 11 in bits 6-7
#define TEXTURE_INDEX_MASK    0x3F // Bits 5-0
#define EVENT_TYPE_MASK       0xE0 // Bits 7-5
#define EVENT_ID_MASK         0x1F // Bits 4-0

// Define display modes
typedef enum {
    DISPLAY_MODE_RAYCASTER,
    DISPLAY_MODE_TOPDOWN,
    DISPLAY_MODE_ART,
    DISPLAY_MODE_WIDE_ART
} DisplayMode;

// Set default display mode
DisplayMode currentDisplayMode = DISPLAY_MODE_RAYCASTER;

SDL_Surface* tileTextures[NUM_TEX];

// Visual position and direction (for raycaster)
double playerX = 12.5; // Interpolated X position (centered in the cell)
double playerY = 12.5; // Interpolated Y position
double dirAngle = 0.0; // Direction angle in radians (0 = North)

// Movement and rotation animation state (for raycaster)
int isMoving = 0;        // 1 if movement animation is in progress
int isRotating = 0;      // 1 if rotation animation is in progress
Uint32 moveStartTime;    // Start time of movement animation
Uint32 rotateStartTime;  // Start time of rotation animation
double startX, startY;   // Starting position for movement animation
double targetX, targetY; // Target position for movement animation
double startAngle;       // Starting angle for rotation animation
double targetAngle;      // Target angle for rotation animation

// Add a time delay between movements (for raycaster)
Uint32 lastMoveTime = 0; // Timestamp of the last move
#define MOVE_DELAY 200   // Delay between movements in milliseconds

// Set default global camera position (for topdown)
int cameraX = 0;
int cameraY = 0;

// Define cell format for maps
typedef struct {
    uint8_t tileByte;   // First byte: tile type and texture index
    uint8_t eventByte;  // Second byte: event type and event ID
} Cell;

// Create 2D map for game world
Cell worldMap[MAP_WIDTH][MAP_HEIGHT];

// Get cell type
uint8_t get_tile_type(Cell cell) {
    return (cell.tileByte & TILE_TYPE_MASK) >> 6;
}

// Get cell texture
uint8_t get_texture_index(Cell cell) {
    return cell.tileByte & TEXTURE_INDEX_MASK;
}

// Get event type
uint8_t get_event_type(Cell cell) {
    return (cell.eventByte & EVENT_TYPE_MASK) >> 5;
}

// Get event id
uint8_t get_event_id(Cell cell) {
    return cell.eventByte & EVENT_ID_MASK;
}

// Get size of layout components
void calculate_layout(int* viewport_width, int* column_width, int* viewport_height, int* column_height, int* dialogue_height) {
    *viewport_width = (RESO_X * VP_WIDTH) / (VP_WIDTH + CO_WIDTH); 
    *column_width = RESO_X - *viewport_width;
    *viewport_height = (RESO_Y * UP_SHARE) / (UP_SHARE + DN_SHARE);
    *column_height = *viewport_height;
    *dialogue_height = RESO_Y - *viewport_height;
}

// Get character index in character set
int get_char_index(const char *c) {
    const char *char_set = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789.,!?:;[]{}*^-+=<>|~@#$%& ";
    int index = 0;
    const char *p = char_set;

    while (*p != '\0') {
        if ((unsigned char)*p < 0x80) {
            if (*p == *c) {
                return index;
            }
            p++;
            index++;
        } else {
            if (p[0] == c[0] && p[1] == c[1]) {
                return index;
            }
            p += 2;
            index++;
        }
    }
    return -1;
}

// Render character using index in grid
void draw_char(SDL_Surface* surface, SDL_Surface* font_surface, int char_index, int x, int y) {
    if (char_index != -1) {
        // Calculate position in the grid
        int src_x = (char_index % FONT_COLUMNS) * (CHAR_WIDTH + CHAR_SPACING);
        int src_y = (char_index / FONT_COLUMNS) * (CHAR_HEIGHT + LINE_SPACING);

        // Set source and destination rects
        SDL_Rect src_rect = { src_x, src_y, CHAR_WIDTH, CHAR_HEIGHT };
        SDL_Rect dst_rect = { x, y, CHAR_WIDTH, CHAR_HEIGHT };

        // Blit character to surface
        SDL_BlitSurface(font_surface, &src_rect, surface, &dst_rect);
    }
}

// Render string of text (UTF-8)
void draw_text(SDL_Surface* surface, SDL_Surface* font_surface, int x, int y, const char* text) {
    int x_offset = 0;
    int y_offset = 0;
    int i = 0;

    while (text[i] != '\0') {
        if (text[i] == '\n') {
            y_offset += CHAR_HEIGHT + NLINE_SPACING;
            x_offset = 0;
            i++;
        } else {
            int char_index;
            int bytes_advance;

            unsigned char ch = text[i];
            if (ch < 0x80) {
                char_index = get_char_index(&text[i]);
                bytes_advance = 1;
            } else if ((ch & 0xE0) == 0xC0) {
                char_index = get_char_index(&text[i]);
                bytes_advance = 2;
            } else {
                char_index = -1;
                bytes_advance = 1;
            }

            draw_char(surface, font_surface, char_index, x + x_offset, y + y_offset);
            x_offset += CHAR_WIDTH + CHAR_SPACING;

            i += bytes_advance;
        }
    }
}

// Load the map
int load_map(const char* filename) {
    FILE* file = fopen(filename, "rb");
    if (!file) {
        printf("Failed to open map file: %s\n", filename);
        return 0;
    }

    for (int y = 0; y < MAP_HEIGHT; y++) {
        for (int x = 0; x < MAP_WIDTH; x++) {
            uint8_t bytes[2];
            if (fread(bytes, 1, 2, file) != 2) {
                printf("Error reading map data at (%d, %d)\n", x, y);
                fclose(file);
                return 0;
            }
            worldMap[x][y].tileByte = bytes[0];  // Store the tileByte
            worldMap[x][y].eventByte = bytes[1]; // Store the eventByte
        }
    }

    fclose(file);
    printf("Map loaded successfully from %s\n", filename);
    return 1;
}

// Get texture atlas
SDL_Surface* texture_atlas = NULL;
void load_texture_atlas(const char* atlas_filename) {
    texture_atlas = IMG_Load(atlas_filename);
    if (!texture_atlas) {
        printf("Failed to load texture atlas: %s\n", IMG_GetError());
        SDL_Quit();
        exit(1);
    }
}

// Get player sprite
SDL_Surface* playerSprite = NULL;
void load_player_sprite(const char* spritename) {
    playerSprite = IMG_Load(spritename);
    if (!playerSprite) {
        printf("Failed to load player sprite: %s\n", IMG_GetError());
        SDL_Quit();
        exit(1);
    }
}

// Initialize the map
void initialize_worldMap(const char* filename, const char* atlasname) {
    if (!load_map(filename)) {
        printf("Failed to load map. Initializing default map.\n");
        
        for (int x = 0; x < MAP_WIDTH; x++) {
            for (int y = 0; y < MAP_HEIGHT; y++) {
                if (x == 0 || y == 0 || x == MAP_WIDTH - 1 || y == MAP_HEIGHT - 1) {
                    worldMap[x][y].tileByte = 1;  // Set tileByte for walls
                    worldMap[x][y].eventByte = 0; // No event
                } else {
                    worldMap[x][y].tileByte = 0;  // Set tileByte for empty space
                    worldMap[x][y].eventByte = 0; // No event
                }
            }
        }

        // Load texture atlas
        load_texture_atlas(atlasname);

        for (int x = 5; x < 19; x++) {
            worldMap[x][10].tileByte = 1;  // Set tileByte for walls
            worldMap[x][10].eventByte = 0; // No event
        }
    } else {
        load_texture_atlas(atlasname);
    }
}

// Get pixel from a surface
Uint32 get_pixel(SDL_Surface *surface, int x, int y) {
    if (x < 0 || x >= surface->w || y < 0 || y >= surface->h)
        return 0; // Out of bounds returns black

    Uint32 pixel;
    Uint8 r, g, b;

    // Calculate pixel address
    Uint32 *pixels = (Uint32 *)surface->pixels;
    pixel = pixels[(y * surface->w) + x];

    // Juggling color channels
    SDL_GetRGB(pixel, surface->format, &b, &g, &r);
    return SDL_MapRGB(surface->format, r, g, b);
}

// Put a pixel onto a surface
void put_pixel(SDL_Surface *surface, int x, int y, Uint32 pixel) {
    if (x < 0 || x >= surface->w || y < 0 || y >= surface->h)
        return;

    Uint32 *pixels = (Uint32 *)surface->pixels;
    pixels[(y * surface->w) + x] = pixel;
}

// Player directions
typedef enum {
    NORTH,
    EAST,
    SOUTH,
    WEST
} Direction;

// Logical position and direction (grid-aligned)
int gridX = 12;
int gridY = 12;
Direction gridDir = NORTH;

// Movement Functions (for raycaster)
void initiate_move_forward(Uint32 currentTime) {
    if (!isMoving && !isRotating) {
        // Determine target grid position
        int newX = gridX;
        int newY = gridY;

        // Move in the direction the player is facing
        switch (gridDir) {
            case NORTH: newX += 1; break; // Move up (north)
            case EAST:  newY -= 1; break; // Move right (east)
            case SOUTH: newX -= 1; break; // Move down (south)
            case WEST:  newY += 1; break; // Move left (west)
        }

        // Check for collision (is walkable?)
        if (newX >= 0 && newX < MAP_WIDTH && newY >= 0 && newY < MAP_HEIGHT) {
            Cell targetCell = worldMap[newX][newY];
            uint8_t tileType = get_tile_type(targetCell);

            if (tileType == 0 || tileType == 2) {
                isMoving = 1;
                moveStartTime = currentTime;
                startX = playerX;
                startY = playerY;
                targetX = newX + 0.5;
                targetY = newY + 0.5;
                gridX = newX;
                gridY = newY;
            }
        }
    }
}

void initiate_move_backward(Uint32 currentTime) {
    if (!isMoving && !isRotating) {
        int newX = gridX;
        int newY = gridY;

        switch (gridDir) {
            case NORTH: newX -= 1; break;
            case EAST:  newY += 1; break;
            case SOUTH: newX += 1; break;
            case WEST:  newY -= 1; break;
        }

        // Check for collision (is walkable?)
        if (newX >= 0 && newX < MAP_WIDTH && newY >= 0 && newY < MAP_HEIGHT) {
            Cell targetCell = worldMap[newX][newY];
            uint8_t tileType = get_tile_type(targetCell);

            if (tileType == 0 || tileType == 2) {
                isMoving = 1;
                moveStartTime = currentTime;
                startX = playerX;
                startY = playerY;
                targetX = newX + 0.5;
                targetY = newY + 0.5;
                gridX = newX;
                gridY = newY;
            }
        }
    }
}

void initiate_turn_left(Uint32 currentTime) {
    if (!isMoving && !isRotating) {
        isRotating = 1;
        rotateStartTime = currentTime;
        startAngle = dirAngle;
        gridDir = (gridDir + 1) % 4;
        targetAngle = startAngle - (M_PI / 2);
    }
}

void initiate_turn_right(Uint32 currentTime) {
    if (!isMoving && !isRotating) {
        isRotating = 1;
        rotateStartTime = currentTime;
        startAngle = dirAngle;
        gridDir = (gridDir + 3) % 4;
        targetAngle = startAngle + (M_PI / 2); 
    }
}

// Movement functions (for topdown view)
void initiate_move_up(Uint32 currentTime) {
    if (!isMoving && !isRotating) {
        int newX = gridX;
        int newY = gridY - 1; 

        // Check for bounds
        if (newY >= 0) {
            Cell targetCell = worldMap[newX][newY];
            uint8_t tileTypeBits = targetCell.tileByte & TILE_TYPE_MASK;

            // Check if tile is walkable
            if (tileTypeBits == TILE_TYPE_FLOOR || tileTypeBits == TILE_TYPE_HALF_FLOOR) {
                isMoving = 1;
                moveStartTime = currentTime;
                startX = playerX;
                startY = playerY;
                targetX = newX + 0.5;
                targetY = newY + 0.5;
                gridX = newX;
                gridY = newY;
            }
        }
    }
}

void initiate_move_down(Uint32 currentTime) {
    if (!isMoving && !isRotating) {
        int newX = gridX;
        int newY = gridY + 1; 

        // Check for bounds
        if (newY >= 0) {
            Cell targetCell = worldMap[newX][newY];
            uint8_t tileTypeBits = targetCell.tileByte & TILE_TYPE_MASK;

            // Check if tile is walkable 
            if (tileTypeBits == TILE_TYPE_FLOOR || tileTypeBits == TILE_TYPE_HALF_FLOOR) {
                isMoving = 1;
                moveStartTime = currentTime;
                startX = playerX;
                startY = playerY;
                targetX = newX + 0.5;
                targetY = newY + 0.5;
                gridX = newX;
                gridY = newY;
            }
        }
    }
}

void initiate_move_right(Uint32 currentTime) {
    if (!isMoving && !isRotating) {
        int newX = gridX + 1;
        int newY = gridY;

        // Check for bounds
        if (newY >= 0) {
            Cell targetCell = worldMap[newX][newY];
            uint8_t tileTypeBits = targetCell.tileByte & TILE_TYPE_MASK;

            // Check if tile is walkable 
            if (tileTypeBits == TILE_TYPE_FLOOR || tileTypeBits == TILE_TYPE_HALF_FLOOR) {
                isMoving = 1;
                moveStartTime = currentTime;
                startX = playerX;
                startY = playerY;
                targetX = newX + 0.5;
                targetY = newY + 0.5;
                gridX = newX;
                gridY = newY;
            }
        }
    }
}

void initiate_move_left(Uint32 currentTime) {
    if (!isMoving && !isRotating) {
        int newX = gridX - 1;
        int newY = gridY;

        // Check for bounds
        if (newY >= 0) {
            Cell targetCell = worldMap[newX][newY];
            uint8_t tileTypeBits = targetCell.tileByte & TILE_TYPE_MASK;

            // Check if tile is walkable 
            if (tileTypeBits == TILE_TYPE_FLOOR || tileTypeBits == TILE_TYPE_HALF_FLOOR) {
                isMoving = 1;
                moveStartTime = currentTime;
                startX = playerX;
                startY = playerY;
                targetX = newX + 0.5;
                targetY = newY + 0.5;
                gridX = newX;
                gridY = newY;
            }
        }
    }
}

// Update movement
void update_movement(Uint32 currentTime) {
    if (isMoving) {
        Uint32 elapsed = currentTime - moveStartTime;
        double t = (double)elapsed / MOVE_DURATION;

        if (t >= 1.0) {
            playerX = targetX;
            playerY = targetY;
            isMoving = 0;
        } else {
            playerX = startX + (targetX - startX) * t;
            playerY = startY + (targetY - startY) * t;
        }
    }
}

// Update rotation
void update_rotation(Uint32 currentTime) {
    if (isRotating) {
        Uint32 elapsed = currentTime - rotateStartTime;
        double t = (double)elapsed / ROTATE_DURATION;

        if (t >= 1.0) {
            dirAngle = targetAngle;
            isRotating = 0;
        } else {
            dirAngle = startAngle + (targetAngle - startAngle) * t;
        }

        // Wrap angle between 0 and 2π
        if (dirAngle < 0) dirAngle += 2 * M_PI;
        if (dirAngle >= 2 * M_PI) dirAngle -= 2 * M_PI;
    }
}

// Handle raycaster input
void handle_raycasting_input(SDL_Event event) {
    if (event.type == SDL_KEYDOWN) {
        Uint32 currentTime = SDL_GetTicks();
        switch (event.key.keysym.sym) {
            case SDLK_UP:
                initiate_move_forward(currentTime);
                break;
            case SDLK_DOWN:
                initiate_move_backward(currentTime);
                break;
            case SDLK_LEFT:
                initiate_turn_left(currentTime);
                break;
            case SDLK_RIGHT:
                initiate_turn_right(currentTime);
                break;
            default:
                break;
        }
    }
}

// Raycaster
void raycaster(SDL_Surface* surface, int viewport_width, int viewport_height) {
    // Clear the viewport
    SDL_FillRect(surface, NULL, SDL_MapRGB(surface->format, 0, 0, 0));

    // Floor and ceiling colors
    Uint32 floorColor = SDL_MapRGB(surface->format, 50, 50, 50);
    Uint32 ceilingColor = SDL_MapRGB(surface->format, 20, 20, 20);

    // Draw floor and ceiling
    SDL_Rect floorRect = {0, viewport_height / 2, viewport_width, viewport_height / 2};
    SDL_FillRect(surface, &floorRect, floorColor);
    SDL_Rect ceilingRect = {0, 0, viewport_width, viewport_height / 2};
    SDL_FillRect(surface, &ceilingRect, ceilingColor);

    // Calculate direction vector and camera plane based on dirAngle
    double dirX = cos(dirAngle);
    double dirY = sin(dirAngle);
    double planeX = -dirY * 0.66; // FOV factor
    double planeY = dirX * 0.66;

    // Raycasting loop
    for (int x = 0; x < viewport_width; x++) {
        // Calculate ray position and direction
        double cameraX = 2 * x / (double)viewport_width - 1; // x-coordinate in camera space
        double rayDirX = dirX + planeX * cameraX;
        double rayDirY = dirY + planeY * cameraX;

        // Map position
        int mapX = (int)playerX;
        int mapY = (int)playerY;

        // Length of ray from current position to next x or y-side
        double sideDistX;
        double sideDistY;

        // Length of ray from one x or y-side to next x or y-side
        double deltaDistX = (rayDirX == 0) ? 1e30 : fabs(1 / rayDirX);
        double deltaDistY = (rayDirY == 0) ? 1e30 : fabs(1 / rayDirY);
        double perpWallDist;

        // Direction to go in x and y (+1 or -1)
        int stepX;
        int stepY;

        int hit = 0; // Was a wall hit?
        int side;    // Was a NS or a EW wall hit?

        // Calculate step and initial sideDist
        if (rayDirX < 0) {
            stepX = -1;
            sideDistX = (playerX - mapX) * deltaDistX;
        } else {
            stepX = 1;
            sideDistX = (mapX + 1.0 - playerX) * deltaDistX;
        }
        if (rayDirY < 0) {
            stepY = -1;
            sideDistY = (playerY - mapY) * deltaDistY;
        } else {
            stepY = 1;
            sideDistY = (mapY + 1.0 - playerY) * deltaDistY;
        }

        // Perform DDA
        while (hit == 0) {
            // Jump to next map square in x or y direction
            if (sideDistX < sideDistY) {
                sideDistX += deltaDistX;
                mapX += stepX;
                side = 0; // NS wall
            } else {
                sideDistY += deltaDistY;
                mapY += stepY;
                side = 1; // EW wall
            }
            // Check if ray has hit a wall
            if (mapX >= 0 && mapX < MAP_WIDTH && mapY >= 0 && mapY < MAP_HEIGHT) {
                if (get_tile_type(worldMap[mapX][mapY]) > 0) hit = 1;
            } else {
                // Out of bounds
                hit = 1;
            }
        }

        // Avoid fish-eye
        if (side == 0) {
            perpWallDist = (mapX - playerX + (1 - stepX) / 2) / rayDirX;
        } else {
            perpWallDist = (mapY - playerY + (1 - stepY) / 2) / rayDirY;
        }

        // Calculate height of line for screen
        int lineHeight = (int)(viewport_height / perpWallDist);

        // Calculate lowest and highest pixel to fill in current stripe
        int drawStart = -lineHeight / 2 + viewport_height / 2;
        if (drawStart < 0) drawStart = 0;
        int drawEnd = lineHeight / 2 + viewport_height / 2;
        if (drawEnd >= viewport_height) drawEnd = viewport_height - 1;

        // Get cell data
        if (mapX < 0 || mapX >= MAP_WIDTH || mapY < 0 || mapY >= MAP_HEIGHT) {
            // Default color if out of bounds
            SDL_Rect wallRect = { x, drawStart, 1, drawEnd - drawStart };
            SDL_FillRect(surface, &wallRect, SDL_MapRGB(surface->format, 255, 255, 255)); // White
            continue;
        }

        Cell cell = worldMap[mapX][mapY];
        uint8_t tileType = get_tile_type(cell);
        uint8_t textureIndex = get_texture_index(cell);

        // Calculate texture coordinates in the atlas
        int texCol = textureIndex % ATLAS_COLUMNS;
        int texRow = textureIndex / ATLAS_COLUMNS;
        int textureOffsetX = texCol * TILE_SIZE;
        int textureOffsetY = texRow * TILE_SIZE;

        // Calculate texture X coordinate for wall hit
        double wallX;
        if (side == 0) {
            wallX = playerY + perpWallDist * rayDirY;
        } else {
            wallX = playerX + perpWallDist * rayDirX;
        }
        wallX -= floor(wallX);

        int texX = (int)(wallX * (double)TILE_SIZE);
        if (side == 0 && rayDirX > 0) texX = TILE_SIZE - texX - 1;
        if (side == 1 && rayDirY < 0) texX = TILE_SIZE - texX - 1;

        // Floor rendering
        for (int y = viewport_height / 2 + 1; y < viewport_height; y++) {
            // Calculate distance from the player to this row
            double currentDist = (double)viewport_height / (2.0 * y - viewport_height);

            // Calculate floor position at this distance
            double floorX = playerX + currentDist * (rayDirX);
            double floorY = playerY + currentDist * (rayDirY);

            // Map position
            int floorMapX = (int)(floorX);
            int floorMapY = (int)(floorY);

            if (floorMapX >= 0 && floorMapX < MAP_WIDTH && floorMapY >= 0 && floorMapY < MAP_HEIGHT) {
                // Get cell from map
                Cell floorCell = worldMap[floorMapX][floorMapY];
                uint8_t floorTileType = floorCell.tileByte & TILE_TYPE_MASK;
                uint8_t floorTextureIndex = floorCell.tileByte & TEXTURE_INDEX_MASK;

                // Calculate texture coordinates
                int floorTexX = (int)((floorX - floorMapX) * TILE_SIZE) & (TILE_SIZE - 1);
                int floorTexY = (int)((floorY - floorMapY) * TILE_SIZE) & (TILE_SIZE - 1);

                // Calculate texture position in atlas
                int atlasColumns = texture_atlas->w / TILE_SIZE;
                int texCol = floorTextureIndex % atlasColumns;
                int texRow = floorTextureIndex / atlasColumns;
                int textureOffsetX = texCol * TILE_SIZE;
                int textureOffsetY = texRow * TILE_SIZE;

                // Get color from texture atlas
                Uint32 floorColor = get_pixel(texture_atlas, textureOffsetX + floorTexX, textureOffsetY + floorTexY);
                    
                // Calculate shading factor based on distance
                double shadingFactor = 1 / (currentDist * 0.2 + 1.0); // Adjust 0.2 to control shading intensity

                // Clamp shadingFactor between 0 and 1
                if (shadingFactor < 0.0) shadingFactor = 0.0;
                if (shadingFactor > 1.0) shadingFactor = 1.0;

                // Apply shading to RGB components
                Uint8 r, g, b;
                SDL_GetRGB(floorColor, surface->format, &r, &g, &b);
                r = (Uint8)(r * shadingFactor);
                g = (Uint8)(g * shadingFactor);
                b = (Uint8)(b * shadingFactor);
                floorColor = SDL_MapRGB(surface->format, r, g, b);

                // Draw floor pixel
                put_pixel(surface, x, y, floorColor);
            } else {
                // Out of bounds
            }
        }

        // Draw texture stripe
        for (int y = drawStart; y < drawEnd; y++) {
            int d = y * 256 - viewport_height * 128 + lineHeight * 128;
            int texY = ((d * TILE_SIZE) / lineHeight) / 256;

            // Clamp texY to texture bounds
            if (texY < 0) texY = 0;
            if (texY >= TILE_SIZE) texY = TILE_SIZE - 1;

            // Get pixel from texture atlas
            Uint32 color = get_pixel(texture_atlas, textureOffsetX + texX, textureOffsetY + texY);
                    
            // Calculate shading factor based on distance
            double shadingFactor = 1.0 / (perpWallDist * 0.1 + 1.0); // Adjust 0.1 to control shading intensity

            // Clamp shadingFactor between 0 and 1
            if (shadingFactor < 0.0) shadingFactor = 0.0;
            if (shadingFactor > 1.0) shadingFactor = 1.0;

            // Apply shading to RGB components
            Uint8 r, g, b;
            SDL_GetRGB(color, surface->format, &r, &g, &b);
            r = (Uint8)(r * shadingFactor);
            g = (Uint8)(g * shadingFactor);
            b = (Uint8)(b * shadingFactor);
            color = SDL_MapRGB(surface->format, r, g, b);

            put_pixel(surface, x, y, color);
        }
    }
}

// Handle top-down input
void handle_top_down_input(SDL_Event event) {
    if (event.type == SDL_KEYDOWN) {
        switch (event.key.keysym.sym) {
            case SDLK_UP:
                initiate_move_up(SDL_GetTicks());
                break;
            case SDLK_DOWN:
                initiate_move_down(SDL_GetTicks());
                break;
            case SDLK_LEFT:
                initiate_move_left(SDL_GetTicks());
                break;
            case SDLK_RIGHT:
                initiate_move_right(SDL_GetTicks());
                break;
            default:
                break;
        }
    }
}

// Top-down view rendering function
void render_top_down(SDL_Surface* surface, int tile_size) {
    int vptilesx = (RESO_X * VP_WIDTH) / (VP_WIDTH + CO_WIDTH) / tile_size;   // Number of horizontal tiles in viewport
    int vptilesy = (RESO_Y * UP_SHARE) / (UP_SHARE + DN_SHARE) / tile_size;   // Number of vertical tiles in viewport
    float xcamoff = ((RESO_X * VP_WIDTH) / (VP_WIDTH + CO_WIDTH)) - vptilesx; // Calculate camera X offset 
    float ycamoff = ((RESO_X * UP_SHARE) / (VP_WIDTH + UP_SHARE)) - vptilesy; // Calculate camera Y offset 

    // Set thresholds for camera movement
    int xthreshold = 7;
    int ythreshold = 4;

    // Check if player at left or right threshold
    if (playerX - cameraX <= xthreshold) {
        cameraX -= (vptilesx / 2); 
    } else if (playerX - cameraX >= vptilesx - xthreshold) {
        cameraX += (vptilesx / 2); 
    }

    // Check if player at bottom or top threshold
    if (playerY - cameraY <= ythreshold) {
        cameraY -= vptilesy / 2;
    } else if (playerY - cameraY >= vptilesy - ythreshold) {
        cameraY += vptilesy / 2;
    }

    // Clamp camera position to map boundaries
    if (cameraX < 0) cameraX = 0;
    if (cameraY < 0) cameraY = 0;
    if (cameraX > MAP_WIDTH - vptilesx) cameraX = MAP_WIDTH - vptilesx;
    if (cameraY > MAP_HEIGHT - vptilesy) cameraY = MAP_HEIGHT - vptilesy;

    // Render visible tiles based on current camera position
    for (int y = 0; y < MAP_HEIGHT; y++) {
        for (int x = 0; x < MAP_WIDTH; x++) {
            int mapX = cameraX + x;
            int mapY = cameraY + y;

            if (mapX >= 0 && mapX < MAP_WIDTH && mapY >= 0 && mapY < MAP_HEIGHT) {
                Cell cell = worldMap[mapX][mapY];
                uint8_t textureIndex = get_texture_index(cell);

                if (textureIndex < NUM_TEX) {
                    // Calculate texture coordinates in texture atlas
                    int texCol = textureIndex % ATLAS_COLUMNS;
                    int texRow = textureIndex / ATLAS_COLUMNS;
                    SDL_Rect srcRect = { texCol * TILE_SIZE, texRow * TILE_SIZE, TILE_SIZE, TILE_SIZE };

                    // Calculate screen position for tile
                    SDL_Rect dstRect = { x * tile_size, y * tile_size, tile_size, tile_size };

                    // Blit texture for the tile
                    SDL_BlitSurface(texture_atlas, &srcRect, surface, &dstRect);
                } else {
                    // Render default texture if textureIndex out of bounds
                    Uint32 defaultColor = SDL_MapRGB(surface->format, 255, 0, 255); // Magenta for errors
                    SDL_Rect tileRect = { x * tile_size, y * tile_size, tile_size, tile_size };
                    SDL_FillRect(surface, &tileRect, defaultColor);
                }
            }
        }
    }

    // Render player sprite
    SDL_Rect playerRect = {
        (int)((playerX - cameraX) * tile_size - tile_size / 2),
        (int)((playerY - cameraY) * tile_size - tile_size / 2),
        tile_size,
        tile_size
    };
    SDL_BlitSurface(playerSprite, NULL, surface, &playerRect);
}

// Render art mode
SDL_Surface* artImage = NULL;
void render_art(SDL_Surface* vpscreen,const char* artfile) {
    artImage = IMG_Load(artfile);
    if (!artImage) {
        printf("Failed to load image: %s\n", IMG_GetError());
        SDL_Quit();
        exit(1);
    }
    SDL_BlitSurface(artImage, NULL, vpscreen, NULL);
}

// Render wide art
void render_wideart(SDL_Surface* vpscreen, SDL_Surface* coscreen, const char* artfile) {
    // Calculate viewport and column widths
    int vpwidt = (RESO_X * VP_WIDTH) / (VP_WIDTH + CO_WIDTH); 
    int cowidt = RESO_X - vpwidt;

    // Load art image
    artImage = IMG_Load(artfile);
    if (!artImage) {
        printf("Failed to load image: %s\n", IMG_GetError());
        SDL_Quit();
        exit(1);
    }

    // Viewport half
    SDL_Rect srcRectVP = { 0, 0, artImage->w * vpwidt / RESO_X, artImage->h };
    SDL_Rect dstRectVP = { 0, 0, vpwidt, vpscreen->h };  // Full viewport height

    // Blit to viewport
    SDL_BlitSurface(artImage, &srcRectVP, vpscreen, &dstRectVP);

    // Column half
    SDL_Rect srcRectCO = { srcRectVP.w, 0, artImage->w * cowidt / RESO_X, artImage->h };
    SDL_Rect dstRectCO = { 0, 0, cowidt, coscreen->h };  // Full column height

    // Blit to column
    SDL_BlitSurface(artImage, &srcRectCO, coscreen, &dstRectCO);
}

int main(int argc, char* argv[]) {
    // Initialize SDL
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        printf("Unable to initialize SDL: %s\n", SDL_GetError());
        return 1;
    }


    Uint32 lastTime = SDL_GetTicks();

    // Create a window
    SDL_Surface* screen = SDL_SetVideoMode(RESO_X, RESO_Y, 32, SDL_SWSURFACE);
    if (!screen) {
        printf("Unable to set video mode: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    // Load the PNG font image
    SDL_Surface* font_surface = IMG_Load("font.png");
    if (!font_surface) {
        printf("Unable to load font: %s\n", IMG_GetError());
        SDL_Quit();
        return 1;
    }

    // Load the PNG Texture Atlas 
    load_texture_atlas("atlas.png");

    // Load the PNG Character Sprite
    load_player_sprite("pc.png");

    // Set the window title
    SDL_WM_SetCaption("Goldbox Game Engine Clone (InDev)", NULL);

    // Calculate layout proportions
    int viewport_width, column_width, viewport_height, column_height, dialogue_height;
    calculate_layout(&viewport_width, &column_width, &viewport_height, &column_height, &dialogue_height);

    // Create viewport surface
    SDL_Surface* viewport_surface = SDL_CreateRGBSurface(SDL_SWSURFACE, viewport_width, viewport_height, 32, 0, 0, 0, 0);
    if (!viewport_surface) {
        printf("Unable to create viewport surface: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    // Initialize the world map
    initialize_worldMap("map.bin", "atlas.png");

    // Create surfaces for each text quadrant
    SDL_Surface* column_surface = SDL_CreateRGBSurface(SDL_SWSURFACE, column_width, column_height, 32, 0, 0, 0, 0);
    SDL_Surface* dialogue_surface = SDL_CreateRGBSurface(SDL_SWSURFACE, RESO_X, dialogue_height, 32, 0, 0, 0, 0);

    // Set initial display mode
    currentDisplayMode = DISPLAY_MODE_RAYCASTER;

    // Event loop
    int running = 1;
    SDL_Event event;
    Uint32 frameStart, frameTime; // Track frame time

    
    while (running) {
        frameStart = SDL_GetTicks(); // Start time of the frame

        // Event handling
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                running = 0;
            } else if (event.type == SDL_KEYDOWN) {
                // Global input handling
                if (event.key.keysym.sym == SDLK_ESCAPE) {
                    running = 0;
                } else if (event.key.keysym.sym == SDLK_TAB) {
                    if (currentDisplayMode == DISPLAY_MODE_RAYCASTER) {
                        currentDisplayMode = DISPLAY_MODE_TOPDOWN;
                    } else if (currentDisplayMode == DISPLAY_MODE_TOPDOWN) {
                        currentDisplayMode = DISPLAY_MODE_ART;
                    } else if (currentDisplayMode == DISPLAY_MODE_ART) {
                        currentDisplayMode = DISPLAY_MODE_WIDE_ART;
                    } else {
                        currentDisplayMode = DISPLAY_MODE_RAYCASTER;
                    }
                } else {
                    // Handle input based on display mode
                    switch (currentDisplayMode) {
                        case DISPLAY_MODE_RAYCASTER:
                            handle_raycasting_input(event);
                            break;
                        case DISPLAY_MODE_TOPDOWN:
                            handle_top_down_input(event);
                            break;
                        default:
                            break;
                    }
                }
            }
        }

        // Update animations
        Uint32 currentTime = SDL_GetTicks();
        update_movement(currentTime);
        update_rotation(currentTime);
        
        // Clear the viewport surface
        SDL_FillRect(viewport_surface, NULL, SDL_MapRGB(viewport_surface->format, 0, 0, 0));

    // Rendering based on display mode
    switch (currentDisplayMode) {
        case DISPLAY_MODE_RAYCASTER:
            raycaster(viewport_surface, viewport_width, viewport_height);
            break;
        case DISPLAY_MODE_TOPDOWN:
            render_top_down(viewport_surface, TILE_SIZE);
            break;
        case DISPLAY_MODE_ART:
            render_art(viewport_surface, "test.png");
            break;
        case DISPLAY_MODE_WIDE_ART:
            render_wideart(viewport_surface, column_surface, "widetest.png");
            break;
        default:
            break;
    }

        // Clear the quadrants (you can fill them or print something to them)
        SDL_FillRect(dialogue_surface, NULL, SDL_MapRGB(screen->format, 200, 80, 30));

        // Render text to the column surface
        if (currentDisplayMode != DISPLAY_MODE_WIDE_ART) {
        SDL_FillRect(column_surface, NULL, SDL_MapRGB(screen->format, 180, 70, 26));
        draw_text(column_surface, font_surface, 10, 10, "This is the \ninfo column.\nCharacter info\nor stats could\ngo here!");
        }

        // Render text to the dialogue box
        draw_text(dialogue_surface, font_surface, 10, 10, "This is the dialogue box, which explains what]s\ngoing on, and conveys story info.");

        // Blit each surface onto the main screen
        SDL_Rect viewport_rect = {0, 0, viewport_width, viewport_height};
        SDL_Rect column_rect = {viewport_width, 0, column_width, column_height};
        SDL_Rect dialogue_rect = {0, viewport_height, RESO_X, dialogue_height};

        SDL_BlitSurface(viewport_surface, NULL, screen, &viewport_rect);
        SDL_BlitSurface(column_surface, NULL, screen, &column_rect);
        SDL_BlitSurface(dialogue_surface, NULL, screen, &dialogue_rect);

        // Update the screen
        SDL_Flip(screen);

        // Frame rate control
        frameTime = SDL_GetTicks() - frameStart;
        if (frameTime < FRAME_DELAY) {
            SDL_Delay(FRAME_DELAY - frameTime);
        }
    }

    SDL_FreeSurface(viewport_surface);
    SDL_FreeSurface(column_surface);
    SDL_FreeSurface(dialogue_surface);
    SDL_FreeSurface(font_surface);
    SDL_Quit();

    return 0;
}
