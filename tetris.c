#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <SDL2/SDL_ttf.h>

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdbool.h>
#include <time.h>

#include "font.h"
#include "tiles.h"

// PLAY GRID DIMENSIONS
#define WIDTH 10
#define HEIGHT 20

// PUSH THE PLAY GRID AROUND FOR RENDERING
#define LEFT_OFFSET 1
#define TOP_OFFSET 4

// DISPLAY SETTINGS
#define WINDOW_TITLE "Tetris"
#define WINDOW_HEIGHT 720

#define MAX_FPS (1000 / 60)

// LOGICAL SIZE OF A TETRIS SQUARE
#define SQUARE_DIM (WINDOW_HEIGHT / ((HEIGHT + 1) + TOP_OFFSET))
#define WINDOW_WIDTH (SQUARE_DIM * (WIDTH + (LEFT_OFFSET * 2)))

// NUMBER OF CHARACTERS PER TETROMINO
#define TETROMINO_WIDTH 4
#define TETROMINO_SIZE 16

// GAMEPLAY MODIFIERS
#define MOVE_DELAY 1000
#define MIN_MOVE_DELAY 50
#define ROTATION_DELAY 150
#define DIFFICULTY_RATIO 0.2
#define SCORE_LEVEL_RATIO 500 // 1000

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
    // Game status
    GAME_STATUS status;
    // Timing
    uint32_t last_frame;
    uint32_t last_move;
    // Score
    uint16_t score;
    // Board
    char board[(WIDTH * HEIGHT) * 2];
    // Current piece
    TETROMINO tetromino_type;
    int tetromino_x;
    int tetromino_y;
    ROTATION tetromino_rotation;
    // Next piece
    TETROMINO next_tetromino_type;
} TETRIS_STATE;

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
    ".L.."
    ".LLL"
    "...."
    "....",
};

// HELPER FUNCTIONS
static int tetromino_translate_rotation(int x, int y, TETROMINO t, ROTATION rotation)
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

static int tetromino_has_space(TETRIS_STATE *tetris, int x, int y)
{
    for (int i = 0; i < TETROMINO_SIZE; i++)
    {
        // Skip empty spaces
        if (tetromino[tetris->tetromino_type][i] == '.')
            continue;

        // Get rotation indexes if rotated
        int rotatedIndex =
            tetromino_translate_rotation(i % TETROMINO_WIDTH,
                                         i / TETROMINO_WIDTH,
                                         tetris->tetromino_type,
                                         tetris->tetromino_rotation);

        // Add the indexes to the top left corner to get the actual position
        int real_x = x + rotatedIndex % TETROMINO_WIDTH;
        int real_y = y + rotatedIndex / TETROMINO_WIDTH;

        // Check the x axis bounds
        if (real_x < 0 || real_x > WIDTH - 1)
            return 1;

        // Off the screen, cannot be a game over
        if (real_y < 0)
            continue;

        // Check if we have hit the bottom
        if (real_y >= HEIGHT)
            return 2;

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

static void tetromino_init(TETRIS_STATE *tetris)
{
    tetris->tetromino_type = rand() % NUM_TETROMINO;
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

static void draw_tile(SDL_Renderer *renderer, SDL_Texture *tex, int x, int y,
                      int index)
{
    SDL_Rect dst_rect = transform_coords(x, y);
    SDL_Rect src_rect = {index * 32, 0, 32, 32};
    SDL_RenderCopy(renderer, tex, &src_rect, &dst_rect);
}

static void draw_font(SDL_Renderer *renderer, TTF_Font *font, int x, int y,
                      const char *str)
{
    static SDL_Color c = {255, 255, 255, 255};
    int font_height = TTF_FontHeight(font);
    int font_width = (font_height / 2.7) * strlen(str);
    SDL_Rect pos = {
        .x = SQUARE_DIM * x,
        .y = SQUARE_DIM * y,
        .w = font_width,
        .h = font_height,
    };
    SDL_Surface *surface = TTF_RenderText_Blended(font, str, c);
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

    // Animate the row destruction. // Blocks game loop lol.
    uint32_t start_animation = SDL_GetTicks();
    while (SDL_GetTicks() < start_animation + 500)
    {
        for (int row = 0; row < HEIGHT; row++)
        {
            if (!cleared_rows[row])
                continue;
            for (int col = 0; col < WIDTH; col++)
            {
                SDL_Rect rect = transform_coords(col, row);
                uint32_t passed = SDL_GetTicks() - start_animation;
                int opacity = (500 - passed) / 255;
                draw_rect(tetris->renderer, rect, (SDL_Color){0, 0, 0, opacity}, true);
            }
        }
        SDL_RenderPresent(tetris->renderer);
    }

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
    int level = tetris->score / SCORE_LEVEL_RATIO;
    switch (cleared)
    {
    case 1:
        tetris->score += 40 * (level + 1);
        break;
    case 2:
        tetris->score += 100 * (level + 1);
        break;
    case 3:
        tetris->score += 300 * (level + 1);
        break;
    default:
        tetris->score += 1200 * (level + 1);
        break;
    }
}

// CORE LOOP FUNCTIONS
static void update_state(TETRIS_STATE *tetris)
{
    // Are we writing the tetromino and creating a new one?
    if (tetris->last_move + MIN_MOVE_DELAY >= SDL_GetTicks())
        return;

    tetris->last_move = SDL_GetTicks();

    int move_status = tetromino_has_space(tetris, tetris->tetromino_x,
                                          tetris->tetromino_y + 1);
    if (move_status == 2)
    {
        tetromino_write(tetris);
        tetromino_clear_row(tetris);
        tetromino_init(tetris);
        return;
    }

    if (move_status == 3)
    {
        tetris->status = GAME_OVER;
        return;
    }

    // Update the move timer.
    tetris->tetromino_y++;
}

static void handle_events(TETRIS_STATE *tetris)
{
    static SDL_Event event;
    static uint32_t last_rotation;
    while (SDL_PollEvent(&event))
    {
        switch (event.type)
        {
        case SDL_QUIT:
            tetris->status = CLOSING;
            break;
        case SDL_KEYDOWN:
            switch (event.key.keysym.scancode)
            {
            // Move left and right
            case SDL_SCANCODE_A:
            case SDL_SCANCODE_LEFT:
                tetris->tetromino_x =
                    !tetromino_has_space(tetris, tetris->tetromino_x - 1,
                                         tetris->tetromino_y)
                        ? tetris->tetromino_x - 1
                        : tetris->tetromino_x;
                break;
            case SDL_SCANCODE_D:
            case SDL_SCANCODE_RIGHT:
                tetris->tetromino_x =
                    !tetromino_has_space(tetris, tetris->tetromino_x + 1,
                                         tetris->tetromino_y)
                        ? tetris->tetromino_x + 1
                        : tetris->tetromino_x;
                break;
            // Move down if possible\0 TODO: CHECK & RESET LAST FALL TIMER
            case SDL_SCANCODE_W:
            case SDL_SCANCODE_UP:
                update_state(tetris);
                break;
            // Rotate
            case SDL_SCANCODE_S:
            case SDL_SCANCODE_DOWN:
                if (SDL_GetTicks() > last_rotation + ROTATION_DELAY)
                {
                    tetris->tetromino_rotation =
                        (tetris->tetromino_rotation + 1) % ROTATIONS;
                    if (tetromino_has_space(tetris, tetris->tetromino_x,
                                            tetris->tetromino_y))
                        tetris->tetromino_rotation =
                            (tetris->tetromino_rotation - 1) % ROTATIONS;
                    last_rotation = SDL_GetTicks();
                }
                break;
            default:
                break;
            }
        default:
            break;
        }
    }
}

static void render_state(TETRIS_STATE *tetris)
{
    SDL_RenderClear(tetris->renderer);

    // Draw board box
    for (int i = 0; i < HEIGHT; i++)
    {
        draw_tile(tetris->renderer, tetris->tiles, 0 - LEFT_OFFSET, i, 0);
        draw_tile(tetris->renderer, tetris->tiles, WIDTH, i, 0);
    }
    for (int i = 0 - LEFT_OFFSET; i < WIDTH + LEFT_OFFSET; i++)
    {
        draw_tile(tetris->renderer, tetris->tiles, i, HEIGHT, 0);
        draw_tile(tetris->renderer, tetris->tiles, i, -1, 0);
    }

    // Draw board status
    for (int i = 0; i < (WIDTH * HEIGHT) * 2; i++)
    {
        int x = (i % WIDTH);
        // Uh why?
        int y = (i / (HEIGHT / 2));
        switch (tetris->board[i])
        {
        case 'I':
            draw_tile(tetris->renderer, tetris->tiles, x, y, 1);
            break;
        case 'O':
            draw_tile(tetris->renderer, tetris->tiles, x, y, 2);
            break;
        case 'T':
            draw_tile(tetris->renderer, tetris->tiles, x, y, 3);
            break;
        case 'S':
            draw_tile(tetris->renderer, tetris->tiles, x, y, 4);
            break;
        case 'Z':
            draw_tile(tetris->renderer, tetris->tiles, x, y, 5);
            break;
        case 'J':
            draw_tile(tetris->renderer, tetris->tiles, x, y, 6);
            break;
        case 'L':
            draw_tile(tetris->renderer, tetris->tiles, x, y, 7);
            break;
        default:
            break;
        }
    }

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

        switch (tetromino[tetris->tetromino_type][i])
        {
        case 'I':
            draw_tile(tetris->renderer, tetris->tiles, tetris->tetromino_x + sub_x, tetris->tetromino_y + sub_y, 1);
            break;
        case 'O':
            draw_tile(tetris->renderer, tetris->tiles, tetris->tetromino_x + sub_x, tetris->tetromino_y + sub_y, 2);
            break;
        case 'T':
            draw_tile(tetris->renderer, tetris->tiles, tetris->tetromino_x + sub_x, tetris->tetromino_y + sub_y, 3);
            break;
        case 'S':
            draw_tile(tetris->renderer, tetris->tiles, tetris->tetromino_x + sub_x, tetris->tetromino_y + sub_y, 4);
            break;
        case 'Z':
            draw_tile(tetris->renderer, tetris->tiles, tetris->tetromino_x + sub_x, tetris->tetromino_y + sub_y, 5);
            break;
        case 'J':
            draw_tile(tetris->renderer, tetris->tiles, tetris->tetromino_x + sub_x, tetris->tetromino_y + sub_y, 6);
            break;
        case 'L':
            draw_tile(tetris->renderer, tetris->tiles, tetris->tetromino_x + sub_x, tetris->tetromino_y + sub_y, 7);
            break;
        default:
            break;
        }
    }

    char score[16];
    sprintf(score, "Score: %7d", tetris->score);
    draw_font(tetris->renderer, tetris->font, 0, 0, score);
    char level[16];
    sprintf(level, "Level: %u", tetris->score / SCORE_LEVEL_RATIO);
    draw_font(tetris->renderer, tetris->font, 0, 1, level);
    char speed[16];
    sprintf(speed, "SPD: %d", MOVE_DELAY - (int)(MOVE_DELAY * (DIFFICULTY_RATIO * (tetris->score / SCORE_LEVEL_RATIO))));
    draw_font(tetris->renderer, tetris->font, 5, 1, speed);
    SDL_RenderPresent(tetris->renderer);
}

// GAME OVER WINDOW
static bool game_over_window(TETRIS_STATE *tetris)
{
    const SDL_MessageBoxButtonData buttons[] = {
        {SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, 0, "Play Again"},
        {SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, 1, "Quit"},
    };
    char score[30];
    sprintf(score, "Score: %.7d\nPlay again?", tetris->score);
    const SDL_MessageBoxData messageboxdata = {
        SDL_MESSAGEBOX_INFORMATION, /* .flags */
        tetris->window,             /* .window */
        "Game Over",                /* .title */
        score,                      /* .message */
        SDL_arraysize(buttons),     /* .numbuttons */
        buttons,                    /* .buttons */
        NULL                        /* .colorScheme */
    };
    int buttonid;
    SDL_ShowMessageBox(&messageboxdata, &buttonid);
    if (buttonid == -1 || buttonid == 1)
        return false;

    if (buttonid == 0)
        return true;

    return false;
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
                       1, WINDOW_HEIGHT / SQUARE_DIM);
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

static void init_tetris_state(TETRIS_STATE *tetris)
{
    tetris->status = PLAYING;
    memset(tetris->board, '.', (WIDTH * HEIGHT) * 2);
    tetris->last_frame = SDL_GetTicks();
    tetris->last_move = 0;
    tetromino_init(tetris);
    tetris->score = 0;
}

static void reset_tetris_state(TETRIS_STATE *tetris)
{
    memset(tetris->board, '.', (WIDTH * HEIGHT) * 2);
    tetris->score = 0;
    tetromino_init(tetris);
    tetris->status = PLAYING;
}

static void tetris_game_over(TETRIS_STATE *tetris)
{
    if (game_over_window(tetris))
        reset_tetris_state(tetris);
    else
        tetris->status = CLOSING;
}

static bool init_modules(void)
{
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) != 0)
        return false;

    if (TTF_Init() != 0)
        return false;

    return true;
}

static void quit_modules(void)
{
    TTF_Quit();
    SDL_Quit();
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

    TETRIS_STATE tetris;
    init_tetris_state(&tetris);
    init_rendering(&tetris);
    uint32_t this_frame = 0;
    while (tetris.status != CLOSING)
    {
        this_frame = SDL_GetTicks();
        handle_events(&tetris);

        if (tetris.last_move + (MOVE_DELAY - (int)(MOVE_DELAY * (DIFFICULTY_RATIO * (tetris.score / SCORE_LEVEL_RATIO)))) < this_frame)
            update_state(&tetris);

        render_state(&tetris);
        this_frame = SDL_GetTicks();
        if (tetris.last_frame > this_frame - MAX_FPS)
            SDL_Delay(tetris.last_frame + MAX_FPS - this_frame);

        if (tetris.status == GAME_OVER)
            tetris_game_over(&tetris);

        tetris.last_frame = SDL_GetTicks();
    }

    free_rendering(&tetris);
    quit_modules();
    return 0;
}
