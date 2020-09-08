#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <SDL2/SDL_ttf.h>

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdbool.h>
#include <time.h>

// Visual assets.
#include "font.h"
#include "tiles.h"

#ifdef MUSIC
// If we enable music import the assets.
#include <SDL2/SDL_mixer.h>
#include "theme.h"
#include "clear.h"
#include "fall.h"
#include "over.h"
#include "level.h"

#define VOLUME_DEFAULT (MIX_MAX_VOLUME / 8)
#endif

#ifdef WASM
#include <emscripten/emscripten.h>
#endif

// PLAY GRID DIMENSIONS
#define WIDTH 10
#define HEIGHT 20

// Add space for boarder and UI
#define LEFT_OFFSET 5
#define RIGHT_OFFSET 1
// Account for top and bottom border
#define TOP_OFFSET 1
#define BOTTOM_OFFSET 1
// UI Horizontal offset
#define UI_OFFSET 3

// DISPLAY SETTINGS
#define WINDOW_TITLE "Tetris"
#define DESIRED_HEIGHT 720

#define MAX_FPS (1000 / 144)

// LOGICAL SIZE OF A TETRIS SQUARE
#define SQUARE_DIM (DESIRED_HEIGHT / (HEIGHT + TOP_OFFSET + BOTTOM_OFFSET))

// Smooth the boarders
#define WINDOW_HEIGHT (SQUARE_DIM * (HEIGHT + TOP_OFFSET + BOTTOM_OFFSET))
#define WINDOW_WIDTH (SQUARE_DIM * (WIDTH + LEFT_OFFSET + RIGHT_OFFSET))

// NUMBER OF CHARACTERS PER TETROMINO
#define TETROMINO_WIDTH 4
#define TETROMINO_SIZE 16

// GAMEPLAY MODIFIERS
#define MOVE_DELAY 1000
#define MIN_MOVE_DELAY 50
#define ROTATION_DELAY 150
#define DIFFICULTY_RATIO 0.1

// STRUCTURE AND DATA DEFINITIONS
typedef enum TETROMINO
{
    I,
    O,
    T,
    S,
    Z,
    J,
    L,
    NUM_TETROMINO,
} TETROMINO;

typedef enum ROTATION
{
    DEG_0,
    DEG_90,
    DEG_180,
    DEG_270,
    ROTATIONS,
} ROTATION;

typedef enum GAME_STATUS
{
    MENU,
    PLAYING,
    PAUSED,
    GAME_OVER,
    CLOSING,
} GAME_STATUS;

typedef struct TETRIS_STATE
{
    // Rendering stuff
    SDL_Window *window;
    SDL_Renderer *renderer;
    TTF_Font *font;
    SDL_Texture *tiles;
#ifdef MUSIC
    // Sounds and music
    Mix_Music *theme;
    Mix_Chunk *move;
    Mix_Chunk *place;
    Mix_Chunk *clear;
    Mix_Chunk *over;
    Mix_Chunk *level_up;
#endif
    // Game status
    GAME_STATUS status;
    // Timing
    uint32_t start_time;
    uint32_t pause_time;
    uint32_t pause_start;
    uint32_t last_frame;
    uint32_t last_move;
    uint32_t last_rotate;
    // Score
    uint16_t score;
    uint16_t rows_cleared;
    uint8_t level;
    // Board
    char board[(WIDTH * HEIGHT) * 2];
    // Tetris Tetromino bag
    TETROMINO tetromino_bag[NUM_TETROMINO];
    uint8_t bag_position;
    // Current piece
    TETROMINO tetromino_type;
    int tetromino_x;
    int tetromino_y;
    ROTATION tetromino_rotation;
} TETRIS_STATE;

// FUNCTION PROTOTYPES
static void draw_border(TETRIS_STATE *tetris);
static void draw_ui(TETRIS_STATE *tetris);
static void draw_bag(TETRIS_STATE *tetris);
static void draw_placed(TETRIS_STATE *tetris);
static void draw_piece(TETRIS_STATE *tetris);
static void main_loop(void *data);
static void reset_tetris_state(TETRIS_STATE *tetris);

// STATIC RESOURCES
const char *tetromino[NUM_TETROMINO] = {
    "..I."
    "..I."
    "..I."
    "..I.",
    ".OO."
    ".OO."
    "...."
    "....",
    "..T."
    ".TTT"
    "...."
    "....",
    "..SS"
    ".SS."
    "...."
    "....",
    ".ZZ."
    "..ZZ"
    "...."
    "....",
    ".J.."
    ".JJJ"
    "...."
    "....",
    "...L"
    ".LLL"
    "...."
    "....",
};

// TIMING FUNCTIONS
static uint32_t tetris_get_time(TETRIS_STATE *tetris)
{
    return SDL_GetTicks() - tetris->start_time + tetris->pause_time;
}

static void tetris_pause(TETRIS_STATE *tetris)
{
    tetris->pause_start = SDL_GetTicks();
    tetris->status = PAUSED;
#ifdef MUSIC
    Mix_PauseMusic();
#endif
}

static void tetris_unpause(TETRIS_STATE *tetris)
{

    tetris->pause_time += SDL_GetTicks() - tetris->pause_start;
    tetris->status = PLAYING;
#ifdef MUSIC
    Mix_ResumeMusic();
#endif
}

// HELPER FUNCTIONS
static int tetromino_translate_rotation(int x, int y, TETROMINO t,
                                        ROTATION rotation)
{
    // Disable O rotation.
    if (t == O)
        return y * TETROMINO_WIDTH + x;

    switch (rotation % ROTATIONS)
    {
    case DEG_0:
        return y * TETROMINO_WIDTH + x;
    case DEG_90:
        return 12 + y - (x * TETROMINO_WIDTH);
    case DEG_180:
        return 15 - (y * TETROMINO_WIDTH) - x;
    case DEG_270:
        return 3 - y + (x * TETROMINO_WIDTH);
    }
    return y * TETROMINO_WIDTH + x;
}

static void tetromino_create_bag(TETRIS_STATE *tetris)
{
    static const TETROMINO start_bag[NUM_TETROMINO] = {I, O, T, S, Z, J, L};
    memcpy(tetris->tetromino_bag, start_bag, sizeof(TETROMINO) * NUM_TETROMINO);
    for (size_t i = I; i < NUM_TETROMINO; i++)
    {
        size_t j = i + rand() / (RAND_MAX / (NUM_TETROMINO - i) + 1);
        TETROMINO t = tetris->tetromino_bag[j];
        tetris->tetromino_bag[j] = tetris->tetromino_bag[i];
        tetris->tetromino_bag[i] = t;
    }
    tetris->bag_position = 0;
    draw_bag(tetris);
}

static int tetromino_has_space(TETRIS_STATE *tetris, ROTATION r, int x, int y)
{
    // Returns 1 for out of bounds, 2 for block placement and 3 for game over.
    for (int i = 0; i < TETROMINO_SIZE; i++)
    {
        // Skip empty spaces
        if (tetromino[tetris->tetromino_type][i] == '.')
            continue;

        // Get rotation indexes if rotated
        int rotatedIndex =
            tetromino_translate_rotation(i % TETROMINO_WIDTH,
                                         i / TETROMINO_WIDTH,
                                         tetris->tetromino_type, r);

        // Add the indexes to the top left corner to get the actual position
        int real_x = x + (rotatedIndex % TETROMINO_WIDTH);
        int real_y = y + (rotatedIndex / TETROMINO_WIDTH);

        // Check the x axis bounds
        if (real_x < 0 || real_x >= WIDTH)
            return 1;

        // Check if we have hit the bottom
        if (real_y >= HEIGHT)
            return 2;

        // Off the screen, cannot be a game over
        if (real_y < 0)
            continue;

        // Check for game over or block placement
        // Check space that we are moving to is empty
        if (tetris->board[real_x + (real_y * WIDTH)] != '.')
        {
            // Block on top.
            if (real_y <= 1)
                return 3;

            // On top of another block, all good
            return 2;
        }
    }
    return 0;
}

static bool tetromino_move(TETRIS_STATE *tetris, ROTATION r, int x, int y)
{
    // Checks if new state is possible, writes if so otherwise does nothing.
    // Checks and allows for wall kicks
    // Check with original movement attempt.
    if (tetromino_has_space(tetris, r, x, y) == 0)
    {
        // Can move! write new position.
        tetris->tetromino_rotation = r;
        tetris->tetromino_x = x;
        tetris->tetromino_y = y;
        return true;
    }
    // WALL KICKS
    // Check if there is a free space to the right.
    if (tetromino_has_space(tetris, r, x + 1, y) == 0)
    {
        tetris->tetromino_rotation = r;
        tetris->tetromino_x = x + 1;
        tetris->tetromino_y = y;
        return true;
    }
    // Check if there is a free space to the left.
    if (tetromino_has_space(tetris, r, x - 1, y) == 0)
    {
        tetris->tetromino_rotation = r;
        tetris->tetromino_x = x - 1;
        tetris->tetromino_y = y;
        return true;
    }
    // FLOOR KICKS // DO WE NEED IT?
    return false;
}

static void tetromino_init(TETRIS_STATE *tetris)
{
    tetris->tetromino_type = tetris->tetromino_bag[tetris->bag_position];
    tetris->bag_position++;
    if (tetris->bag_position >= NUM_TETROMINO)
        tetromino_create_bag(tetris);

    tetris->tetromino_x = (WIDTH / 2) - (TETROMINO_WIDTH / 2);
    tetris->tetromino_y = -TETROMINO_WIDTH;
    tetris->tetromino_rotation = DEG_0;
}

static void tetromino_write(TETRIS_STATE *tetris)
{
    // Convert tetromino x and y to actual board coordinates
    for (int i = 0; i < TETROMINO_SIZE; i++)
    {
        if (tetromino[tetris->tetromino_type][i] == '.')
            continue;

        // Get the x and y inside of the tetromino
        int sub_x = i % TETROMINO_WIDTH;
        int sub_y = i / TETROMINO_WIDTH;

        // Get the rotated index
        int true_index =
            tetromino_translate_rotation(sub_x, sub_y, tetris->tetromino_type,
                                         tetris->tetromino_rotation);

        int true_x = true_index % TETROMINO_WIDTH;
        int true_y = true_index / TETROMINO_WIDTH;

        // Write to the board
        // Index translation.
        int index = tetris->tetromino_x + true_x + ((tetris->tetromino_y + true_y) * WIDTH);

        tetris->board[index] =
            tetromino[tetris->tetromino_type][i];
    }
}

// RENDER FUNCTIONS
static SDL_Rect transform_coords(int x, int y)
{
    return (SDL_Rect){
        .x = (x + LEFT_OFFSET) * SQUARE_DIM,
        .y = WINDOW_HEIGHT - (HEIGHT - y + 1) * SQUARE_DIM,
        .w = SQUARE_DIM,
        .h = SQUARE_DIM,
    };
}

static void draw_rect(SDL_Renderer *renderer, SDL_Rect pos, SDL_Color colour,
                      bool fill)
{
    SDL_SetRenderDrawColor(renderer, colour.r, colour.g, colour.b, colour.a);
    if (fill)
        SDL_RenderFillRect(renderer, &pos);
    else
        SDL_RenderDrawRect(renderer, &pos);

    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
}

static void draw_tile(SDL_Renderer *renderer, SDL_Rect dst_rect,
                      SDL_Texture *tex, char c)
{
    int index = -1;
    switch (c)
    {
    case 0:
        index = 0;
        break;
    case 'I':
        index = 1;
        break;
    case 'O':
        index = 2;
        break;
    case 'T':
        index = 3;
        break;
    case 'S':
        index = 4;
        break;
    case 'Z':
        index = 5;
        break;
    case 'J':
        index = 6;
        break;
    case 'L':
        index = 7;
        break;
    default:
        return;
    }
    SDL_Rect src_rect = {index * 32, 0, 32, 32};
    SDL_RenderCopy(renderer, tex, &src_rect, &dst_rect);
}

static void draw_tetromino_tile(TETRIS_STATE *tetris, char t, int x, int y)
{
    SDL_Rect dst_rect = transform_coords(x, y);
    draw_tile(tetris->renderer, dst_rect, tetris->tiles, t);
}

static void draw_tetromino_preview_tile(TETRIS_STATE *tetris, TETROMINO t,
                                        int x, int y)
{
    // Position of the top left quad
    SDL_Rect start_rect = transform_coords(x, y);
    start_rect.w = SQUARE_DIM / 2;
    start_rect.h = SQUARE_DIM / 2;
    for (int i = 0; i < TETROMINO_SIZE; i++)
    {
        int sub_x = i % TETROMINO_WIDTH;
        int sub_y = i / TETROMINO_WIDTH;
        SDL_Rect dst_rect = start_rect;
        dst_rect.x += (SQUARE_DIM / 2) * sub_x;
        dst_rect.y += (SQUARE_DIM / 2) * sub_y;
        char c = tetromino[t][i];
        draw_tile(tetris->renderer, dst_rect, tetris->tiles, c);
    }
}

static void draw_font(SDL_Renderer *renderer, TTF_Font *font, int x, int y,
                      const char *str)
{
    static SDL_Color c = {255, 255, 255, 255};
    SDL_Surface *surface = TTF_RenderText_Solid(font, str, c);
    SDL_Rect pos = {
        .x = SQUARE_DIM * (x + 1) + 2,
        .y = (surface->h * y) + (TOP_OFFSET * SQUARE_DIM),
    };
    pos.w = surface->w;
    pos.h = surface->h;
    SDL_Texture *texture = SDL_CreateTextureFromSurface(renderer, surface);
    SDL_FreeSurface(surface);
    SDL_RenderCopy(renderer, texture, NULL, &pos);
    SDL_DestroyTexture(texture);
}

static void tetromino_clear_row(TETRIS_STATE *tetris)
{
    // Check all the rows
    int cleared_rows[HEIGHT] = {0};
    int cleared = 0;
    for (int row = 0; row < HEIGHT; row++)
        for (int col = 0; col < WIDTH; col++)
        {
            if (tetris->board[(row * WIDTH) + col] == '.')
                break;
            if (col == WIDTH - 1)
            {
                cleared_rows[row] = 1;
                cleared++;
            }
        }

    // No rows cleared.
    if (!cleared)
        return;

#ifdef MUSIC
    Mix_PlayChannel(-1, tetris->clear, 0);
#endif
    // // Animate the row destruction. // Blocks game loop lol.
    // uint32_t start_animation = SDL_GetTicks();
    // while (SDL_GetTicks() < start_animation + 500)
    // {
    //     for (int row = 0; row < HEIGHT; row++)
    //     {
    //         if (!cleared_rows[row])
    //             continue;
    //         for (int col = 0; col < WIDTH; col++)
    //         {
    //             SDL_Rect rect = transform_coords(col, row);
    //             uint32_t passed = SDL_GetTicks() - start_animation;
    //             int opacity = (500 - passed) / 255;
    //             draw_rect(tetris->renderer, rect, (SDL_Color){0, 0, 0, opacity}, true);
    //         }
    //     }
    //     SDL_RenderPresent(tetris->renderer);
    // }

    // Remove the rows
    for (int row = 0; row < HEIGHT; row++)
    {
        if (!cleared_rows[row])
            continue;
        memmove(tetris->board + WIDTH, tetris->board, row * WIDTH);
    }
    // Initialize the top row
    memset(tetris->board, '.', WIDTH * cleared);

    // Add the score!
    switch (cleared)
    {
    case 1:
        tetris->score += 40 * (tetris->level + 1);
        tetris->rows_cleared += cleared;
        break;
    case 2:
        tetris->score += 100 * (tetris->level + 1);
        tetris->rows_cleared += cleared;
        break;
    case 3:
        tetris->score += 300 * (tetris->level + 1);
        tetris->rows_cleared += cleared;
        break;
    default:
        tetris->score += 1200 * (tetris->level + 1);
        tetris->rows_cleared += cleared * 2;
        break;
    }
    // New level?
    int level;
    if (tetris->level < 10)
        level = tetris->rows_cleared / 5;
    else if (tetris->level < 20)
        level = tetris->rows_cleared / 10;
    else if (tetris->level < 30)
        level = tetris->rows_cleared / 15;
    else
        level = tetris->rows_cleared / 25;

    if (level > tetris->level)
    {
        tetris->level = level;
#ifdef MUSIC
        Mix_PlayChannel(-1, tetris->level_up, 0);
#endif
    }
}

// RENDER PRESETS

static void draw_border(TETRIS_STATE *tetris)
{
    // Draw board box
    // Vertical barriers
    for (int i = 0; i < HEIGHT; i++)
    {
        draw_tetromino_tile(tetris, 0, -LEFT_OFFSET, i); // LEFT EDGE
        draw_tetromino_tile(tetris, 0, WIDTH, i);        // RIGHT EDGE
        draw_tetromino_tile(tetris, 0, -1, i);           // UI EDGE
    }
    // Horizontal barriers
    for (int i = 0 - LEFT_OFFSET; i < WIDTH + LEFT_OFFSET + RIGHT_OFFSET; i++)
    {
        draw_tetromino_tile(tetris, 0, i, HEIGHT); // TOP
        draw_tetromino_tile(tetris, 0, i, -1);     // BOTTOM
        if (i < 0)
            draw_tetromino_tile(tetris, 0, i, UI_OFFSET); // UI SEPERATOR
    }
}

static void draw_ui(TETRIS_STATE *tetris)
{
    const SDL_Rect viewport = {
        .x = (1) * SQUARE_DIM,
        .y = (1) * SQUARE_DIM,
        .w = (LEFT_OFFSET - 2) * SQUARE_DIM,
        .h = (UI_OFFSET)*SQUARE_DIM,
    };
    SDL_RenderFillRect(tetris->renderer, &viewport);

    // Draw score, level and time
    char time[] = "Time:           ";
    sprintf(time + 6, " %u", tetris_get_time(tetris) / 1000);
    draw_font(tetris->renderer, tetris->font, 0, 0, time);
    char level[] = "Level:           ";
    sprintf(level + 7, " %u", tetris->level);
    draw_font(tetris->renderer, tetris->font, 0, 1, level);
    char score[] = "Score:";
    draw_font(tetris->renderer, tetris->font, 0, 2, score);
    char points[9];
    sprintf(points, "%.8d", tetris->score);
    draw_font(tetris->renderer, tetris->font, 0, 3, points);
}

static void draw_bag(TETRIS_STATE *tetris)
{
    const SDL_Rect viewport = {
        .x = (1) * SQUARE_DIM,
        .y = (UI_OFFSET + 2) * SQUARE_DIM,
        .w = (LEFT_OFFSET - 2) * SQUARE_DIM,
        .h = (HEIGHT - UI_OFFSET - 1) * SQUARE_DIM,
    };
    SDL_RenderFillRect(tetris->renderer, &viewport);

    int x = 1 - LEFT_OFFSET;
    int y = UI_OFFSET + 1;
    for (int p = tetris->bag_position; p < NUM_TETROMINO; p++)
    {
        int position = p - tetris->bag_position;
        TETROMINO t = tetris->tetromino_bag[p];
        int new_y = y + position;
        // We dont want to write out of our section
        if (new_y + TETROMINO_WIDTH >= WIDTH * 2)
            break;

        draw_tetromino_preview_tile(tetris, t, x, new_y);
        // The perminant offset between pieces
        y += 2;
    }
}

static void draw_placed(TETRIS_STATE *tetris)
{
    // Draw board state
    for (int i = 0; i < (WIDTH * HEIGHT) * 2; i++)
    {
        int x = (i % WIDTH);
        // Uh why?
        int y = (i / (HEIGHT / 2));
        draw_tetromino_tile(tetris, tetris->board[i], x, y);
    }
}

static void draw_piece(TETRIS_STATE *tetris)
{
    // Draw falling piece
    for (int i = 0; i < TETROMINO_SIZE; i++)
    {
        // Skip empty spaces
        if (tetromino[tetris->tetromino_type][i] == '.')
            continue;

        int sub_x = i % TETROMINO_WIDTH;
        int sub_y = i / TETROMINO_WIDTH;
        int rotatedIndex =
            tetromino_translate_rotation(sub_x, sub_y,
                                         tetris->tetromino_type,
                                         tetris->tetromino_rotation);
        sub_x = rotatedIndex % TETROMINO_WIDTH;
        sub_y = rotatedIndex / TETROMINO_WIDTH;

        if (tetris->tetromino_y + sub_y < 0)
            continue;

        draw_tetromino_tile(tetris, tetromino[tetris->tetromino_type][i],
                            tetris->tetromino_x + sub_x,
                            tetris->tetromino_y + sub_y);
    }
}

static void draw_board(TETRIS_STATE *tetris)
{
    const SDL_Rect viewport = {
        .x = (LEFT_OFFSET)*SQUARE_DIM,
        .y = 1 * SQUARE_DIM,
        .w = WIDTH * SQUARE_DIM,
        .h = HEIGHT * SQUARE_DIM,
    };
    SDL_RenderFillRect(tetris->renderer, &viewport);
    draw_placed(tetris);
    draw_piece(tetris);
}

// CORE LOOP FUNCTIONS
static void update_state(TETRIS_STATE *tetris)
{
    // Are we writing the tetromino and creating a new one?
    if (tetris->last_move + MIN_MOVE_DELAY >= tetris_get_time(tetris))
        return;

    tetris->last_move = tetris_get_time(tetris);

    int move_status = tetromino_has_space(tetris, tetris->tetromino_rotation, tetris->tetromino_x,
                                          tetris->tetromino_y + 1);
    if (move_status == 2)
    {
#ifdef MUSIC
        Mix_PlayChannel(-1, tetris->place, 0);
#endif
        tetromino_write(tetris);
        tetromino_clear_row(tetris);
        tetromino_init(tetris);
        draw_bag(tetris);
        draw_board(tetris);
        return;
    }

    if (move_status == 3)
    {
        tetris->status = GAME_OVER;
#ifdef MUSIC
        Mix_HaltMusic();
        Mix_PlayChannel(-1, tetris->over, 0);
#endif
        char *game_over = "Game over! Press enter to play again";
        TTF_Font *big = TTF_OpenFontRW(SDL_RWFromConstMem(ssp_regular_otf, ssp_regular_otf_len),
                                       1, SQUARE_DIM);
        static SDL_Color c = {255, 255, 255, 255};
        SDL_Surface *surface = TTF_RenderText_Solid(big, game_over, c);
        TTF_CloseFont(big);
        SDL_Rect pos = {
            .x = WINDOW_WIDTH / 2 - (surface->w / 2),
            .y = WINDOW_HEIGHT / 2,
            .w = surface->w,
            .h = surface->h,
        };
        SDL_RenderFillRect(tetris->renderer, &pos);
        SDL_Texture *texture = SDL_CreateTextureFromSurface(tetris->renderer, surface);
        SDL_FreeSurface(surface);
        SDL_RenderCopy(tetris->renderer, texture, NULL, &pos);
        SDL_DestroyTexture(texture);
        return;
    }

    // Update the move timer.
    tetris->tetromino_y++;
    draw_board(tetris);
}

static void handle_events(TETRIS_STATE *tetris)
{
    static SDL_Event event;
#ifdef MUSIC
    static bool muted;
#endif
    while (SDL_PollEvent(&event))
    {
        switch (event.type)
        {
        case SDL_QUIT:
            tetris->status = CLOSING;
            break;
        case SDL_KEYDOWN:
#ifdef MUSIC
            if (event.key.keysym.scancode == SDL_SCANCODE_M)
            {
                if (muted)
                {
                    Mix_VolumeMusic(VOLUME_DEFAULT);
                    Mix_Volume(-1, VOLUME_DEFAULT * 1.5);
                }
                else
                {
                    Mix_VolumeMusic(0);
                    Mix_Volume(-1, 0);
                }
                muted = !muted;
            }
#endif
            if (tetris->status == GAME_OVER)
            {
                switch (event.key.keysym.scancode)
                {
                case SDL_SCANCODE_RETURN:
                    reset_tetris_state(tetris);
                    tetris->status = PLAYING;
                    break;
                default:
                    break;
                }
                break;
            }
            if (tetris->status == PAUSED)
            {
                switch (event.key.keysym.scancode)
                {
                case SDL_SCANCODE_P:
                case SDL_SCANCODE_ESCAPE:
                    tetris_unpause(tetris);
                    break;
                default:
                    break;
                }
                break;
            }
            switch (event.key.keysym.scancode)
            {
            // Move left and right
            case SDL_SCANCODE_A:
            case SDL_SCANCODE_LEFT:
                if (tetromino_move(tetris, tetris->tetromino_rotation,
                                   tetris->tetromino_x - 1, tetris->tetromino_y))
                    draw_board(tetris);
                break;
            case SDL_SCANCODE_D:
            case SDL_SCANCODE_RIGHT:
                if (tetromino_move(tetris, tetris->tetromino_rotation,
                                   tetris->tetromino_x + 1, tetris->tetromino_y))
                    draw_board(tetris);
                break;
            // Move down one unit (trigger a state update early)
            case SDL_SCANCODE_W:
            case SDL_SCANCODE_UP:
                if (tetris_get_time(tetris) <= tetris->last_rotate + ROTATION_DELAY)
                    break;
                if (tetromino_move(tetris,
                                   (tetris->tetromino_rotation + 1) % ROTATIONS,
                                   tetris->tetromino_x, tetris->tetromino_y))
                {
                    draw_board(tetris);
                    tetris->last_rotate = tetris_get_time(tetris);
                }
                break;
            // Rotate
            case SDL_SCANCODE_S:
            case SDL_SCANCODE_DOWN:
                update_state(tetris);
                break;
            case SDL_SCANCODE_P:
            case SDL_SCANCODE_ESCAPE:
                tetris_pause(tetris);
                break;
            default:
                break;
            }
        default:
            break;
        }
    }
}

// INITIALIZATION FUNCTIONS
static void init_rendering(TETRIS_STATE *tetris)
{
    tetris->window =
        SDL_CreateWindow(WINDOW_TITLE,
                         SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                         WINDOW_WIDTH, WINDOW_HEIGHT, 0);
    tetris->renderer = SDL_CreateRenderer(tetris->window, -1,
                                          SDL_RENDERER_ACCELERATED);
    SDL_SetRenderDrawColor(tetris->renderer, 0, 0, 0, 255);
    SDL_RenderSetLogicalSize(tetris->renderer, WINDOW_WIDTH, WINDOW_HEIGHT);
    SDL_SetRenderDrawBlendMode(tetris->renderer, SDL_BLENDMODE_BLEND);
    tetris->font =
        TTF_OpenFontRW(SDL_RWFromConstMem(ssp_regular_otf, ssp_regular_otf_len),
                       1, (WINDOW_HEIGHT / SQUARE_DIM) * 0.75);
    tetris->tiles = IMG_LoadTexture_RW(tetris->renderer,
                                       SDL_RWFromConstMem(tiles_png, tiles_png_len), 0);
}

static void free_rendering(TETRIS_STATE *tetris)
{
    TTF_CloseFont(tetris->font);
    SDL_DestroyTexture(tetris->tiles);
    SDL_DestroyRenderer(tetris->renderer);
    SDL_DestroyWindow(tetris->window);
}

#ifdef MUSIC
static void init_sound(TETRIS_STATE *tetris)
{
    Mix_VolumeMusic(VOLUME_DEFAULT);
    Mix_Volume(-1, VOLUME_DEFAULT * 1.5);
    tetris->theme = Mix_LoadMUS_RW(SDL_RWFromConstMem(theme_mp3, theme_mp3_len), -1);
    tetris->place = Mix_LoadWAV_RW(SDL_RWFromConstMem(fall_wav, fall_wav_len), -1);
    tetris->clear = Mix_LoadWAV_RW(SDL_RWFromConstMem(clear_wav, clear_wav_len), -1);
    tetris->over = Mix_LoadWAV_RW(SDL_RWFromConstMem(over_wav, over_wav_len), -1);
    tetris->level_up = Mix_LoadWAV_RW(SDL_RWFromConstMem(level_wav, level_wav_len), -1);
}

static void free_sound(TETRIS_STATE *tetris)
{
    Mix_FreeMusic(tetris->theme);
    Mix_FreeChunk(tetris->place);
    Mix_FreeChunk(tetris->clear);
    Mix_FreeChunk(tetris->over);
    Mix_FreeChunk(tetris->level_up);
}
#endif

static void reset_tetris_state(TETRIS_STATE *tetris)
{
    memset(tetris->board, '.', (WIDTH * HEIGHT) * 2);
    tetris->level = 0;
    tetris->rows_cleared = 0;
    tetris->score = 0;
    tetromino_create_bag(tetris);
    tetromino_init(tetris);
    tetris->tetromino_y = 0;
    tetris->status = PLAYING;
#ifdef MUSIC
    Mix_PlayMusic(tetris->theme, -1);
#endif
    // Initialize the timers.
    uint32_t now = SDL_GetTicks();
    tetris->start_time = now;
    tetris->pause_time = 0;
    tetris->pause_start = 0;
    tetris->last_frame = now;
    tetris->last_move = 0;
    tetris->last_rotate = 0;
    // Draw the boarder
    draw_border(tetris);
    draw_board(tetris);
    draw_ui(tetris);
    draw_bag(tetris);
}

static void init_tetris_state(TETRIS_STATE *tetris)
{
    tetris->status = PLAYING;
    reset_tetris_state(tetris);
}

static bool init_modules(void)
{
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) != 0)
        return false;

    if (TTF_Init() != 0)
        return false;

#ifdef MUSIC
    if (Mix_OpenAudio(22050, MIX_DEFAULT_FORMAT, 2, 4096) == -1)
        return false;
#endif

    return true;
}

static void quit_modules(void)
{
#ifdef MUSIC
    Mix_CloseAudio();
#endif
    TTF_Quit();
    SDL_Quit();
}

static void game_loop(void *data)
{
    TETRIS_STATE *tetris = data;
    uint32_t this_frame = 0;
    this_frame = tetris_get_time(tetris);
    handle_events(tetris);
    switch (tetris->status)
    {
    case PLAYING:
        if (tetris->last_move + MOVE_DELAY - (int)(MOVE_DELAY * DIFFICULTY_RATIO * tetris->level) < this_frame)
            update_state(tetris);
        draw_ui(tetris);
        break;
    case PAUSED:
        break;
    case GAME_OVER:
        break;
    default:
        break;
    }
    SDL_RenderPresent(tetris->renderer);
    this_frame = tetris_get_time(tetris);
    if (tetris->last_frame > this_frame - MAX_FPS)
        SDL_Delay(tetris->last_frame + MAX_FPS - this_frame);
    tetris->last_frame = tetris_get_time(tetris);
}

int main(int argc, char *argv[])
{
    if (!init_modules())
    {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR,
                                 "Fatal Error",
                                 "Unable to initialize SDL2",
                                 NULL);
        return 1;
    }
    srand(time(NULL));

    TETRIS_STATE tetris = {0};
    init_rendering(&tetris);
#ifdef MUSIC
    init_sound(&tetris);
#endif
    init_tetris_state(&tetris);

#ifdef WASM
    emscripten_set_main_loop_arg(&game_loop, &tetris, -1, 1);
#else
    while (tetris.status != CLOSING)
        game_loop(&tetris);
#endif

#ifdef MUSIC
    free_sound(&tetris);
#endif
    free_rendering(&tetris);
    quit_modules();
    return 0;
}
