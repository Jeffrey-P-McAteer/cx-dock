///bin/sh -c "gcc -o /tmp/so-tx-gl-frame $0 -lm $(pkg-config --cflags --libs x11 gl glu glut xrender ) " && exec /tmp/so-tx-gl-frame
///bin/true ; exit 1

/*------------------------------------------------------------------------
  The simplest possible Linux OpenGL program? Maybe...
  Modification for creating a RGBA window (transparency with compositors)
  by Wolfgang 'datenwolf' Draxinger

  (c) 2002 by FTB. See me in comp.graphics.api.opengl

  (c) 2011 Wolfgang Draxinger. See me in comp.graphics.api.opengl and on StackOverflow

      License agreement: This source code is provided "as is". You
  can use this source code however you want for your own personal
  use. If you give this source code to anybody else then you must
  leave this message in it.

  --
  <\___/>
  / O O \
  \_____/  FTB.

  -- 
  datenwolf

------------------------------------------------------------------------*/

#define MONITOR_WIDTH 1920
#define MONITOR_HEIGHT 1080

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

#include <GL/gl.h>
#include <GL/glu.h>
#include <GL/glut.h>
#include <GL/glx.h>
#include <X11/Xatom.h>
#include <X11/extensions/Xrender.h>

typedef struct
{
    Visual *visual;
    VisualID visualid;
    int screen;
    unsigned int depth;
    int klass;
    unsigned long red_mask;
    unsigned long green_mask;
    unsigned long blue_mask;
    int colormap_size;
    int bits_per_rgb;
} XVisualInfo_CPP;

/*------------------------------------------------------------------------
  Something went horribly wrong
------------------------------------------------------------------------*/\
static void fatalError(const char *why)
{
    fprintf(stderr, "%s", why);
    exit(0x666);
}

/*------------------------------------------------------------------------
  Global vars
------------------------------------------------------------------------*/
static int Xscreen;

static Atom del_atom;
static Colormap cmap;
static Display *Xdisplay;
static XVisualInfo_CPP *visual;
static XRenderPictFormat *pictFormat;
static GLXFBConfig *fbconfigs, fbconfig;
static int numfbconfigs;
static GLXContext RenderContext;
static Window Xroot, WindowHandle, GLXWindowHandle;
static int width, height;   /* Size of the window */

int const tex_width=512;
int const tex_height=512;
static GLuint texture;

static int VisData[] = {
GLX_RENDER_TYPE, GLX_RGBA_BIT,
GLX_DRAWABLE_TYPE, GLX_WINDOW_BIT,
GLX_DOUBLEBUFFER, True,
GLX_RED_SIZE, 1,
GLX_GREEN_SIZE, 1,
GLX_BLUE_SIZE, 1,
GLX_ALPHA_SIZE, 1,
GLX_DEPTH_SIZE, 1,
None
};
/*------------------------------------------------------------------------
  Create a window
------------------------------------------------------------------------*/
static Bool WaitForMapNotify(Display *d, XEvent *e, char *arg)
{
    return (e->type == MapNotify) && (e->xmap.window == *(Window*)arg);
}

static void print_i3_wm_warnings() {
    printf("WARNING: if running i3, add the line \"for_window [class=\"cx-dock\"] floating enable sticky enable border none\" to your config to prevent border.\n");
    // TODO search for that string & print warning
}

static void ensure_compositor_running() {
    printf("TODO improve compositor check; currently we just fork compton if not running");
    system("pgrep compton || (i3-msg exec compton && echo Used i3-exec to spawn compton) || ( ( compton & ) & echo Double-forked compton ) ");
}

static void createTheWindow()
{
    XEvent event;
    int x,y, attr_mask;
    XSizeHints hints;
    XWMHints *StartupState;
    XTextProperty textprop;
    XSetWindowAttributes attr;
    static char *title = "cx-dock";

    print_i3_wm_warnings();
    ensure_compositor_running();

    /* Connect to the X server */
    Xdisplay = XOpenDisplay(NULL);
    if (!Xdisplay) {
        fatalError("Couldn't connect to X server\n");
    }
    Xscreen = DefaultScreen(Xdisplay);
    Xroot = RootWindow(Xdisplay, Xscreen);

    fbconfigs = glXChooseFBConfig(Xdisplay, Xscreen, VisData, &numfbconfigs);
    for(int i = 0; i<numfbconfigs; i++) {
        visual = (XVisualInfo_CPP*) glXGetVisualFromFBConfig(Xdisplay, fbconfigs[i]);
        if(!visual)
            continue;

        pictFormat = XRenderFindVisualFormat(Xdisplay, visual->visual);
        if(!pictFormat)
            continue;

        if(pictFormat->direct.alphaMask > 0) {
            fbconfig = fbconfigs[i];
            break;
        }
    }

    /* Create a colormap - only needed on some X clients, eg. IRIX */
    cmap = XCreateColormap(Xdisplay, Xroot, visual->visual, AllocNone);

    /* Prepare the attributes for our window */
    attr.colormap = cmap;


    attr.border_pixel = 0;
    attr.event_mask =
        StructureNotifyMask |
        EnterWindowMask |
        LeaveWindowMask |
        ExposureMask |
        ButtonPressMask |
        ButtonReleaseMask |
        OwnerGrabButtonMask |
        KeyPressMask |
        KeyReleaseMask;

    attr.background_pixmap = None;

    attr_mask = 
        CWBackPixmap|
        CWColormap|
        CWBorderPixel|
        CWEventMask;    /* What's in the attr data */

    /* Create the window */
    width = DisplayWidth(Xdisplay, DefaultScreen(Xdisplay))/2;
    height = DisplayHeight(Xdisplay, DefaultScreen(Xdisplay))/2;
    x=width/2, y=height/2;

    /* Create the window */
    WindowHandle = XCreateWindow(   Xdisplay, /* Screen */
                    Xroot, /* Parent */
                    x, y, width, height,/* Position */
                    1,/* Border */
                    visual->depth,/* Color depth*/
                    InputOutput,/* klass */
                    visual->visual,/* Visual */
                    attr_mask, &attr);/* Attributes*/

    if( !WindowHandle ) {
        fatalError("Couldn't create the window\n");
    }

    /* Configure it...  (ok, ok, this next bit isn't "minimal") */
    textprop.value = (unsigned char*)title;
    textprop.encoding = XA_STRING;
    textprop.format = 8;
    textprop.nitems = strlen(title);

    hints.x = x;
    hints.y = y;
    hints.width = width;
    hints.height = height;
    hints.flags = USPosition|USSize;

    StartupState = XAllocWMHints();
    StartupState->initial_state = NormalState;
    StartupState->flags = StateHint;

    XSetWMProperties(Xdisplay, WindowHandle,&textprop, &textprop,/* Window title/icon title*/
            NULL, 0,/* Argv[], argc for program*/
            &hints, /* Start position/size*/
            StartupState,/* Iconised/not flag   */
            NULL);

    /* Specify we are a dock, most window managers respect it & avoid borders, keep on top, etc. */
    // Atom window_type_atom = XInternAtom(Xdisplay, "_NET_WM_WINDOW_TYPE", 0);
    // Atom window_type_dock_atom = XInternAtom(Xdisplay, "_NET_WM_WINDOW_TYPE_DOCK", 0);
    // XChangeProperty(Xdisplay, WindowHandle, window_type_atom, XA_ATOM, 32, PropModeReplace, (unsigned char*)&window_type_dock_atom, 1);

    // Atom window_state_atom = XInternAtom(Xdisplay, "_NET_WM_STATE", 0);
    // Atom window_state_above_atom = XInternAtom(Xdisplay, "_NET_WM_STATE_ABOVE", 0);
    // XChangeProperty(Xdisplay, WindowHandle, window_state_atom, XA_ATOM, 32, PropModeReplace, (unsigned char*)&window_state_above_atom, 1);

#define SET_ATOM_ATOM(NAME, VALUE) {\
        Atom name_atom = XInternAtom(Xdisplay, NAME, 0); \
        Atom value_atom = XInternAtom(Xdisplay, VALUE, 0); \
        XChangeProperty(Xdisplay, WindowHandle, name_atom, XA_ATOM, 32, PropModeReplace, (unsigned char*)&value_atom, 1); \
    }

#define SET_ATOM_STR(NAME, VALUE) {\
        Atom name_atom = XInternAtom(Xdisplay, NAME, 0); \
        XChangeProperty(Xdisplay, WindowHandle, name_atom, XA_ATOM, 32, PropModeReplace, (unsigned char*)& VALUE, 1); \
    }
    
    //SET_ATOM_ATOM("_NET_WM_WINDOW_TYPE", "_NET_WM_WINDOW_TYPE_DOCK"); // i3 borks a little
    SET_ATOM_ATOM("_NET_WM_STATE", "_NET_WM_STATE_ABOVE");

    SET_ATOM_STR("_NET_WM_NAME", "cx-dock");
    SET_ATOM_STR("_NET_WM_VISIBLE_NAME", "cx-dock");

    XStoreName(Xdisplay, WindowHandle, "cx-dock");

    XClassHint* class_hint = XAllocClassHint();
    XGetClassHint(Xdisplay, WindowHandle, class_hint);
    if (class_hint != NULL) {
        class_hint->res_name = "cx-dock";
        class_hint->res_class = "cx-dock";
        XSetClassHint(Xdisplay, WindowHandle, class_hint);
    }

#undef SET_ATOM_STR
#undef SET_ATOM_ATOM



    XFree(StartupState);

    /* Open it, wait for it to appear */
    XMapWindow(Xdisplay, WindowHandle);
    XIfEvent(Xdisplay, &event, WaitForMapNotify, (char*)&WindowHandle);

    /* Set the kill atom so we get a message when the user tries to close the window */
    if ((del_atom = XInternAtom(Xdisplay, "WM_DELETE_WINDOW", 0)) != None) {
        XSetWMProtocols(Xdisplay, WindowHandle, &del_atom, 1);
    }

    /* Finally query dimensions of screen & map to the bottom of the screen */
    // int screen_num = DefaultScreen(Xdisplay);
    // int screen_w = DisplayWidth(Xdisplay, screen_num);
    // int screen_h = DisplayHeight(Xdisplay, screen_num);
    // printf("screen_w=%d screen_h=%d\n", screen_w, screen_h);

    // int delta_x, delta_y;
    // Window child; // ???
    // XWindowAttributes xwa;
    // XTranslateCoordinates(Xdisplay, WindowHandle, Xroot, 0, 0, &delta_x, &delta_y, &child );
    // XGetWindowAttributes(Xdisplay, WindowHandle, &xwa); // Window-relative coordinates
    // int screen_x = delta_x - xwa.x;
    // int screen_y = delta_y - xwa.y;
    // printf("screen_x=%d screen_y=%d\n", screen_x, screen_y);

    unsigned int win_w = (int) ((double) MONITOR_WIDTH * 0.8);
    unsigned int win_h = 128;
    int win_x = (MONITOR_WIDTH / 2) - (win_w / 2);
    int win_y = MONITOR_HEIGHT - win_h;
    printf("win_w=%d win_h=%d win_x=%d win_y=%d\n", win_w, win_h, win_x, win_y);
    XMoveResizeWindow(Xdisplay, WindowHandle, win_x, win_y, win_w, win_h);
    XSync(Xdisplay, False);


}
/*------------------------------------------------------------------------
  Create the OpenGL rendering context
------------------------------------------------------------------------*/
static void createTheRenderContext()
{
    /* See if we can do OpenGL on this visual */
    int dummy;
    if (!glXQueryExtension(Xdisplay, &dummy, &dummy)) {
        fatalError("OpenGL not supported by X server\n");
    }

    /* Create the OpenGL rendering context */
    RenderContext = glXCreateNewContext(Xdisplay, fbconfig, GLX_RGBA_TYPE, 0, True);
    if (!RenderContext) {
        fatalError("Failed to create a GL context\n");
    }

    GLXWindowHandle = glXCreateWindow(Xdisplay, fbconfig, WindowHandle, NULL);

    /* Make it current */
    if (!glXMakeContextCurrent(Xdisplay, GLXWindowHandle, GLXWindowHandle, RenderContext)) {
        fatalError("glXMakeCurrent failed for window\n");
    }

    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, tex_width, tex_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
}
/*------------------------------------------------------------------------
  Window messages
------------------------------------------------------------------------*/
static int updateTheMessageQueue()
{
    XEvent event;
    XConfigureEvent *xc;

    while (XPending(Xdisplay))
    {
        XNextEvent(Xdisplay, &event);
        switch (event.type)
        {
        case ClientMessage:
            if (event.xclient.data.l[0] == del_atom)
            {
                return 0;
            }
        break;

        case ConfigureNotify:
            xc = &(event.xconfigure);
            width = xc->width;
            height = xc->height;
            break;
        }
    }
    return 1;
}


/*------------------------------------------------------------------------
  Redraw the window
------------------------------------------------------------------------*/
float const light_dir[]={1,1,1,0};
float const light_color[]={1,0.95,0.9,1};

static void redrawTheWindow()
{
    int size;
    static float a=0;
    static float b=0;
    static float c=0;

    glViewport(0,0,width,height);

    /* Clear the screen */
    // glClearColor(0.750,0.750,1.0,0.5);
    glClearColor(0.0,0.0,0.0,0.);
    glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    gluPerspective(45, (float)width/(float)height, 1, 10);

    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();


    glEnable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);

    glLightfv(GL_LIGHT0, GL_POSITION, light_dir);
    glLightfv(GL_LIGHT0, GL_DIFFUSE, light_color);

    glTranslatef(0,0,-5);

    glRotatef(a, 1, 0, 0);
    glRotatef(b, 0, 1, 0);
    glRotatef(c, 0, 0, 1);

    glEnable(GL_LIGHT0);
    glEnable(GL_LIGHTING);
    glutSolidTeapot(1);

    a=fmod(a+0.1, 360.);
    b=fmod(b+0.5, 360.);
    c=fmod(c+0.25, 360.);

    /* Swapbuffers */
    glXSwapBuffers(Xdisplay, GLXWindowHandle);
}

/*------------------------------------------------------------------------
  Program entry point
------------------------------------------------------------------------*/
int main(int argc, char *argv[])
{
    /* instead of a triangle I wanted a teapot. GLUT has it. 
       GLUT is NOT used for window creation, but just the teapot
       primitive. Nevertheless it must be initialized */
    glutInit(&argc, argv);

    createTheWindow();
    createTheRenderContext();

    while (updateTheMessageQueue()) {
        redrawTheWindow();
    }

    return 0;
}

