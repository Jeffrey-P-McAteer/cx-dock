
// Gimme memmem
#define _POSIX_C_SOURCE 200809L
#define _GNU_SOURCE

// Highest-level config
#define DOCK_APP_TITLE "cx-dock"

// C (GNU C) headers
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <math.h>
#include <x86intrin.h>

// Linux headers
#include <unistd.h>
#include <sys/types.h>
#include <pwd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <signal.h>

// GL & X11 headers
#include <GL/gl.h>
#include <GL/glu.h>
#include <GL/glut.h>
#include <GL/glx.h>
#include <X11/Xatom.h>
#include <X11/extensions/Xrender.h>
#include <X11/extensions/Xrandr.h>
#include <X11/extensions/XShm.h>


// Utility macros
#define SET_ATOM_ATOM(NAME, VALUE) {\
        Atom name_atom = XInternAtom(Xdisplay, NAME, 0); \
        Atom value_atom = XInternAtom(Xdisplay, VALUE, 0); \
        XChangeProperty(Xdisplay, WindowHandle, name_atom, XA_ATOM, 32, PropModeReplace, (unsigned char*)&value_atom, 1); \
    }

#define SET_ATOM_STR(NAME, VALUE) {\
        Atom name_atom = XInternAtom(Xdisplay, NAME, 0); \
        XChangeProperty(Xdisplay, WindowHandle, name_atom, XA_ATOM, 32, PropModeReplace, (unsigned char*)& VALUE, 1); \
    }


static void die(const char *why) {
    fprintf(stderr, "%s", why);
    exit(0x01);
}


const char *user_home_dir;
static void init_user_home_dir() {
  if ((user_home_dir = getenv("HOME")) == NULL) {
    user_home_dir = getpwuid(getuid())->pw_dir;
  }
}


static void print_i3_wm_warnings() {
  const char* i3_req_config_line = "for_window [class=\"cx-dock\"] floating enable sticky enable border none";
  char i3_config_file_path[256];
  snprintf(i3_config_file_path, 255, "%s/.config/i3/config", user_home_dir);
  
  // mmap instead of reading for efficiency reasons
  int fd = open(i3_config_file_path, O_RDONLY);
  if (!fd) {
    printf("Could not read %s\n", i3_config_file_path);
    return;
  }
  int len = lseek(fd, 0, SEEK_END);
  void* data = mmap(0, len, PROT_READ, MAP_PRIVATE, fd, 0);
  if (data) {
    //char* data_char = (char*) data;
    //char* i3_req_config_location = strnstr(data_char, len, i3_req_config_line);
    char* i3_req_config_location = (char*) memmem((void*) data, len, (void*) i3_req_config_line, strlen(i3_req_config_line) );
    if (i3_req_config_location == NULL) {
      // Missing!
      printf("You have an i3 config file at \"%s\" which does not contain the config string \"%s\", which means the dock will likely have a border in it. Please ammend your config to prevent this bug.\n",
        i3_config_file_path, i3_req_config_line
      );
    }
    munmap(data, len);
  }
}

static Display* Xdisplay;
static void init_x11_disp() {
  Xdisplay = XOpenDisplay(NULL);
  if (!Xdisplay) {
    die("Couldn't connect to X server\n");
  }
}

static void ensure_compositor_running() {
  Atom prop_atom = XInternAtom(Xdisplay, "_NET_WM_CM_S0", False);
  bool we_have_compositor = XGetSelectionOwner(Xdisplay, prop_atom) != None;
  if (!we_have_compositor) {
    printf("No compositor detected, spawning compton...");
    // TODO check for N most common compositors & spawn one of those, possibly checking env var as well
    system("pgrep compton || (i3-msg exec compton && echo Used i3-exec to spawn compton) || ( ( compton & ) & echo Double-forked compton ) ");
  }
}

static int dpy_width;
static int dpy_height;
static void init_xrandr_assign_first_dpy_width_height() {
  XRRScreenResources* screens = XRRGetScreenResources(Xdisplay, DefaultRootWindow(Xdisplay));
  XRRCrtcInfo* info = NULL;
  int i;
  for (i = 0; i < screens->ncrtc; i++) {
    info = XRRGetCrtcInfo(Xdisplay, screens, screens->crtcs[i]);
    if (info != NULL) {
      printf("Detected display size = %dx%d\n", info->width, info->height);
      dpy_width = info->width;
      dpy_height = info->height;
      XRRFreeCrtcInfo(info);
      break;
    }
  }
  XRRFreeScreenResources(screens);
}

// Helper fn for init_x11_gl_window
static Bool WaitForMapNotify(Display *d, XEvent *e, char *arg) {
    return (e->type == MapNotify) && (e->xmap.window == *(Window*)arg);
}

static Window our_x11_win;
static Atom del_atom;
static GLXFBConfig fbconfig;
static Colormap cmap;
static int VisData[] = { // Used in GL init calls
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
static XVisualInfo* visual;
static XRenderPictFormat* pictFormat;
static Window WindowHandle;
static int win_x, win_y, win_w, win_h; // Dock bounding box
static GLXContext RenderContext;
static Window GLXWindowHandle;
static GLuint texture;
int const tex_width=256;
int const tex_height=256;
static void init_x11_gl_window() {
  int Xscreen = DefaultScreen(Xdisplay);
  Window Xroot = RootWindow(Xdisplay, Xscreen);

  int numfbconfigs;
  GLXFBConfig* fbconfigs = glXChooseFBConfig(Xdisplay, Xscreen, VisData, &numfbconfigs);
  for(int i = 0; i<numfbconfigs; i++) {
      visual = (XVisualInfo*) glXGetVisualFromFBConfig(Xdisplay, fbconfigs[i]);
      if(!visual) {
        continue;
      }
      pictFormat = XRenderFindVisualFormat(Xdisplay, visual->visual);
      if(!pictFormat) {
        continue;
      }
      if(pictFormat->direct.alphaMask > 0) { // Found a GL config w/ alpha capabilities!
          fbconfig = fbconfigs[i];
          break;
      }
  }

  /* Create a colormap - only needed on some X clients, eg. IRIX */
  cmap = XCreateColormap(Xdisplay, Xroot, visual->visual, AllocNone);

  XSetWindowAttributes attr;
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

  int attr_mask = 
      CWBackPixmap|
      CWColormap|
      CWBorderPixel|
      CWEventMask;    /* What's in the attr data */


  win_w = ((double) dpy_width * 0.75);
  win_h = 128;
  win_x = (dpy_width/2) - (win_w/2);
  win_y = dpy_height - win_h;

  WindowHandle = XCreateWindow(
    Xdisplay, /* Screen */
    Xroot, /* Parent */
    win_x, win_y, win_w, win_h,/* Position */
    0, /* Border */
    visual->depth,/* Color depth*/
    InputOutput,/* klass */
    visual->visual,/* Visual */
    attr_mask, &attr/* Attributes*/
  );

  if (!WindowHandle) {
    die("Couldn't create the window\n");
  }

  XTextProperty textprop;
  textprop.value = DOCK_APP_TITLE;
  textprop.encoding = XA_STRING;
  textprop.format = 8;
  textprop.nitems = strlen(DOCK_APP_TITLE);

  XSizeHints hints;
  hints.x = win_x;
  hints.y = win_y;
  hints.width = win_w;
  hints.height = win_h;
  hints.flags = USPosition|USSize;

  XWMHints* StartupState = XAllocWMHints();
  StartupState->initial_state = NormalState;
  StartupState->flags = StateHint;

  XSetWMProperties(Xdisplay, WindowHandle,
    &textprop, &textprop,/* Window title/icon title*/
    NULL, 0,/* Argv[], argc for program*/
    &hints, /* Start position/size*/
    StartupState,/* Iconised/not flag   */
    NULL
  );

  //SET_ATOM_ATOM("_NET_WM_WINDOW_TYPE", "_NET_WM_WINDOW_TYPE_DOCK"); // i3 borks a little
  SET_ATOM_ATOM("_NET_WM_STATE", "_NET_WM_STATE_ABOVE");

  SET_ATOM_STR("_NET_WM_NAME", DOCK_APP_TITLE);
  SET_ATOM_STR("_NET_WM_VISIBLE_NAME", DOCK_APP_TITLE);

  XStoreName(Xdisplay, WindowHandle, DOCK_APP_TITLE);

  XClassHint* class_hint = XAllocClassHint();
  XGetClassHint(Xdisplay, WindowHandle, class_hint);
  if (class_hint != NULL) {
      class_hint->res_name = DOCK_APP_TITLE;
      class_hint->res_class = DOCK_APP_TITLE;
      XSetClassHint(Xdisplay, WindowHandle, class_hint);
  }

  XFree(StartupState);

  /* Open it, wait for it to appear */
  XMapWindow(Xdisplay, WindowHandle);
  XEvent event;
  XIfEvent(Xdisplay, &event, WaitForMapNotify, (char*)&WindowHandle);

  /* Set the kill atom so we get a message when the user tries to close the window */
  if ((del_atom = XInternAtom(Xdisplay, "WM_DELETE_WINDOW", 0)) != None) {
      XSetWMProtocols(Xdisplay, WindowHandle, &del_atom, 1);
  }

  /* See if we can do OpenGL on this visual */
  int dummy;
  if (!glXQueryExtension(Xdisplay, &dummy, &dummy)) {
    die("OpenGL not supported by X server\n");
  }

  /* Create the OpenGL rendering context */
  RenderContext = glXCreateNewContext(Xdisplay, fbconfig, GLX_RGBA_TYPE, 0, True);
  if (!RenderContext) {
    die("Failed to create a GL context\n");
  }

  GLXWindowHandle = glXCreateWindow(Xdisplay, fbconfig, WindowHandle, NULL);

  /* Make it current */
  if (!glXMakeContextCurrent(Xdisplay, GLXWindowHandle, GLXWindowHandle, RenderContext)) {
    die("glXMakeCurrent failed for window\n");
  }

  glGenTextures(1, &texture);
  glBindTexture(GL_TEXTURE_2D, texture);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, tex_width, tex_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);


}

static void setup_shared_memory_for_rw_pixels() {
  // XShmGetImage / XShmPutImage

}

const float light_dir[] = {1,1,1,0};
const float light_color[] = {1,0.95,0.9,1};
static void render_one_frame() {
  glViewport(0,0,win_w,win_h);

  /* Clear the screen */
  // glClearColor(0.750,0.750,1.0,0.5);
  glClearColor(0.0, 0.0, 0.0, 0.0);
  glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);


  glEnable(GL_DEPTH_TEST);
  glEnable(GL_NORMALIZE);
  glDisable(GL_CULL_FACE);

  glLightfv(GL_LIGHT0, GL_POSITION, light_dir);
  glLightfv(GL_LIGHT0, GL_DIFFUSE, light_color);


  glEnable(GL_LIGHT0);
  glEnable(GL_LIGHTING);
  
  glMatrixMode(GL_PROJECTION);
  glLoadIdentity();
  //gluPerspective(45, (float)win_w/(float)win_h, 1, 10);

  glOrtho(0, win_w, 0, win_h, 0, 1); // Set coordinate system

  glMatrixMode(GL_MODELVIEW);
  glLoadIdentity();

  //GLfloat top_inset_px = (int) __rdtsc() % 240; // Testing
  GLfloat top_inset_px = 120;
  GLfloat polygon_verticies[] = {
    top_inset_px, win_h, 0, // Top-left
    0, 0, 0, // Bottom-left
    win_w, 0, 0, // Bottom-right
    win_w-top_inset_px, win_h, 0, // Top-right
  };
  // GLfloat polygon_verticies[] = {
  //   0, 60, 0, // Top-left
  //   0, 10, 0, // Bottom-left
  //   50, 10, 0, // Bottom-right
  //   50, 60, 0, // Top-right
  // };

  glEnableClientState(GL_VERTEX_ARRAY);

  glVertexPointer(3 /* how many dimensions? */, GL_FLOAT, 0, polygon_verticies);

  glDrawArrays(GL_POLYGON, 0, 4 /* len of polygon_verticies / dimensions */);

  glDisableClientState(GL_VERTEX_ARRAY);


  /* Swapbuffers */
  glXSwapBuffers(Xdisplay, GLXWindowHandle);
}

static volatile bool exit_flag;
static void do_render_loop() {
  exit_flag = false;

  printf("Beginning render loop...\n");
  
  int render_i = 0;
  XEvent event;
  XConfigureEvent *xc;
  bool window_moved_by_user = false;
  unsigned long last_cpu_ts = 0;
  int render_i_at_last_ts = 0;

  while (!exit_flag) {
    // Display stats
    unsigned long this_cpu_ts = __rdtsc();
    if (this_cpu_ts - last_cpu_ts > 1000000000) { // every billion CPU timestamps... (roughly once a second)
      printf("loops/s = %d\n", (render_i - render_i_at_last_ts) );
      last_cpu_ts = this_cpu_ts;
      render_i_at_last_ts = render_i;
    }
    // Book keeping
    render_i += 1;
    if (render_i > 10000000) {
      render_i = 0;
    }
    // Force window to be at the right location
    if (window_moved_by_user || render_i % 100 == 0) {
      XMoveResizeWindow(Xdisplay, WindowHandle, win_x, win_y, win_w, win_h);
      XSync(Xdisplay, False);
    }

    // Check all new X11 messages
    while (XPending(Xdisplay))
    {
        XNextEvent(Xdisplay, &event);
        switch (event.type)
        {
          case ClientMessage:
              if (event.xclient.data.l[0] == del_atom) {
                  exit_flag = true;
              }
          break;

          case ConfigureNotify:
              xc = &(event.xconfigure);
              // xc->width; xc->height;
              window_moved_by_user = true;
              break;
        }
    }

    // Re-draw window using GL commands
    render_one_frame();

  }
  printf("\nExiting render loop...\n");
}

void signal_handler(int sig_num) {
  exit_flag = true;
}


int main(int argc, char** argv) {
  signal(SIGINT, signal_handler);

  init_user_home_dir();
  print_i3_wm_warnings();
  
  glutInit(&argc, argv); // Must be called before GL commands issued

  init_x11_disp();
  ensure_compositor_running();
  init_xrandr_assign_first_dpy_width_height();
  init_x11_gl_window();
  setup_shared_memory_for_rw_pixels();

  do_render_loop();

  return 0;
}








