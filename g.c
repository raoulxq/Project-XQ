/*
vi:ts=8:sw=4:sts=4
 * Common tag list commands:
 * TlistToggle
 * TlistUpdate
 *
 * TODO:
 * * Implement some kind of sparse data structure (possibly octree)
 * * Create models for non-terrain things
 * * Have mobs rendered complete separate from terrain
 * * Improve path finding/do actual path finding
 *
 * Ok, so I'm totally throwing everything away... Trees, fire, sheep,
 * to make implementation of a 3D world easier. I can reinvent them
 * as soon as everything is running.
 *
 * Plus, there are some other changes:
 * I'm staying with a block size of 1 byte. Dirt and water will have 
 * some extra bits to implement pressure, because I really like that 
 * idea of pressure not only for water, but also for dirt and potentially 
 * other fluids: Lava, oil, ...
 * 
 * Dirt will be a "fluid" in a sense as that there can be a maximum of 
 * 1/4 difference between two blocks... If it's more, they will erode.
 * 
 * Water is different in that the water level will always level out, 
 * i.e. an ocean would have the same level of water everywhere. There 
 * must be a rule with water of nearly the same pressure, so that it 
 * doesn't need cpu cycles for just swapping back and forth. Maybe
 * restrict cpu cycles for fluids in general, like they're only leveled
 * out every other world update.
 *
 * What we'll get for free with this system is that we can implement 
 * erosion... Soil can contain water and if water flows through the 
 * soil, there is a chance that it will carry some soil with it...
 * That means, if it rains, the water will take the soil with it if 
 * we let enough time pass. Isn't it awesome? It is!
 *
 * Data structure:
 * 
 * 1 Byte (for now):
 * Every byte has it's unique look
 *
 * 00000wxx Soil, where w is if it's wet and xx is the height
 * 00001xxx Water, where xxx is the height
 * 0001000x Rock, x is height
 * 0001001x free
 * 000101xx Tree, xx is the thickness of the stem
 * 
 * So in fact there are still many blocks left for future plans.
 * 
 * More thoughts about terrain generation:
 * 
 * Both soil and water will 'rain' from above. Maybe only the amount that get's
 * washed out at the corners of the world.  Of course, 1000 times the amount of
 * water would rain down than the amount of soil. But still, hey.
 * 
 * I could make water randomly disappear, thus draining it out of underground
 * and making it rain on the top.
 *
 * Underground, during initial terrain generation time, empty blocks between
 * stones tend to grow together, building caves. Maybe the same for soil. This
 * way, we'll be able to have vains. The same goes for coal, iron, etc. They
 * will be generated randomly, but then they'll stick together, maybe all
 * minerals of a type in 64 blocks distance will be clumped together, so that
 * it looks like a vain.
 *
 * And crap blender is too complicated to work with... Not really, but I want
 * to code now.
 */

#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/timeb.h>
#include <errno.h>
#include "SDL/SDL.h"
#include <GL/gl.h>
#include <GL/glu.h>
#include <GL/glut.h>

#define ID_EMPTY        0x00
#define ID_SOIL_DRY     0x01
#define ID_SOIL_WET     0x02
#define ID_WATER_MIN    0x03
#define ID_WATER_MAX    0x14
#define ID_WATER_NUM    (ID_WATER_MAX-ID_WATER_MIN+1)
#define ID_ROCK_HALF    0x15
#define ID_ROCK_FULL    0x16
#define ID_FREE_0       0x17
#define ID_FREE_1       0x18
#define ID_TREE_MIN     0x19
#define ID_TREE_MAX     0x1c
#define ID_MAX          ID_TREE_MAX

#define IS_WATER(x) (x >=ID_WATER_MIN && x <= ID_WATER_MAX)
#define IS_SOIL(x) (x == ID_SOIL_DRY || x == ID_SOIL_WET)

#define CGUI            0xf0f0f0
#define CSOIL_DRY       0x7E3117
#define CSOIL_WET       0x463E41
#define CWATER          (78*256+73*16+68)

Uint8 * plane = NULL;
Uint32 pitch;     // *y = *y+pitch, lenght of a line
Uint32 potch;     // *z = *z+potch, size of a plane
Sint32 sizex=30; // Must be at least psheep.range
Sint32 sizey=30; // Must be at least psheep.range
Sint32 sizez=20;  // Not in use yet
Sint32 bytes_per_pixel=1;
Sint32 win_sizex=1100;
Sint32 win_sizey=1100;
int list_blocks;
GLfloat gridx=30;
GLfloat gridy=30;
GLfloat gridz=20; // Not in use yet
GLfloat anglex=-40.0;
GLfloat angley=0.0;
GLfloat anglez=0.0;
GLfloat translatex= 0.4;
GLfloat translatey= 0.0;
GLfloat translatez=-7.0;
Sint32 gridox=0; // Grid offset x
Sint32 gridoy=0; // Grid offset y
Sint32 gridoz=0;
Sint32 curposx;
Sint32 curposy;
Sint32 curposz;
SDLKey keys[SDLK_LAST];
Sint32 mousebuttons[6]; // 1=SDL_BUTTON_LEFT, 2=SDL_BUTTON_MIDDLE, 3=SDL_BUTTON_RIGHT, 4=SDL_BUTTON_WHEELUP, 5=SDL_BUTTON_WHEELDOWN
Sint32 is_fullscreen = 0;
#define out_buffer_size 2000
char out_buffer[out_buffer_size];
int out_buffer_usage=0;

void out_num(int n);

typedef struct {
    Sint32 x, y, z;
} pos_t;

typedef struct {
    Sint32 posx;
    Sint32 posy;
    Sint32 posz;
    Sint32 tx; // The current target (usually prey)
    Sint32 ty;
    Sint32 food;
} mob_t;

typedef struct {
    Sint32 maxnum;               // Maxnum for array
    Sint32 num;                  // This many live currently/are instantiated initially
    Sint32 speed;                // The animal can walk this far in one step
    Sint32 range;                // The animal can find prey/food (this many steps+1) away
    Sint32 food_max;             // The animal can be this full
    Sint32 food_min_offspring;   // The animal needs at least this much food to have offspring
    Sint32 food_after_offspring; // Amount of food the animal has after it had offspring or after game startup
    Sint32 food_offspring;       // Amount of food for newborn animals
    Sint32 food_prey;            // Amount of food an animal receives when eating prey/grass (should probably dependent on the prey but doesn't currently)
    Sint32 offspring_rate;       // Probability to create offspring
} animal_props_t;

typedef struct {
    Sint32 maxnum;      // Maxnum for array
    Sint32 growth_loop;
    Sint32 num;
    Sint32 burn_diameter;
    Sint32 burn_duration;
    Sint32 burn_ignition; // probability to be ignited
} plant_props_t;

plant_props_t pgrass = {
    0           // maxnum (dummy)
        ,1000000// growth_loop
        ,0      // num
        ,2      // burn_diameter <- not in use
        ,3      // burn_duration
        ,200    // burn_ignition
};

plant_props_t pwood = {
    0           // maxnum (dummy)
        ,9000000// growth_loop
        ,0      // num
        ,8      // burn_diameter <- not in use
        ,200    // burn_duration
        ,200    // burn_ignition
};

#define fire_len 1500
plant_props_t pfire = {
    fire_len    // maxnum
        ,1000000000// growth_loop
        ,0      // num
        ,7      // burn_diameter
        ,0      // burn_duration
        ,0      // burn_ignition
};

mob_t afire[fire_len];

#define max_range 32
#define prey_len ((max_range*2+1)*(max_range*2+1))

#define sheep_len 1000000
animal_props_t psheep = {
    sheep_len // maximum
        ,0    // num
        ,1    // speed
        ,64   // range
        ,2000 // food_max
        ,1700 // food_min_offspring
        ,1000 // food_after_offspring
        ,25   // food_offspring
        ,15   // food_prey
        ,30   // offspring_rate
};

mob_t asheep[sheep_len];

#define dtime_len 100
static Uint64 adtime[dtime_len];
static struct timeb dtime_cur, dtime_old;
static Sint32 dtime_idx;

void dtime_init() {
    int i;
    ftime(&dtime_cur);
    ftime(&dtime_old);
    dtime_idx=0;
    for (i=0; i<dtime_len; i++)
        adtime[i] = 0;
}

void dtime_reinit() {
    dtime_idx=0;
}

void dtime_print() {
    int i;
    if (dtime_idx > 0)
        printf("%lu", (long unsigned int) adtime[0]);
    for (i=1; i<dtime_idx; i++)
        printf(",%lu", (long unsigned int) adtime[i]);
}

Uint64 dtime_diff() {
    int s, ms;
    dtime_old = dtime_cur;
    ftime(&dtime_cur);
    s = dtime_cur.time - dtime_old.time;
    ms = dtime_cur.millitm - dtime_old.millitm;
    return (1000*s+ms);
}

void dtime_checkpoint() {
    if (dtime_idx < dtime_len-1)
        adtime[dtime_idx++] += dtime_diff();
}


static Sint64 next = 1;
/* RAND_MAX assumed to be 32767 */
Uint32 rxrand(Uint32 n) {
    next = next * 1103515245 + 12345;
    return((Uint32)(next/65536) % n);
}
Uint32 rxrand32k(void) {
    next = next * 1103515245 + 12345;
    return((Uint32)(next/65536) % 32768);
}
Uint32 rxrand128(void) {
    next = next * 1103515245 + 12345;
    return((Uint32)(next/65536) % 127);
}

void ustrcpy(Uint8 * d, Uint8 * s, Sint32 l) {
    if (!d || !s) { fprintf(stderr, "NULL string or zero length in ustrlen(%u)\n", l); exit(0); }
    for (;;) {
	*d=*s;
	if (l==0 || *s==0) return;
	d++; s++; l--;
    }
}

Sint32 ustrlen(Uint8 * s) {
    Sint32 l=0;
    if (!s) { fprintf(stderr, "NULL string in ustrlen()\n"); exit(0); }
    for (;;) {
	if (s[l] == 0) return l;
	l++;
    }
}

Sint32 updatescreendata() {
    // FIXME: Should free old surface first
    // Update some variables for current screen
    static Sint32 old_sizex=0;
    static Sint32 old_sizey=0;
    static Sint32 old_sizez=0;

    if (old_sizex != sizex || old_sizey != sizey || old_sizez != sizez) {
        pitch = sizex * bytes_per_pixel;
        potch = pitch * sizey;
        plane = (Uint8 *) realloc((void *) plane, potch*sizez);
        if (plane == NULL) {
            fprintf(stderr, "Failed to allocate memory for %dx%dx%d pixels.\n", sizex, sizey, sizez);
            exit(5);
        }

        old_sizex=sizex;
        old_sizey=sizey;
        old_sizez=sizez;
    }

    return 0;
}

Sint32 absx(Sint32 x) {
    return (x+5*sizex)%sizex;
}

Sint32 absy(Sint32 y) {
    return (y+5*sizey)%sizey;
}

Sint32 absz(Sint32 z) {
    // TODO: Currently the world wraps around x, y and z edges... It shouldn't wrap around z
    return (z+5*sizez)%sizez;
}

Uint8 getat(Sint32 x, Sint32 y, Sint32 z) {
    x=absx(x);
    y=absy(y);
    if (z < 0 || z >= sizez)
        return ID_EMPTY;
//     z=absz(z);
    return *(Uint8 *) (plane + x*bytes_per_pixel + y*pitch + z*potch);
}

Sint32 checkat(Sint32 x, Sint32 y, Sint32 z) {
    if (x >= 0 && y >= 0 && z >= 0 && x <= sizex-1 && y <= sizey-1 && z <= sizez-1)
        return 1;
    else
        return 0;
}

Sint32 checkat2(Sint32 x, Sint32 y, Sint32 z) {
    if (x >= 1 && y >= 1 && z >= 1 && x <= sizex-2 && y <= sizey-2 && z <= sizez-2)
        return 1;
    else
        return 0;
}

void putat(Sint32 x, Sint32 y, Sint32 z, Uint8 c) {
    x=absx(x);
    y=absy(y);
    z=absz(z);
    *(Uint8 *) (plane + x*bytes_per_pixel + y*pitch + z*potch) = c;
}

Sint32 init_gfx(int argc, char * argv[]) 
{
    /***************
     * Init SDL
     **************/
    Sint32 flags;

    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
	printf("Unable to initialize SDL: %s\n", SDL_GetError());
	return 5;
    }
    atexit(SDL_Quit);

//     SDL_GL_SetAttribute( SDL_GL_RED_SIZE, 5 );
//     SDL_GL_SetAttribute( SDL_GL_GREEN_SIZE, 5 );
//     SDL_GL_SetAttribute( SDL_GL_BLUE_SIZE, 5 );
//     SDL_GL_SetAttribute( SDL_GL_DEPTH_SIZE, 16 );
    SDL_GL_SetAttribute( SDL_GL_DOUBLEBUFFER, 1 );

    flags = SDL_HWSURFACE|SDL_RESIZABLE|SDL_OPENGL;
    if (argc == 2 && strcmp(argv[1], "-fs") == 0) {
        is_fullscreen = 1;
    }
    if (argc == 2
            && (strcmp(argv[1], "+fs") == 0 
                || strcmp(argv[1], "-nofs") == 0 
                || strcmp(argv[1], "--nofullscreen") == 0)) {
        is_fullscreen = 0;
    }
    if (is_fullscreen) {
        const SDL_VideoInfo *i = SDL_GetVideoInfo();
        win_sizex = i->current_w;
        win_sizey = i->current_h;
        flags |= SDL_OPENGL | SDL_FULLSCREEN;
    }

    SDL_Surface * screen;
    screen = SDL_SetVideoMode(win_sizex, win_sizey, 32, flags);

    if (screen == NULL) {
	printf("Unable to set video mode: %s\n", SDL_GetError());
	return 10;
    }
    updatescreendata();

    /********************
     * Init OpenGL/glut
     *******************/
    glutInit(&argc, argv);
    glShadeModel( GL_SMOOTH );
    glCullFace( GL_BACK );
    glFrontFace( GL_CCW );
    glEnable( GL_CULL_FACE );
    // I believe anti aliasing is already set in the video driver on HD5870, 
    // I don't see a difference and it seems to be anti-aliased already.
    glEnable( GL_LINE_SMOOTH );
    glEnable( GL_BLEND );

    /* Light */
    GLfloat mats_specular [] = {1.0, 1.0,  1.0, 1.0}; glMaterialfv(GL_FRONT , GL_SPECULAR , mats_specular);
    GLfloat mats_shininess[] = {50.0};                glMaterialfv(GL_FRONT , GL_SHININESS, mats_shininess);
    GLfloat light_position[] = {0.0, 0.0, -1.0, 0.0}; glLightfv   (GL_LIGHT0, GL_POSITION , light_position);
    //GLfloat light_ambient [] = {1.0, 1.0,  0.8, 1.0}; glLightfv   (GL_LIGHT0, GL_AMBIENT  , light_ambient);
    //glEnable(GL_LIGHTING);
    //glEnable(GL_LIGHT0);
    glEnable(GL_DEPTH_TEST);
    glClear(GL_DEPTH_BUFFER_BIT|GL_COLOR_BUFFER_BIT);
    glViewport( 0, 0, win_sizex, win_sizey );
    glMatrixMode( GL_PROJECTION );
    glLoadIdentity( );
    gluPerspective( 60.0, (float)win_sizex/(float)win_sizey, 0.1, 1024.0 );

    return 0;
}

// Draw a cube with frame and translate 1 to the right
void cube_right(int color) 
{
    glColor3ub(color/65536, (color/256)&255, color&255);
    glutSolidCube(.95);
    glColor3ub(128, 128, 128);
    glutWireCube(.95);
    glTranslatef( 1., 0., 0. );
}

void cube_right_height(int color, GLfloat height) 
{
    glPushMatrix();

    glTranslatef(0.,0.,(height-1)/2.);
    glScalef(1., 1., height);

    glColor3ub(color/65536, (color/256)&255, color&255);
    glutSolidCube(.95);

    glColor3ub(128, 128, 128);
    glutWireCube(.95);

    glPopMatrix();

    glTranslatef( 1., 0., 0. );
}

void display_grid()
{
    int x,y,z;
    glClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT );

    glMatrixMode( GL_MODELVIEW );
    glLoadIdentity( );
    glTranslatef( translatex, translatey, translatez );
    glRotatef( anglex, 1., 0., 0.);
    glRotatef( angley, 0., 1., 0.);
    glRotatef( anglez, 0., 0., 1.);

    GLfloat m=(gridx > gridy ? gridx : gridy);
    glScalef(4./m, 4./m, 4./m);
    glTranslatef( -m/2., -m/2., -m/2. );

    // Smoothly go to the current position of gridox/y
    // Stay within a window of 2*win boxes
    // Move "move" boxes at once to make movement more smooth
    const int move=10;
    const int win=10;
    static int ddx;
    int dx=absx(gridox-curposx);
    if (dx > win && dx < sizex/2) ddx=move;
    if (dx < sizex-win && dx > sizex/2) ddx=-move;
    if (ddx > 0) {
        ddx--;
        curposx=absx(curposx+1);
    }
    if (ddx < 0) {
        ddx++;
        curposx=absx(curposx-1);
    }
    static int ddy;
    int dy=absy(gridoy-curposy);
    if (dy > win && dy < sizey/2) ddy=move;
    if (dy < sizey-win && dy > sizey/2) ddy=-move;
    if (ddy > 0) {
        ddy--;
        curposy=absy(curposy+1);
    }
    if (ddy < 0) {
        ddy++;
        curposy=absy(curposy-1);
    }

    int block;
    for (z=0; z<gridz; z++) {
        for (y=0; y<gridy; y++) {
            for (x=0; x<gridx; x++) {
                block = getat(x,y,z);
                glCallList(list_blocks+block);
            }
            glTranslatef( -gridx, 1., 0. );
        }
        glTranslatef( 0., -gridy, 1. );
    }
}

void drawchar(int * c)
{
    int i;
    glPushMatrix();
    for (i=0; i<4; i++) {
        if (c[i] & 8) { cube_right(CGUI); } else { glTranslatef(1.,0.,0.); }
        if (c[i] & 4) { cube_right(CGUI); } else { glTranslatef(1.,0.,0.); }
        if (c[i] & 2) { cube_right(CGUI); } else { glTranslatef(1.,0.,0.); }
        if (c[i] & 1) { cube_right(CGUI); } else { glTranslatef(1.,0.,0.); }
        glTranslatef( -4., 1., 0.);
    }
    glPopMatrix();
    glTranslatef( 4., 0., 0.);
}

void out_display_text()
{
    glMatrixMode( GL_MODELVIEW );
    glLoadIdentity( );
    glTranslatef( 0.0, 0.0, -20.0 );
    glRotatef( -5., 1., 0., 0.0 );

    glScalef(0.15, 0.15, 0.15);
    glTranslatef( -100, 60, 0.0 );

    glPushMatrix();
    /*
        33: ! 46: .  90: Z                    
        34: " 47: /  91: [                    
        35: # 48: 0  92: \
        36: $ 57: 9  93: ]                    
        37: % 58: :  94: ^                    
        38: & 59: ;  95: _                    
        39: ' 60: <  96: `                    
        40: ( 61: =  97: a                    
        41: ) 62: >  122: z                   
        42: * 63: ?  123: {                   
        43: + 64: @  124: |                   
        44: , 65: A  125: }                   
        45: - 66: B  126: ~                   
    */

    static int base=0;
    if (!base) {
        int n[][5] = {
            {43,0,4,14,4}
            ,{45,0,0,14,0}
            ,{48,1,0,0,0}
            ,{49,0,0,4,0}
            ,{50,0,0,10,0}
            ,{51,0,8,4,2}
            ,{52,0,10,0,10}
            ,{53,0,10,4,10}
            ,{54,0,10,10,10}
            ,{55,0,10,14,10}
            ,{56,0,14,10,14}
            ,{57,0,14,14,14}
            ,{-1,0,0,0,0}
        };

        base = glGenLists(128);
        int i;
        int c;
        for (i=0; n[i][0] > 0; i++) {
            c=n[i][0]+base;
            glNewList(c, GL_COMPILE);
            drawchar(n[i]+1);
            glEndList();
        }
    }

    char * t = out_buffer;
    int idx=0;
    while (t[idx]) {
        if (t[idx] == '\n') {
            glPopMatrix();
            glTranslatef( 0., -4., 0.);
            glPushMatrix();
        } else {
            glCallList(base+t[idx]);
        }
        idx++;
    }
    glPopMatrix();
    out_buffer[out_buffer_usage++]=0;
//     fprintf(stderr, "\n\nx%sx\n\n", out_buffer);
    out_buffer_usage=0;
}

void out_text(char * t) {
    while (*t && out_buffer_usage < (out_buffer_size-1)) {
        out_buffer[out_buffer_usage++] = *t++;
    }
}

void out_num(int n)
{
    static char d[20];
    int upper_bound=10;
    int len=0;
    int c;
    if (n<0) {
        n=-n;
        out_text("-");
    } else {
        out_text("+");
    }
    while (n > (upper_bound-1) && upper_bound < 1000000000) {
        upper_bound *= 10;
    }
    while (upper_bound > 1) {
        upper_bound /= 10;
        c = (n/upper_bound);
        n -= c*upper_bound;
        d[len++]='0'+c;
    }
    d[len++]=0;
    out_text(d);
    out_text("\n");
}


void gen_world() 
{
    // World generation!
    Sint32 x=0,y=0,z=0;
    Uint8  block;
    for (z=0; z<sizez; z++) { // The sky is at least 3 high
        for (y=0; y<sizey; y++) {
            for (x=0; x<sizex; x++) {
                block = ID_EMPTY;
                if (rxrand(0x2) == 1) block = ID_SOIL_DRY;
                if (rxrand(0x10) == 1) block = ID_WATER_MIN+rxrand(ID_WATER_NUM);
                if (z > sizez-3) block = ID_EMPTY;
//                 block = ID_EMPTY;
                putat(x, y, z, block);
            }
        }
    }

}

Sint32 water_level(Uint8 block)
{
    if (block == ID_EMPTY   ) return 0;
    if (block == ID_SOIL_DRY) return 0;
    if (block == ID_SOIL_WET) return 1;
    if (block >= ID_WATER_MIN && block <= ID_WATER_MAX) return 1+block-ID_WATER_MIN;
    return -1;
}

Uint8 water_level_increase(Uint8 block) 
{
    if (block == ID_EMPTY   ) return ID_WATER_MIN;
    if (block == ID_SOIL_DRY) return ID_SOIL_WET;
    if (block >= ID_WATER_MIN && block < ID_WATER_MAX) return block+1;
    return -2;
}

Uint8 water_level_decrease(Uint8 block) 
{
    if (block == ID_SOIL_WET ) return ID_SOIL_DRY;
    if (block == ID_WATER_MIN) return ID_EMPTY;
    if (block >  ID_WATER_MIN && block <= ID_WATER_MAX) return block-1;
    return -2;
}

/******************************************
 * Purpose:
 * 
 * Blocks fall down if they can,
 * i.e. dirt, water, ... but not rock
 ******************************************/
void gravity()
{
    Sint32 x=0,y=0,z=0;
    Uint8  block;
    Uint8  above_block;
    Uint8  next_block;
    Uint8  next_2_block;
    Uint8  diagonal_above_block;
    Uint8  diagonal_two_above_block;

    Sint32 step=0x04; // for testing, should probably be 0x02 or so
    if (keys[SDLK_LSHIFT] || keys[SDLK_RSHIFT])
        step = 0x1;
    for (z=0; z<sizez-1; z+=rxrand(step)+1) {
        for (y=0; y<sizey; y+=rxrand(step)+1) {
            for (x=0; x<sizex; x+=rxrand(step)+1) {
                block = getat(x, y, z);
                above_block = getat(x, y, z+1);
                if (block == ID_EMPTY) {
                    // Drop a block one down
                    if (IS_SOIL(above_block) || IS_WATER(above_block))
                    {
                        block = above_block;
                        above_block = ID_EMPTY;
                        putat(x, y, z+1, above_block);
                        putat(x, y, z, block);
                    }
                } else if(block >= ID_WATER_MIN && block < ID_WATER_MAX) {
                    // Make water drop down
                    while (block >= ID_WATER_MIN
                            && block < ID_WATER_MAX
                            && IS_WATER(above_block)) {
                        block++;
                        if (above_block > ID_WATER_MIN)
                            above_block--;
                        else
                            above_block = ID_EMPTY;

                    }

                    // Make other items drop down, water will go up
                    if (IS_SOIL(above_block)) {
                        Uint8 f=above_block;
                        above_block = block;
                        block = f;
                    }

                    putat(x, y, z+1, above_block);
                    putat(x, y, z, block);
                } else if (block == ID_SOIL_DRY) {

                    // Water makes dry dirt blocks wet
                    if (IS_WATER(above_block)) {
                        block = ID_SOIL_WET;
                        if (above_block > ID_WATER_MIN)
                            above_block--;
                        else
                            above_block = ID_EMPTY;
                    }

                    // If a wet block is above a dry block, the water goes down
                    if (block == ID_SOIL_DRY && above_block == ID_SOIL_WET) {
                        Uint8 f=above_block;
                        above_block = block;
                        block = f;
                    }
                    putat(x, y, z+1, above_block);
                    putat(x, y, z, block);
                }
            }
        }
    }

    // Take diagonal movements out of the loop to avoid interference
    int cnt;
    for (cnt=0; cnt<(sizex*sizey*sizez); cnt+=rxrand(step*step*step)+1) {
        x = rxrand(sizex);
        y = rxrand(sizey);
        z = rxrand(sizez-1);
        Sint32 nextx=x, nexty=y;
        Sint32 next2x=x, next2y=y;

        if (rxrand(2) == 1)
            nexty=absy(y+1-2*rxrand(2));
        else
            nextx=absx(x+1-2*rxrand(2));

        while (next2x == x && next2y == y) {
            next2x = absx(x-2+rxrand(5));
            next2y = absy(y-2+rxrand(5));
        }

        block = getat(x, y, z);
        above_block = getat(x, y, z+1);
        next_block = getat(nextx, nexty, z);
        next_2_block = getat(next2x, next2y, z);
        diagonal_above_block = getat(nextx, nexty, z+1);
        diagonal_two_above_block = getat(nextx, nexty, z+2);

//         Sint32 wl_next_block  = water_level(next_block);
        Sint32 wl_next_2_block  = water_level(next_2_block);
        Sint32 wl_block = water_level(block);

        // TODO: "Horizontal" movement of water

        // Water diagonal movement from above
        if (z < sizez-1
                && (block == ID_EMPTY
                    || block == ID_SOIL_DRY 
                    || (block >= ID_WATER_MIN && block < ID_WATER_MAX)
                    )
                && (IS_WATER(diagonal_above_block) || diagonal_above_block == ID_SOIL_WET)) {
            // Increase block
            if (block == ID_EMPTY) {
                block = ID_WATER_MIN;
            } else if (block == ID_SOIL_DRY) {
                block = ID_SOIL_WET;
            } else {
                block++;
            }

            // Decrease diagonal_above_block
            if (diagonal_above_block == ID_SOIL_WET) {
                diagonal_above_block = ID_SOIL_DRY;
            } else if (diagonal_above_block == ID_WATER_MIN) {
                diagonal_above_block = ID_EMPTY;
            } else {
                diagonal_above_block--;
            }

            putat(x, y, z, block);
            putat(nextx, nexty, z+1, diagonal_above_block);
        }

        // Water levelling out on the same level, if water_level(block) < water_level(next_block)
        if (wl_next_2_block > 0
                && wl_block >= 0
                && block != ID_SOIL_WET
                && wl_block < ID_WATER_NUM-1
                && wl_next_2_block > wl_block+1)
        {
            wl_block++;
            wl_next_2_block--;
            block = water_level_increase(block);
            next_2_block = water_level_decrease(next_2_block);
            putat(x, y, z, block);
            putat(next2x, next2y, z, next_2_block);
        }

        // If two blocks above and one left/right/forward/back is a dirt block, it moves down 
        // one diagonally
        if (z < sizez-1 
                && (block       == ID_EMPTY || IS_WATER(block))) {
            if (IS_SOIL(diagonal_above_block)) {
                Uint8 f=diagonal_above_block;
                diagonal_above_block = block;
                block = f;
                putat(x, y, z, block);
                putat(nextx, nexty, z+1, diagonal_above_block);
            }
        }
    }

//     int j;
//     for (j=0; j<20; j++) {
//         putat(rxrand(sizex), gridy/2, sizez-1, ID_SOIL_DRY);
//         putat(gridx/2, rxrand(sizey), sizez-1, ID_WATER_MIN);
//     }
}

void init_display_lists()
{
// #define ID_SOIL_DRY 0x00
// #define ID_SOIL_WET 0x01
// #define ID_WATER_MIN    0x02
// #define ID_WATER_MAX    0x0f
// #define ID_WATER_NUM    (ID_WATER_MAX-ID_WATER_MIN)
// #define ID_ROCK_HALF    0x10
// #define ID_ROCK_FULL    0x11
// #define ID_FREE_0       0x12
// #define ID_FREE_1       0x13
// #define ID_TREE_MIN     0x14
// #define ID_TREE_MAX     0x17
// #define ID_MAX          ID_TREE_MAX
// #define ID_EMPTY        0xff

    list_blocks = glGenLists(256);
    int l=list_blocks;
    int i;

    glNewList(l+ID_SOIL_DRY, GL_COMPILE); cube_right(CSOIL_DRY); glEndList();
    glNewList(l+ID_SOIL_WET, GL_COMPILE); cube_right(CSOIL_WET); glEndList();
    
    for (i=0; i<ID_WATER_NUM; i++) {
        glNewList(l+ID_WATER_MIN+i, GL_COMPILE); cube_right_height(CWATER, (i+1)*1.0/ID_WATER_NUM); glEndList();
    }

    glNewList(l+ID_EMPTY         , GL_COMPILE); glTranslatef( 1., 0., 0. );         glEndList();
}

int main(int argc, char * argv[]) 
{
    Sint32 running = 1;
    Sint32 key=0;
    Sint32 i;

    if (init_gfx(argc, argv) != 0)
        return 5;

    init_display_lists();

    gen_world();

    dtime_init();

    static Sint32 generation = 0;
    while (running) {

        if (generation == 0) 
            printf("Generation,Sheep,Grass,Wood,Fire,asheep[0].posx/y\n");
        if (generation % 100 == 0) {
            dtime_print(); printf("    \r");
//             printf("||%d,%d,%d,%d,%d,%d,%d    \r", generation, psheep.num, pgrass.num, pwood.num, pfire.num, asheep[0].posx, asheep[0].posy);
            fflush(stdout);
            dtime_init();
        }

        dtime_reinit();

        generation++;
	SDL_Event event;
	while (SDL_PollEvent(&event)) {
	    switch (event.type) {
		case SDL_QUIT:
		    running = 0;
		    return 0;
		case SDL_KEYDOWN:
		    {
			key = event.key.keysym.sym;
			keys[key]=1;
			if (key == 27) running = 0;
		    } break;
		case SDL_KEYUP:
		    {
			key = event.key.keysym.sym;
			keys[key]=0;
		    } break;
		case SDL_VIDEORESIZE:
		    {
		    } break;
		case SDL_MOUSEMOTION:
		    {
                        if (mousebuttons[SDL_BUTTON_LEFT]) {
                            if (event.motion.xrel > -100 && event.motion.xrel < 100)
                                anglez+=event.motion.xrel/3.0;
                            if (event.motion.yrel > -100 && event.motion.yrel < 100)
                                anglex+=event.motion.yrel/3.0;
                        }
		    } break;
                case SDL_MOUSEBUTTONUP:
                    if (event.button.button >= 1 && event.button.button <= 5)
                        mousebuttons[event.button.button] = 0;
                    break;
                case SDL_MOUSEBUTTONDOWN:
                    {
                        int button = event.button.button;
                        if (button >= 1 && button <= 5)
                            mousebuttons[button] = 1;
                        // Zoom
                        if (button == SDL_BUTTON_WHEELUP || button == SDL_BUTTON_WHEELDOWN) {
                            if (button == SDL_BUTTON_WHEELUP) {
                                translatez += 0.4;
                                translatex -= 1.0*(event.button.x-win_sizex/2)/win_sizex;
                                translatey += 1.0*(event.button.y-win_sizey/2)/win_sizey;
                            }
                            if (button == SDL_BUTTON_WHEELDOWN) {
                                translatez -= 0.4;
                                translatex -= 1.0*(event.button.x-win_sizex/2)/win_sizex;
                                translatey += 1.0*(event.button.y-win_sizey/2)/win_sizey;
                            }
                        }
                    }
                    break;
	    }

	}

        int jumpsize=1;
        if (keys[SDLK_LSHIFT] || keys[SDLK_RSHIFT])
            jumpsize=8;
        if (keys['k'] || keys[SDLK_UP])    gridoy+=jumpsize;
        if (keys['j'] || keys[SDLK_DOWN])  gridoy-=jumpsize;
        if (keys['h'] || keys[SDLK_LEFT])  gridox-=jumpsize;
        if (keys['l'] || keys[SDLK_RIGHT]) gridox+=jumpsize;


        dtime_checkpoint();

        gravity();

        dtime_checkpoint();

        display_grid();

        dtime_checkpoint(); 

        out_display_text();

        dtime_checkpoint();

        SDL_GL_SwapBuffers( );

        dtime_checkpoint();

        {
            const Sint32 target_delay=17; // 30FPS ~ 33ms; 60FPS ~ 17ms; 120FPS ~ 8ms
            static Sint32 oldtime;
            Sint32 newtime=0;
            Sint32 dtimediff;
            for (i=0; i<dtime_idx; i++)
                newtime += adtime[i];
            if (abs(oldtime-newtime) > 30) {
                oldtime = newtime;
            } else {
                dtimediff = newtime-oldtime;
                oldtime = newtime;
                if (dtimediff < (target_delay))
                    SDL_Delay(target_delay-dtimediff);
            }
        }

        dtime_checkpoint();  // 6

    }
    printf("\n");
    return 0;
}
