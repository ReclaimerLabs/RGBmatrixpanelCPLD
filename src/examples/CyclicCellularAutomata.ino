/**
 * @file CyclicCellularAutomata.ino
 * @author Alex Hiam - <alex@graycat.io>
 *
 * @brief Cyclic Cellular Automata for the Reclaimer Labs Internet Window.
 * 
 * CCA engine and example for the Internet Window. Runs the 313 rule set as
 * described @ http://psoup.math.wisc.edu/mcell/rullex_cycl.html
 * Reseeds the automota every 5 minutes.
 * 
 * For more info on CCA see:
 *  - https://en.wikipedia.org/wiki/Cyclic_cellular_automaton
 *  - http://psoup.math.wisc.edu/mcell/rullex_cycl.html
 * 
 * Internet Window: https://github.com/ReclaimerLabs/RGBmatrixpanelCPLD
 */

#include <RGBmatrixPanelCPLD.h>


#define WIDTH       128 ///< Display width
#define HEIGHT      64  ///< Display height

#define N_CELLS     ((WIDTH) * (HEIGHT)) ///< Population size in cells
#define BUFFER_SIZE ((N_CELLS) * 2)      ///< 2x N_CELLS for double buffer

#define RESYNC_PERIOD_MS 5000 ///< How often to resync the CPLD (can get out of sync with ESD)

// Some convenience defines for different colors:
#define HUE_RED    0    ///< The hue corresponding to red
#define HUE_YELLOW 255  ///< The hue corresponding to yellow
#define HUE_GREEN  512  ///< The hue corresponding to green
#define HUE_AQUA   768  ///< The hue corresponding to aqua
#define HUE_BLUE   1023 ///< The hue corresponding to blue
#define HUE_PURPLE 1279 ///< The hue corresponding to purple


/**
 * Passed to #set_neighborhood_rules to set the neighborhood type.
 */
typedef enum {
    NEIGHBORHOOD_M=0, ///< Moore neighborhood
    NEIGHBORHOOD_VN,  ///< Von Neumann neighborhood
} NeighborhoodType;


/**
 * The Cyclic Cellular Automata configuration object.
 */
typedef struct {
    // Game rules:
    uint8_t range;          ///< The size of a cell's neighborhood
    uint8_t threshold;      ///<The number of neighbors with the succeeding value required to consume a cell
    uint8_t n_states;       ///< The number of states a cell can be in
    NeighborhoodType type;  ///<  The type of neighborhood
    
    // Color settings:
    uint16_t include_black; ///< If 1 state 0 = black, else state 0 = min_hue
    uint16_t min_hue;       ///< The minimum hue to use [0-1535]
    uint16_t max_hue;       ///< The maximum hue to use [0-1535]
    uint8_t saturation;     ///< The saturation [0-255]
    uint8_t brightness;     ///< The brightness [0-255] (probably shouldn't exceed 128...)
    
    // Config:
    uint8_t wrap_edges;
    uint32_t step_period;
    uint32_t duration_ms;
} CCA_Config;


/**
 * The Cyclic Cellular Automata object.
 */
typedef struct {
    uint8_t *buffer;       // Pointer to the user's pre-allocated buffer
    uint8_t *front_buffer; // Pointer to the current front buffer within #buffer
    uint8_t *back_buffer;  // Pointer to the current back buffer within #buffer
    uint16_t width;        // The width of the automaton
    uint16_t height;       // The height of the automaton
    uint32_t n_cells;      // The number of cells in the automaton (width*height)

    CCA_Config config;     // The configuration
} CCA_Neighborhood;


// Create a configuration:
CCA_Config sg_configuration = {
    // 313 from http://psoup.math.wisc.edu/mcell/rullex_cycl.html
    1,               // Range = 1 cells
    3,               // Threshold = 3 cells
    3,               // 3 possible states
    NEIGHBORHOOD_M,  // Moore neighborhood
    
    0,               // don't include black, so state 0 = min_hue
    HUE_RED,         // Min hue = red
    HUE_BLUE,        // Max hue = blue for red, green, blue
    128,             // Saturation = 50%
    77,              // Brightness = 30%
    
    1,               // Wrap edges
    0,               // No delay between steps
    600000           // Run for 5 minutes
};

// Internet Window display instance:
static RGBmatrixPanelCPLD sg_display(WIDTH, HEIGHT);

static CCA_Neighborhood sg_neighborhood;
static uint8_t sg_neighborhood_buffer[BUFFER_SIZE];

/**
 * Initialize the given #CCA_Neighborhood.
 * 
 * @param neighborhood Pointer to the neighborhood
 * @param buffer Pointer to a pre-initialized buffer of size (width*height)*2
 * @param width The width of the neighborhood
 * @param height The height of the neighborhood
 */
void init_neighborhood(CCA_Neighborhood *neighborhood, uint8_t *buffer,
        uint16_t width, uint16_t height);

/**
 * Sets the given neighborhood's configuration.
 * 
 * This is not done in the most efficient way as the #CCA_Config struct
 * is passed in by value, but it's only 24 bytes so not that big of a deal.
 * 
 * @param neighborhood Pointer to the neighborhood
 * @param config The #CCA_Config configuration
 */
void configure_neighborhood(CCA_Neighborhood *neighborhood, CCA_Config config);

/**
 * Randomly seed the given neighborhood.
 * 
 * @param neighborhood Pointer to the neighborhood to seed
 */
void seed_neighborhood(CCA_Neighborhood *neighborhood);

/**
 * Compute the next state of the given neighborhood.
 * 
 * @param neighborhood Pointer to the neighborhood to tick
 */
void tick_neighborhood(CCA_Neighborhood *neighborhood);

/**
 * Draw the given neighborhood to the display.
 * 
 * @param neighborhood Pointer to the neighborhood to draw
 * @param display Pointer to the RGBmatrixPanelCPLD instance to draw to
 */
void draw_neighborhood(CCA_Neighborhood *neighborhood, RGBmatrixPanelCPLD *display);

/**
 * Get the current state of the a cell.
 *
 * @param neighborhood Pointer to the neighborhood
 * @param x The x coordinate of the desired cell
 * @param y The y coordinate of the desired cell
 */
uint8_t get_cell(CCA_Neighborhood *neighborhood, uint16_t x, uint16_t y);

/**
 * Set the next state of the a cell.
 *
 * @param neighborhood Pointer to the neighborhood
 * @param x The x coordinate of the desired cell
 * @param y The y coordinate of the desired cell
 * @param state The cell's new state
 */
void set_next_cell(CCA_Neighborhood *neighborhood, uint16_t x, uint16_t y, uint8_t state);



/**
 * This is supposed to be defined already, see:
 *   https://community.particle.io/t/random-seed-from-cloud-not-working/17343/14
 */
void random_seed_from_cloud(unsigned seed) {
   srand(seed);
}


void setup() {
    // Initialize the RGBmatrixPanelCPLD object:
    sg_display.begin();

    // Initialize the neighborhood:
    init_neighborhood(&sg_neighborhood, sg_neighborhood_buffer, WIDTH, HEIGHT);
    
    // Set the configuration:
    configure_neighborhood(&sg_neighborhood, sg_configuration);
}

void loop() {
    uint64_t start_time, elapsed_time;
    static uint64_t epoch = 0;
    static uint64_t resync_time = 0;

    if (epoch == 0 || millis() - epoch >= (sg_neighborhood.config.duration_ms)) {
        // It's the dawn of time, need to re-seed:
        seed_neighborhood(&sg_neighborhood);
        // Grab the new start time:
        epoch = millis();
    }
    
    // Grab the time before displaying the current state and calculating the next:
    start_time = millis();
    
    if (start_time - resync_time >= RESYNC_PERIOD_MS) {
        // Time to call resync to ensure we're in sync with the CPLD:
        sg_display.resync();
        // Restart the resync counter:
        resync_time = start_time;
    }
    
    // Draw the current state of the neighborhood:
    draw_neighborhood(&sg_neighborhood, &sg_display);

    // Calculate the next state:
    tick_neighborhood(&sg_neighborhood);
       
    // Sleep if there's any time left over in our configured period:
    elapsed_time = millis() - start_time;
    //if (STEP_PERIOD_MS > elapsed_time) {
    //    delay(STEP_PERIOD_MS - elapsed_time);
    //}
}

void init_neighborhood(CCA_Neighborhood *neighborhood, uint8_t *buffer,
        uint16_t width, uint16_t height) {
    neighborhood->width = width;
    neighborhood->height = height;
    neighborhood->n_cells = width*height;

    neighborhood->buffer = buffer;
    neighborhood->front_buffer = buffer;
    neighborhood->back_buffer = buffer + neighborhood->n_cells;
}

void configure_neighborhood(CCA_Neighborhood *neighborhood, CCA_Config config) {
    neighborhood->config = config;
}


void seed_neighborhood(CCA_Neighborhood *neighborhood) {
    uint32_t i;
    uint8_t n_states;
    n_states =  neighborhood->config.n_states;
    for (i=0; i<neighborhood->n_cells; i++) {
        neighborhood->front_buffer[i] = (uint8_t) random(0, n_states);
    }
}


uint8_t get_cell(CCA_Neighborhood *neighborhood, uint16_t x, uint16_t y) {
    return neighborhood->front_buffer[y*(neighborhood->width) + x];
}

void set_next_cell(CCA_Neighborhood *neighborhood, uint16_t x, uint16_t y, uint8_t state) {
    neighborhood->back_buffer[y*(neighborhood->width) + x] = state;
}



void tick_neighborhood(CCA_Neighborhood *neighborhood) {
    uint16_t cell_y, cell_x;
    int16_t neighbor_x, neighbor_y;
    int8_t offset_x, offset_y;
    uint8_t cell, neighbor, *tmp, range;
    uint8_t cell_remains;
    
    // The range tells us how many neighbors we have:
    range = neighborhood->config.range;

    // Loop through every cell:
    for (cell_y=0; cell_y<neighborhood->height; cell_y++) {
        for (cell_x=0; cell_x<neighborhood->width; cell_x++) {
            
            // Get the state of the current cell:
            cell = get_cell(neighborhood, cell_x, cell_y);
            
            // Start a counter for the threshold:
            cell_remains = neighborhood->config.threshold;
            
            // Loop through each neighbor:
            for (offset_y=-range; offset_y<=range; offset_y++) {
                for (offset_x=-range; offset_x<=range; offset_x++) {
                    
                    // Don't count the current cell as a neighbor:
                    if (offset_x == 0 && offset_y == 0) continue;

                    neighbor_x = offset_x + cell_x;
                    neighbor_y = offset_y + cell_y;
                    
                    if (neighborhood->config.type == NEIGHBORHOOD_VN) {
                        if (abs(offset_x) + abs(offset_y) > range) {
                            // Von Neumann uses Manhattan distance, so ignore 
                            continue;
                        }
                    }

                    if (neighborhood->config.wrap_edges) {
                        // We're wrapping arounf the edges:
                        if (neighbor_x >= neighborhood->width) neighbor_x -= neighborhood->width;
                        else if (neighbor_x < 0) neighbor_x += neighborhood->width;
                        if (neighbor_y >= neighborhood->height) neighbor_y -= neighborhood->height;
                        else if (neighbor_y < 0) neighbor_y += neighborhood->height;
                    }
                    else {
                        // Anything past an edge is ignored:
                        if (neighbor_x >= neighborhood->width) continue;
                        else if (neighbor_x < 0)  continue;
                        if (neighbor_y >= neighborhood->height)  continue;
                        else if (neighbor_y < 0)  continue;
                    }

                    // Now that we know this is a valid neighbor we get its state:
                    neighbor = get_cell(neighborhood, neighbor_x, neighbor_y);
                    
                    if (neighbor == (cell+1) % (neighborhood->config.n_states)) {
                        // The neighbor has the state that is successive to the current
                        // cell's state, decrement our threshold counter:
                        cell_remains--;
                    }
                }
                if (!cell_remains) {
                    // We've reached our threshold, no need to keep looking at neighbors:
                    break;
                }
            }
    
            if (!cell_remains) {
                // The number of neighbors with successive states is >= threshold, the
                // current cell is consumed by that state:
                set_next_cell(neighborhood, cell_x, cell_y, neighbor);
            }
            else {
                // The number of neighbors with successive states is < threshold, the
                // current cell remains in its current state:
                set_next_cell(neighborhood, cell_x, cell_y, cell);
            }
            
        }
    }
    
    // The back buffer is now updated, ping-pong to make it the front buffer:
    tmp = neighborhood->front_buffer;
    neighborhood->front_buffer = neighborhood->back_buffer;
    neighborhood->back_buffer = tmp;
}


void draw_neighborhood(CCA_Neighborhood *neighborhood, RGBmatrixPanelCPLD *display) {
    uint32_t y, x;
    uint16_t hue, color;
    uint8_t cell, r, g, b, n_states;
    
    n_states = neighborhood->config.n_states;

    if (neighborhood->config.include_black && n_states > 2) {
        // state 0=black, so map 1..n -> min_hue-max_hue, which is n-1 states;
        n_states--;
        
        // If we're including black but there's only 2 states, then decrementing
        // n_states here will lead to a divide by 0 in the map() call later.
    }

    for (y=0; y<neighborhood->height; y++) {
        for (x=0; x<neighborhood->width; x++) {
            cell = get_cell(neighborhood, x, y);

            if (neighborhood->config.include_black && cell == 0) {
                // State 0 is black:
                color = 0;
            }
            else {
                if (neighborhood->config.include_black) {
                    // We're including black and the cell state is non-zero,
                    // need to decrement cell state to properly map 1..n -> hue
                    cell--;
                }
                
                // Calculate the hue that corresponds to the cell state:
                hue = map(cell, 0, n_states-1, neighborhood->config.min_hue,
                    neighborhood->config.max_hue);
                
                // Convert to RGB:
                color = display->ColorHSV(hue, neighborhood->config.saturation, 
                    neighborhood->config.brightness, 1);
                
                // ColorHSV returns the color in RGB565 format, convert to RGB888:
                r = ((color >> 11) & 0x1F);
                g = ((color >> 5) & 0x3F);
                b = (color & 0x1F);
                
                // Now from RGB888 to RGB444 for the display:
                color = display->Color444(r, g, b);
            }

            // Finally draw it:
            display->drawPixel(x, y, color);
        }
    }
}
