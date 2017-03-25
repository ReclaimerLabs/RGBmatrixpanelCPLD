/**
 * @file InternetWindowOfLife.c
 * @author Alex Hiam - <alex@graycat.io>
 *
 * @brief Conway's Game of Life for the Reclaimer Labs Internet Window.
 * 
 * Runs 1-3 Game of Life simulations separately in the R, G and B color 
 * channels. Every 5 minutes the panel is cleared and a new simulation starts,
 * each time deciding randomly which colors are enabled.
 * 
 * See: https://github.com/ReclaimerLabs/RGBmatrixpanelCPLD
 */

#include <RGBmatrixPanelCPLD.h>


#define BRIGHTNESS     0x08 ///< The brightness of each lit pixel
#define STEP_PERIOD_MS 100  ///< How often to update the universes (minimum)
#define WRAP_EDGES     1    ///< 1 to wrap around display edges, 0 to not

#define UNIVERSE_DURATION_MS 300000 ///< How long each simulation runs


#define WIDTH       128                  ///< Display width
#define HEIGHT      64                   ///< Display height

#define N_CELLS     ((WIDTH) * (HEIGHT)) ///< Population size in cells
#define N_BYTES     ((N_CELLS) / 8)      ///< Number of bytes to store population (/8 for 1 bit per cell)
#define BUFFER_SIZE ((N_CELLS) / 4)      ///< Size of entire ping-pong buffer (2x N_BYTES for front+back)


#define RESYNC_PERIOD_MS 5000 ///< How often to resync the CPLD (can get out of sync with ESD)

/**
 * A Game of Life object
 * 
 * Contains the state of a Game of Life universe.
 * (should probably get converted to an actual C++ object...)
 */
typedef struct {
    uint16_t width;            ///< Width of the universe
    uint16_t height;           ///< Height of the universe
    uint16_t n_cells;          ///< Number of cells in the universe (width*height)
    uint16_t n_bytes;          ///< Number of bytes in the universe (cells / 8)
    uint8_t *buffer;           ///< Pointer to start of allocated ping-pong buffer
    uint16_t buffer_len;       ///< Length of the entire allocated ping-pong buffer
    uint8_t *current_universe; ///< Pointer to front buffer (being displayed)
    uint8_t *next_universe;    ///< Buffer to write next calculated frame to
    uint8_t wrap_edges;        ///< Whether or not to wrap around edges
} GameOfLife;

// Allocate buffers and GameOfLife objects for R, G and B universes: 
static uint8_t sg_red_buffer[BUFFER_SIZE];
static GameOfLife sg_red_universe;

static uint8_t sg_green_buffer[BUFFER_SIZE];
static GameOfLife sg_green_universe;

static uint8_t sg_blue_buffer[BUFFER_SIZE];
static GameOfLife sg_blue_universe;


// Internet Window display instance:
RGBmatrixPanelCPLD display(WIDTH, HEIGHT);


// Game of Life function prototypes:

/**
 * Initialize the given game of life universe.
 * 
 * @param universe pointer to a #GameOfLife object
 * @param buffer pointer to a pre-allocated buffer of size (width*height)/4
 * @param width width of the universe
 * @param height height of the universe
 * @param wrap_edges if 1 individuals will wrap around the universe edges, if 0 they'll fall off
 */
void gol_init(GameOfLife *universe, uint8_t *buffer, uint16_t width, uint16_t height,
              uint8_t wrap_edges);

/**
 * Clear the given universe.
 * 
 * @param universe pointer to an initialized #GameOfLife object
 */
void gol_clear(GameOfLife *universe);

/**
 * Set the given universe's cells to random states.
 * 
 * @param universe pointer to an initialized #GameOfLife object
 */
void gol_random_seed(GameOfLife *universe);

/**
 * Move the given universes clock one tick forward.
 * 
 * @param universe pointer to an initialized #GameOfLife object
 * 
 * @return returns the number of cells that have changed state
 */
uint16_t gol_tick(GameOfLife *universe);

/**
 * Get the state of a cell in the given universe.
 * 
 * @param universe pointer to an initialized #GameOfLife object
 * @param x the x coordinate of the desired cell
 * @param y the y coordinate of the desired cell
 *
 * @return returns 1 if the cell is alive, 0 if dead
 */
uint8_t gol_get_cell(GameOfLife *universe, uint16_t x, uint16_t y);

/**
 * Give life to a cell in the given universe.
 * 
 * Can be used for initial seeding, or for divine intervention.
 * 
 * @param universe pointer to an initialized #GameOfLife object
 * @param x the x coordinate of the desired cell
 * @param y the y coordinate of the desired cell
 */
void gol_set_cell(GameOfLife *universe, uint16_t x, uint16_t y);

/**
 * Kill a cell in the given universe.
 * 
 * Can be used for initial seeding, or for divine intervention.
 * 
 * @param universe pointer to an initialized #GameOfLife object
 * @param x the x coordinate of the desired cell
 * @param y the y coordinate of the desired cell
 */
void gol_clear_cell(GameOfLife *universe, uint16_t x, uint16_t y);


/**
 * Give life to a cell in the given universe's back buffer.
 * 
 * Used while computing the next state of the universe, and should
 * probably only be called by other gol_ functions...
 *
 * @param universe pointer to an initialized #GameOfLife object
 * @param x the x coordinate of the desired cell
 * @param y the y coordinate of the desired cell
 */
static void gol_set_next_cell(GameOfLife *universe, uint16_t x, uint16_t y);

/**
 * Kill a cell in the given universe's back buffer.
 * 
 * Used while computing the next state of the universe, and should
 * probably only be called by other gol_ functions...
 *
 * @param universe pointer to an initialized #GameOfLife object
 * @param x the x coordinate of the desired cell
 * @param y the y coordinate of the desired cell
 */
static void gol_clear_next_cell(GameOfLife *universe, uint16_t x, uint16_t y);



/**
 * Draws the R, G and B universes to the display.
 */
void draw_universes(void);


/**
 * This is supposed to be defined already, see:
 *   https://community.particle.io/t/random-seed-from-cloud-not-working/17343/14
 */
void random_seed_from_cloud(unsigned seed) {
   srand(seed);
}


void setup() {
    // Initialize the RGBmatrixPanelCPLD object:
    display.begin();
    
    // Initialize the universes:
    gol_init(&sg_red_universe, sg_red_buffer, WIDTH, HEIGHT, WRAP_EDGES);
    gol_init(&sg_green_universe, sg_green_buffer, WIDTH, HEIGHT, WRAP_EDGES);
    gol_init(&sg_blue_universe, sg_blue_buffer, WIDTH, HEIGHT, WRAP_EDGES);
}

void loop() {
    uint64_t start_time, elapsed_time;
    static uint64_t epoch = 0;
    static uint64_t resync_time = 0;
    static uint8_t enabled_colors;
    
    if (epoch == 0 || millis() - epoch >= UNIVERSE_DURATION_MS) {
        // It's the dawn of time, need to seed the universes

        // Randomly select which colors are enabled:
        enabled_colors = random(1, 8);
        // [1-7] for 1 bit per color: bit 0 for red, bit 1 for green and bit 2 for blue

        // Random seed the enabled colors:
        if (enabled_colors & 0x1) {
            // Bit 0 is set, random seed red:
            gol_random_seed(&sg_red_universe);
        }
        else {
            // Red is disabled, clear it:
            gol_clear(&sg_red_universe);
        }
        
        if (enabled_colors & 0x2) {
            // Bit 1 is set, random seed green:
            gol_random_seed(&sg_green_universe);
        }
        else {
            gol_clear(&sg_green_universe);
        }
        
        if (enabled_colors & 0x4) {
            // Bit 2 is set, random seed blue:
            gol_random_seed(&sg_blue_universe);
        }
        else {
            gol_clear(&sg_blue_universe);
        }
        
        // Grab the universe start time:
        epoch = millis();
    }
    
    // Grab the time before displaying the current state and calculating the next:
    start_time = millis();
    
    if (start_time - resync_time >= RESYNC_PERIOD_MS) {
        // Time to call resync to ensure we're in sync with the CPLD:
        display.resync();
        // Restart the resync counter:
        resync_time = start_time;
    }
    
    // Draw the current state of the R,G,B universes:
    draw_universes();

    // Calculate the next state of the universes:
    if (enabled_colors & 0x1) gol_tick(&sg_red_universe);
    if (enabled_colors & 0x2) gol_tick(&sg_green_universe);
    if (enabled_colors & 0x4) gol_tick(&sg_blue_universe);
    // This could be sped up 3x by updating all the universes at the
    // same time, but it's nice and object-oriented the way it is...
    
    // Sleep if there's any time left over in our configured period:
    elapsed_time = millis() - start_time;
    if (STEP_PERIOD_MS > elapsed_time) {
        delay(STEP_PERIOD_MS - elapsed_time);
    }
}


void draw_universes(void) {
    uint8_t r, g, b, channels;
    uint16_t x, y;
    // Loop through every cell:
    for (y=0; y<HEIGHT; y++) {
        for (x=0; x<WIDTH; x++) {
            // Get the state of the current cell in each universe:
            r = gol_get_cell(&sg_red_universe, x, y);
            g = gol_get_cell(&sg_green_universe, x, y);
            b = gol_get_cell(&sg_blue_universe, x, y);
            
            // Normalize the pixel brightness:
            channels = r + g + b;
            if (r) r = BRIGHTNESS / channels;
            if (g) g = BRIGHTNESS / channels;
            if (b) b = BRIGHTNESS / channels;

            // Draw it:
            display.drawPixel(x, y, display.Color444(r, g, b));
        }
    }
}


void gol_init(GameOfLife *universe, uint8_t *buffer, uint16_t width, uint16_t height,
              uint8_t wrap_edges) {
    // Set initial values:
    universe->width = width;
    universe->height = height;
    universe->n_cells = width*height;
    universe->n_bytes = universe->n_cells/8; // 1 bit per cell
    universe->buffer_len = universe->n_cells/4; // 2x n_bytes for front + back buffer
    universe->buffer = buffer;
    universe->current_universe = buffer; // Start with first half as front buffer
    universe->next_universe = buffer+universe->n_bytes; // And second half as back buffer
    universe->wrap_edges = wrap_edges ? 1 : 0;
    
    // Make sure all cells start dead:
    gol_clear(universe);
}

void gol_clear(GameOfLife *universe) {
    uint16_t i;
    // Quicker to walk through a byte at a time to clear:
    for (i=0; i<universe->buffer_len; i++) {
        universe->buffer[i] = 0;
    }
}

void gol_random_seed(GameOfLife *universe) {
    uint16_t i;
    // Quicker to walk through a byte at a time:
    for (i=0; i<universe->buffer_len; i++) {
        // Randomly set all the bits of the current byte:
        universe->buffer[i] = (uint8_t) random(0, 256);
    }
}

uint16_t gol_tick(GameOfLife *universe) {
    uint16_t cell_x, cell_y, n_changes;
    uint16_t neighbor_x, neighbor_y;
    int8_t offset_x, offset_y;
    uint8_t n_living_neighbors;
    uint8_t *current_universe, *next_universe;

    // Keep local pointers so we don't half to write all that every time:
    current_universe = universe->current_universe;
    next_universe = universe->next_universe;
    
    n_changes = 0;
    
    // Loop through every cell in the universe:
    for (cell_y=0; cell_y<universe->height; cell_y++) {
        for (cell_x=0; cell_x<universe->width; cell_x++) {

            // First count the number of living neighbors:
            n_living_neighbors = 0;
            for (offset_y=-1; offset_y<2; offset_y++) {
                for (offset_x=-1; offset_x<2; offset_x++) {
                    // Don't count the current cell as a neighbor:
                    if (offset_x == 0 && offset_y == 0) continue;

                    if (!universe->wrap_edges) {
                        // We're not wrapping edges, ignore cells past display edges:
                        if (cell_x == 0 && offset_x == -1) continue;
                        if (cell_x == universe->width-1 && offset_x == 1) continue;
                        if (cell_y == 0 && offset_y == -1) continue;
                        if (cell_y == universe->height-1 && offset_y == 1) continue;
                        neighbor_x = offset_x + cell_x;
                        neighbor_y = offset_y + cell_y;
                    }
                    else {
                        // Edge wrap enabled, any neighbor past the edge wraps to the other side: 
                        if (cell_x == 0 && offset_x == -1) {
                            neighbor_x = universe->width-1;
                        }
                        else if (cell_x == universe->width-1 && offset_x == 1){
                            neighbor_x = 0;
                        }
                        else neighbor_x = offset_x + cell_x;
                        
                        if (cell_y == 0 && offset_y == -1) {
                            neighbor_y = universe->height-1;
                        }
                        else if (cell_y == universe->height-1 && offset_y == 1) {
                            neighbor_y = 0;
                        }
                        else neighbor_y = offset_y + cell_y;
                    }

                    n_living_neighbors += gol_get_cell(universe, neighbor_x, neighbor_y);
                }
            }
            
            // Now n_living_neighbors holds the number of living neighbors, time to check the current cell:
            if (gol_get_cell(universe, cell_x, cell_y)) {
                // It's alive!
                if (n_living_neighbors < 2) {
                    // Cell dies of underpopulation, set the cells state in the back buffer:
                    gol_clear_next_cell(universe, cell_x, cell_y);
                    n_changes++;
                }
                else if (n_living_neighbors > 3) {
                    // Cell dies of overpopulation
                    gol_clear_next_cell(universe, cell_x, cell_y);
                    n_changes++;
                }
                else {
                    // Cell survives
                    gol_set_next_cell(universe, cell_x, cell_y);
                }
                
            }
            else {
                // It's dead :(
                if (n_living_neighbors == 3) {
                    // New cell is born
                    gol_set_next_cell(universe, cell_x, cell_y);
                    n_changes++;
                }
                else {
                    // Stays dead
                    gol_clear_next_cell(universe, cell_x, cell_y);
                }
            }
        }
    }
    
    // Now we've fully updated the back buffer it's time to ping-pong:
    universe->current_universe = next_universe;
    universe->next_universe = current_universe;
    
    return n_changes;
}

/**
 * Calculates the buffer index and bit number of the given cell.
 *
 * @param x the x coordinate of desired the cell
 * @param y the y coordinate of desired the cell
 * @param byte_index pointer to where to store the index to the byte in the buffer where the cell is
 * @param bit_num pointer to where to store which bit of the byte stores the cell
 */
static void gol_find_cell(uint16_t x, uint16_t y, uint16_t *byte_index, uint8_t *bit_num) {
    uint16_t cell_index;
    // First compute index in cells (bits):
    cell_index = y * WIDTH + x;
    // Divide by 8 to convert to bytes:
    (*byte_index) = cell_index / 8;
    // The the bit number is given by the remainder: 
    (*bit_num) = 7 - (cell_index % 8);
}

uint8_t gol_get_cell(GameOfLife *universe, uint16_t x, uint16_t y) {
    uint16_t byte_index;
    uint8_t bit_num;
    // Get the bit location of the cell:
    gol_find_cell(x, y, &byte_index, &bit_num);
    // Move the cell's bit to the lowest bit position:
    return (universe->current_universe[byte_index] >> bit_num) & 0x01;
}

void gol_set_cell(GameOfLife *universe, uint16_t x, uint16_t y) {
    uint16_t byte_index;
    uint8_t bit_num;
    gol_find_cell(x, y, &byte_index, &bit_num);
    universe->current_universe[byte_index] |= 1<<bit_num;
}

void gol_clear_cell(GameOfLife *universe, uint16_t x, uint16_t y) {
    uint16_t byte_index;
    uint8_t bit_num;
    gol_find_cell(x, y, &byte_index, &bit_num);
    universe->current_universe[byte_index] &= ~(1<<bit_num);
}

static void gol_set_next_cell(GameOfLife *universe, uint16_t x, uint16_t y) {
    uint16_t byte_index;
    uint8_t bit_num;
    gol_find_cell(x, y, &byte_index, &bit_num);
    universe->next_universe[byte_index] |= 1<<bit_num;
}

static void gol_clear_next_cell(GameOfLife *universe, uint16_t x, uint16_t y) {
    uint16_t byte_index;
    uint8_t bit_num;
    gol_find_cell(x, y, &byte_index, &bit_num);
    universe->next_universe[byte_index] &= ~(1<<bit_num);
}
