#include "pebble.h"
#define UPDATE_MS 50 // Refresh rate in ms
#define mapsize 20    // Size of map: 20x20
#define IDCLIP false  // Walk thru walls
int32_t ZOOM=4;       // Number of pixels per map square

typedef struct PlayerStruct {
  int32_t x;                  // Player's X Position
  int32_t y;                  // Player's Y Position
  int32_t facing;             // Player Direction Facing (from 0 - TRIG_MAX_ANGLE)
} PlayerStruct;

static PlayerStruct player;
static Window *window;
static GRect window_frame;
static Layer *graphics_layer;

// ------------------------------------------------------------------------ //
//  Map Functions
// ------------------------------------------------------------------------ //
static int8_t map[mapsize * mapsize];  // int8 means cells can be from -127 to 128

void GenerateMap(int32_t startx, int32_t starty) {
  int32_t x, y;
  int8_t try;
  int32_t cursorx, cursory, next=1;
  
  cursorx = startx; cursory=starty;  
  for (int16_t i=0; i<mapsize*mapsize; i++) map[i] = 0; // Fill map with 0s
  
  while(true) {
    int32_t current = cursory * mapsize + cursorx;
    if((map[current] & 15) == 15) {  // If No Tries Left
      if(cursory==starty && cursorx==startx) {  // If back at the start, then we're done.
        map[current]=1;
        for (int16_t i=0; i<mapsize*mapsize; i++) map[i] = 1-map[i]; // invert map bits (0=empty, 1=wall, -1=special)
        return;
      }
      switch(map[current] >> 4) { // Else go back to the previous cell:  NOTE: If the 1st two bits are used, need to "&3" mask this
       case 0: cursorx++; break;
       case 1: cursory++; break;
       case 2: cursorx--; break;
       case 3: cursory--; break;
      }
      map[current]=next; next=1;
    } else {
      do try = rand()%4; while (map[current] & (1<<try));  // Pick Random Directions until that direction hasn't been tried
      map[current] |= (1<<try); // turn on bit in this cell saying this path has been tried
      // below is just: x=0, y=0; if(try=0)x=1; if(try=1)y=1; if(try=2)x=-1; if(try=3)y=-1;
      y=(try&1); x=y^1; if(try&2){y=(~y)+1; x=(~x)+1;} //  y = try's 1st bit, x=y with 1st bit xor'd (toggled).  Then "Two's Complement Negation" if try's 2nd bit=1
      
      // Move if spot is blank and every spot around it is blank (except where it came from)
      if((cursory+y)>0 && (cursory+y)<mapsize-1 && (cursorx+x)>0 && (cursorx+x)<mapsize-1) // Make sure not moving to or over boundary
        if(map[(cursory+y) * mapsize + cursorx + x]==0)                                    // Make sure not moving to a dug spot
          if((map[(cursory+y-1) * mapsize + cursorx+x]==0 || try==1))                      // Nothing above (unless came from above)
            if((map[(cursory+y+1) * mapsize + cursorx+x]==0 || try==3))                    // nothing below (unless came from below)
              if((map[(cursory+y) * mapsize + cursorx+x - 1]==0 || try==0))                // nothing to the left (unless came from left)
                if((map[(cursory+y) * mapsize + cursorx + x + 1]==0 || try==2)) {          // nothing to the right (unless came from right)
                  next=2;          
                  cursorx += x; cursory += y;                                              // All's good!  Let's move
                  map[cursory * mapsize + cursorx] |= ((try+2)%4) << 4; //record in new cell where ya came from -- the (try+2)%4 is because when you move west, you came from east
                }
    }
  } //End While True
}

int8_t getmap(int32_t x, int32_t y) {
  x=x>>6; y=y>>6;
  if (x<0 || x>=mapsize || y<0 || y>=mapsize) return -1;
  return map[(y * mapsize) + x];
}

void setmap(int32_t x, int32_t y, int8_t value) {
  x=x>>6; y=y>>6;
  if ((x >= 0) && (x < mapsize) && (y >= 0) && (y < mapsize))
    map[y * mapsize + x] = value;
}

int32_t min(int32_t a, int32_t b) {return (a<b)?a:b;}
int32_t max(int32_t a, int32_t b) {return (a>b)?a:b;}

// ------------------------------------------------------------------------ //
//  Drawing Functions
// ------------------------------------------------------------------------ //
void fill_window(GContext *ctx, uint8_t *data) {
  for(uint16_t y=0, yaddr=0; y<168; y++, yaddr+=20)
    for(uint16_t x=0; x<19; x++)
      ((uint8_t*)(((GBitmap*)ctx)->addr))[yaddr+x] = data[y%8];
}
  //uint8_t *ctx8 = ((uint8_t*)(((GBitmap*)ctx)->addr));
static void draw_map(GContext *ctx, GRect box, int32_t zoom) {
  //1-pixel-per-square map:
  //for (int16_t x = 0; x < mapsize; x++) for (int16_t y = 0; y < mapsize; y++) {graphics_context_set_stroke_color(ctx, map[y*mapsize+x]>0?1:0); graphics_draw_pixel(ctx, GPoint(x, y));}
  uint32_t *ctx32 = ((uint32_t*)(((GBitmap*)ctx)->addr));
  int32_t x,y, yaddr, xaddr, xbit;
  int32_t xonmap, yonmap, yonmapinit;

  xonmap = ((player.x*zoom)>>6) - (box.size.w/2);  // Divide by ZOOM to get map X coord, but rounds [-ZOOM to 0] to 0 and plots it, so divide by ZOOM after checking if <0
  yonmapinit = ((player.y*zoom)>>6) - (box.size.h/2);
  for(x=0; x<box.size.w; x++, xonmap++) {
    xaddr = (x+box.origin.x) >> 5;        // X memory address
    xbit = ~(1<<((x+box.origin.x) & 31)); // X bit shift level (normally wouldn't ~ it, but ~ is used more often than not)
    if(xonmap>=0 && xonmap<(mapsize*zoom)) {
      yonmap = yonmapinit;
      yaddr = box.origin.y * 5;           // Y memory address
      for(y=0; y<box.size.h; y++, yonmap++, yaddr+=5) {
        if(yonmap>=0 && yonmap<(mapsize*zoom)) {             // If within Y bounds
          if(map[(((yonmap/zoom)*mapsize))+(xonmap/zoom)]>0) //   Map shows a wall >0
            ctx32[xaddr + yaddr] |= ~xbit;                   //     White dot
          else                                               //   Map shows <= 0
            ctx32[xaddr + yaddr] &= xbit;                    //     Black dot
        } else {                                             // Else: Out of Y bounds
          ctx32[xaddr + yaddr] &= xbit;                      //   Black dot
        }
      }
    } else {                                // Out of X bounds: Black vertical stripe
      for(yaddr=box.origin.y*5; yaddr<((box.size.h + box.origin.y)*5); yaddr+=5)
        ctx32[xaddr + yaddr] &= xbit;
    }
  }

  graphics_context_set_fill_color(ctx, (time_ms(NULL, NULL) % 250)>125?0:1);                      // Flashing dot
  graphics_fill_rect(ctx, GRect((box.size.w/2)+box.origin.x - 1, (box.size.h/2)+box.origin.y - 1, 3, 3), 0, GCornerNone); // Square Cursor

  graphics_context_set_stroke_color(ctx, 1); graphics_draw_rect(ctx, GRect(box.origin.x-1, box.origin.y-1, box.size.w+2, box.size.h+2)); // White Border
}

static void draw_mini_map(GContext *ctx, uint32_t box_x, uint32_t box_y, uint32_t box_w, uint32_t box_h, int32_t zoom) {
  //1-pixel-per-square map:
  //for (int16_t x = 0; x < mapsize; x++) for (int16_t y = 0; y < mapsize; y++) {graphics_context_set_stroke_color(ctx, map[y*mapsize+x]>0?1:0); graphics_draw_pixel(ctx, GPoint(x, y));}
  uint32_t *ctx32 = ((uint32_t*)(((GBitmap*)ctx)->addr));
  uint32_t x,y, yaddr, xaddr, xbit;
  int32_t xonmap, yonmap, yonmapinit;

  xonmap = ((player.x*zoom)>>6) - (box_w/2);  // Divide by ZOOM to get map X coord, but rounds [-ZOOM to 0] to 0 and plots it, so divide by ZOOM after checking if <0
  yonmapinit = ((player.y*zoom)>>6) - (box_h/2);
  for(x=0; x<box_w; x++, xonmap++) {
    xaddr = (x+box_x) >> 5;        // X memory address
    xbit = ~(1<<((x+box_x) & 31)); // X bit shift level (normally wouldn't ~ it, but ~ is used more often than not)
    if(xonmap>=0 && xonmap<(mapsize*zoom)) {
      yonmap = yonmapinit;
      yaddr = box_y * 5;           // Y memory address
      for(y=0; y<box_h; y++, yonmap++, yaddr+=5) {
        if(yonmap>=0 && yonmap<(mapsize*zoom)) {             // If within Y bounds
          if(map[(((yonmap/zoom)*mapsize))+(xonmap/zoom)]>0) //   Map shows a wall >0
            ctx32[xaddr + yaddr] |= ~xbit;                   //     White dot
          else                                               //   Map shows <= 0
            ctx32[xaddr + yaddr] &= xbit;                    //     Black dot
        } else {                                             // Else: Out of Y bounds
          ctx32[xaddr + yaddr] &= xbit;                      //   Black dot
        }
      }
    } else {                                // Out of X bounds: Black vertical stripe
      for(yaddr=box_y*5; yaddr<((box_h + box_y)*5); yaddr+=5)
        ctx32[xaddr + yaddr] &= xbit;
    }
  }

  graphics_context_set_fill_color(ctx, (time_ms(NULL, NULL) % 250)>125?0:1);                      // Flashing dot
  graphics_fill_rect(ctx, GRect((box_w/2)+box_x - 1, (box_h/2)+box_y - 1, 3, 3), 0, GCornerNone); // Square Cursor

  graphics_context_set_stroke_color(ctx, 1); graphics_draw_rect(ctx, GRect(box_x-1, box_y-1, box_w+2, box_h+2)); // White Border
}

static void graphics_layer_update(Layer *me, GContext *ctx) {

  //draw_mini_map(ctx, 4, 4, 100, 100, ZOOM);
  //draw_mini_map(ctx, 4, 110, 40, 40, 4);
  draw_map(ctx, GRect(4, 4, 100, 100), ZOOM);
  draw_map(ctx, GRect(4, 110, 40, 40), 4);

  static char text[40];  //Buffer to hold text
  GRect textframe = GRect(24, 0, 100, 20);  // Text Box Position and Size
  snprintf(text, sizeof(text), "x:%ld y:%ld z:%ld", (player.x), (player.y), ZOOM);  // What text to draw
  graphics_context_set_fill_color(ctx, 0); graphics_fill_rect(ctx, textframe, 0, GCornerNone);  //Black Filled Rectangle
  graphics_context_set_stroke_color(ctx, 1); graphics_draw_rect(ctx, textframe);                //White Rectangle Border
  graphics_context_set_text_color(ctx, 1);  // Text Color
  graphics_draw_text(ctx, text, fonts_get_system_font(FONT_KEY_GOTHIC_14), textframe, GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);  //Write Text

}

/*
  //graphics_context_set_stroke_color(ctx, (time_ms(NULL, NULL) % 250)>125?0:1);  // Flashing dot
  //graphics_draw_pixel(ctx, GPoint((overlay.size.w/2)+overlay.origin.x, (overlay.size.h/2)+overlay.origin.y));
  for (int16_t x = 0; x < mapsize; x++) for (int16_t y = 0; y < mapsize; y++) {graphics_context_set_stroke_color(ctx,  map[y*mapsize+x]>0?1:0); graphics_draw_pixel(ctx, GPoint(x, y));}

  GRect overlay;
  overlay = GRect(24,24,100,100);
  GRect dot;
  
  for (int16_t x = 0; x < mapsize; x++)
    for (int16_t y = 0; y < mapsize; y++) {
      graphics_context_set_fill_color(ctx,  map[y*mapsize+x]>0?1:0);
      dot.origin.x = (x*ZOOM) + (overlay.size.w/2) - ((player.x*ZOOM)>>6);
      dot.origin.y = (y*ZOOM) + (overlay.size.h/2) - ((player.y*ZOOM)>>6);
      
      
      x - (overlay.size.w/2) + ((player.x*ZOOM)>>6);= (xonmap*ZOOM) 
      
      dot.size.w = ZOOM;
      dot.size.h = ZOOM;
      if(dot.origin.x+ZOOM>=0 && dot.origin.y+ZOOM>=0 && dot.origin.x<overlay.size.w && dot.origin.y<overlay.size.h) {
        if(dot.origin.x<0) {dot.size.w += dot.origin.x; dot.origin.x=0;}
        if(dot.origin.y<0) {dot.size.h += dot.origin.y; dot.origin.y=0;}
        if(dot.origin.x+dot.size.w>overlay.size.w) dot.size.w = (overlay.size.w - dot.origin.x);
        if(dot.origin.y+dot.size.h>overlay.size.h) dot.size.h = (overlay.size.h - dot.origin.y);
        
        dot.origin.x += overlay.origin.x;
        dot.origin.y += overlay.origin.y;
        graphics_fill_rect(ctx, dot, 0, GCornerNone);
     }
  }
*/
/*  
  int16_t MAP_X, MAP_Y;
  MAP_X = 22;
  MAP_Y = (152 / 2) - ((MAPSIZE * ZOOM) / 2);
  for (int16_t x = 0; x < MAPSIZE; x++)
    for (int16_t y = 0; y < MAPSIZE; y++) {
      graphics_context_set_stroke_color(ctx, getxmap(x,y)>0?0:1); graphics_draw_pixel(ctx, GPoint(x, y));
      graphics_context_set_fill_color(ctx, getxmap(x,y)>0?0:1);
      graphics_fill_rect(ctx, GRect((x*ZOOM)+MAP_X, (y*ZOOM)+MAP_Y, ZOOM, ZOOM), 0, GCornerNone);
    
      //if(getxmap(x,y)==0) graphics_context_set_fill_color(ctx, 0); else graphics_context_set_fill_color(ctx, 1); // Black if map cell = 0, else white
      //graphics_fill_rect(ctx, GRect((x*ZOOM)+MAP_X, (y*ZOOM)+MAP_Y, ZOOM, ZOOM), 0, GCornerNone);
      //if(getxmap(x,y)==2) {
        //graphics_context_set_fill_color(ctx, 1);
        //graphics_draw_pixel(ctx, GPoint((x*ZOOM)+MAP_X+2, (y*ZOOM)+MAP_Y+2)); // Special Dot
        //graphics_fill_rect(ctx, GRect((x*ZOOM)+MAP_X+3, (y*ZOOM)+MAP_Y+3, 2, 2), 0, GCornerNone); // Special Dot
      //} 
*/
// ------------------------------------------------------------------------ //
//  Timer Functions
// ------------------------------------------------------------------------ //
void walk(int32_t direction, int32_t distance) {
  int32_t dx = (cos_lookup(direction) * distance) / TRIG_MAX_RATIO;
  int32_t dy = (sin_lookup(direction) * distance) / TRIG_MAX_RATIO;
  if(getmap(player.x + dx, player.y) <= 0 || IDCLIP) player.x += dx;
  if(getmap(player.x, player.y + dy) <= 0 || IDCLIP) player.y += dy;
}

static void timer_callback(void *data) {
  AccelData accel=(AccelData){.x=0, .y=0, .z=0};          // all three are int16_t
  accel_service_peek(&accel);                             // read accelerometer
  //walk(player.facing, accel.y>>4);                        // walk based on accel.y  Technically: walk(accel.y * 64px / 1000);
  //player.facing += (accel.x<<4);                        //   spin
  int32_t dx = (accel.x>>4);
  int32_t dy = (accel.y>>4);
  if(getmap(player.x + dx, player.y) <= 0 || IDCLIP) player.x += dx;
  if(getmap(player.x, player.y - dy) <= 0 || IDCLIP) player.y -= dy;
  
  layer_mark_dirty(graphics_layer);
  app_timer_register(UPDATE_MS, timer_callback, NULL);
}

// ------------------------------------------------------------------------ //
//  Button Functions
// ------------------------------------------------------------------------ //
void     up_single_click_handler(ClickRecognizerRef recognizer, void *context) {ZOOM--; if(ZOOM<1) ZOOM=1;}
void   down_single_click_handler(ClickRecognizerRef recognizer, void *context) {ZOOM++; if(ZOOM>20) ZOOM=20;}
void select_single_click_handler(ClickRecognizerRef recognizer, void *context) {
  GenerateMap(mapsize/2, 0);
  player = (PlayerStruct){.x=(64*(mapsize/2)), .y=(-2 * 64), .facing=10000};
}
void click_config_provider(void *context) {
  window_single_click_subscribe(BUTTON_ID_UP, up_single_click_handler);
  window_single_click_subscribe(BUTTON_ID_SELECT, select_single_click_handler);
  window_single_click_subscribe(BUTTON_ID_DOWN, down_single_click_handler);
}

// ------------------------------------------------------------------------ //
//  Main Functions
// ------------------------------------------------------------------------ //
static void window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  GRect frame = window_frame = layer_get_frame(window_layer);

  graphics_layer = layer_create(frame);
  layer_set_update_proc(graphics_layer, graphics_layer_update);
  layer_add_child(window_layer, graphics_layer);

}

static void window_unload(Window *window) {
  layer_destroy(graphics_layer);
}

static void init(void) {
  window = window_create();
  window_set_window_handlers(window, (WindowHandlers) {
    .load = window_load,
    .unload = window_unload
  });
  window_stack_push(window, true /* Animated */);
  window_set_click_config_provider(window, click_config_provider);
  window_set_background_color(window, 0);
  
  accel_data_service_subscribe(0, NULL);  // Start accelerometer
  srand(time(NULL));  // Seed randomizer
  for (int16_t i=0; i<mapsize*mapsize; i++) map[i] = 0; // Fill map with 0s
  
  player = (PlayerStruct){.x=(64*(mapsize/2)), .y=(-2 * 64), .facing=10000};
  
  app_timer_register(UPDATE_MS, timer_callback, NULL);
}

static void deinit(void) {
  accel_data_service_unsubscribe();
  window_destroy(window);
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}
