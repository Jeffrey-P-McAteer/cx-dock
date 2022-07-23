
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
  Atom del_atom;
  if ((del_atom = XInternAtom(Xdisplay, "WM_DELETE_WINDOW", 0)) != None) {
      XSetWMProtocols(Xdisplay, WindowHandle, &del_atom, 1);
  }


}

static void setup_shared_memory_for_rw_pixels() {
  // XShmGetImage / XShmPutImage
}

static volatile bool exit_flag;
static void do_render_loop() {
  exit_flag = false;
  printf("Beginning render loop...\n");
  while (!exit_flag) {
    // TODO

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
  init_x11_disp();
  ensure_compositor_running();
  init_xrandr_assign_first_dpy_width_height();
  init_x11_gl_window();
  setup_shared_memory_for_rw_pixels();

  do_render_loop();

  return 0;
}








