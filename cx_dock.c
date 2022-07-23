
// Gimme memmem
#define _POSIX_C_SOURCE 200809L
#define _GNU_SOURCE

#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <math.h>

#include <unistd.h>
#include <sys/types.h>
#include <pwd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <GL/gl.h>
#include <GL/glu.h>
#include <GL/glut.h>
#include <GL/glx.h>
#include <X11/Xatom.h>
#include <X11/extensions/Xrender.h>
#include <X11/extensions/Xrandr.h>


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




int main(int argc, char** argv) {
  init_user_home_dir();
  print_i3_wm_warnings();
  init_x11_disp();
  ensure_compositor_running();
  init_xrandr_assign_first_dpy_width_height();



  return 0;
}








