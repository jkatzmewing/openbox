// -*- mode: C++; indent-tabs-mode: nil; -*-

#ifdef    HAVE_CONFIG_H
#  include "../config.h"
#endif // HAVE_CONFIG_H

extern "C" {
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>

#ifdef    SHAPE
#  include <X11/extensions/shape.h>
#endif // SHAPE

#ifdef    XINERAMA
#  include <X11/extensions/Xinerama.h>
#endif // XINERAMA

#ifdef    HAVE_FCNTL_H
#  include <fcntl.h>
#endif // HAVE_FCNTL_H

#ifdef    HAVE_STDIO_H
#  include <stdio.h>
#endif // HAVE_STDIO_H

#ifdef HAVE_STDLIB_H
#  include <stdlib.h>
#endif // HAVE_STDLIB_H

#ifdef HAVE_STRING_H
#  include <string.h>
#endif // HAVE_STRING_H

#ifdef    HAVE_UNISTD_H
#  include <sys/types.h>
#  include <unistd.h>
#endif // HAVE_UNISTD_H

#ifdef    HAVE_SYS_SELECT_H
#  include <sys/select.h>
#endif // HAVE_SYS_SELECT_H

#ifdef    HAVE_SIGNAL_H
#  include <signal.h>
#endif // HAVE_SIGNAL_H

#ifdef    HAVE_SYS_WAIT_H
#  include <sys/types.h>
#  include <sys/wait.h>
#endif // HAVE_SYS_WAIT_H
}

#include <string>
using std::string;

#include "basedisplay.hh"
#include "gccache.hh"
#include "timer.hh"
#include "util.hh"


// X error handler to handle any and all X errors while the application is
// running
static bool internal_error = False;

BaseDisplay *base_display;

static int handleXErrors(Display *d, XErrorEvent *e) {
#ifdef    DEBUG
  char errtxt[128];

  XGetErrorText(d, e->error_code, errtxt, 128); // XXX: use this!!
  fprintf(stderr, "%s:  X error: %s(%d) opcodes %d/%d\n  resource 0x%lx\n",
          base_display->getApplicationName(), errtxt, e->error_code,
          e->request_code, e->minor_code, e->resourceid);
#else
  // shutup gcc
  (void) d;
  (void) e;
#endif // DEBUG

  if (internal_error) abort();

  return(False);
}


// signal handler to allow for proper and gentle shutdown

static void signalhandler(int sig) {

  static int re_enter = 0;

  switch (sig) {
  case SIGCHLD:
    int status;
    waitpid(-1, &status, WNOHANG | WUNTRACED);
    break;

  default:
    if (base_display->handleSignal(sig))
      return;

    fprintf(stderr, "%s:  signal %d caught\n",
            base_display->getApplicationName(), sig);

    if (! base_display->isStartup() && ! re_enter) {
      internal_error = True;

      re_enter = 1;
      fprintf(stderr, "shutting down\n");
      base_display->shutdown();
    }

    if (sig != SIGTERM && sig != SIGINT) {
      fprintf(stderr, "aborting... dumping core\n");
      abort();
    }

    exit(0);

    break;
  }
}


BaseDisplay::BaseDisplay(const char *app_name, const char *dpy_name) {
  application_name = app_name;

  run_state = STARTUP;

  ::base_display = this;

  struct sigaction action;

  action.sa_handler = signalhandler;
  action.sa_mask = sigset_t();
  action.sa_flags = SA_NOCLDSTOP | SA_NODEFER;

  sigaction(SIGPIPE, &action, NULL);
  sigaction(SIGSEGV, &action, NULL);
  sigaction(SIGFPE, &action, NULL);
  sigaction(SIGTERM, &action, NULL);
  sigaction(SIGINT, &action, NULL);
  sigaction(SIGCHLD, &action, NULL);
  sigaction(SIGHUP, &action, NULL);
  sigaction(SIGUSR1, &action, NULL);
  sigaction(SIGUSR2, &action, NULL);

  if (! (display = XOpenDisplay(dpy_name))) {
    fprintf(stderr,
            "BaseDisplay::BaseDisplay: connection to X server failed.\n");
    ::exit(2);
  } else if (fcntl(ConnectionNumber(display), F_SETFD, 1) == -1) {
    fprintf(stderr,
            "BaseDisplay::BaseDisplay: couldn't mark display connection "
            "as close-on-exec\n");
    ::exit(2);
  }

  display_name = XDisplayName(dpy_name);

#ifdef    SHAPE
  shape.extensions = XShapeQueryExtension(display, &shape.event_basep,
                                          &shape.error_basep);
#else // !SHAPE
  shape.extensions = False;
#endif // SHAPE

#ifdef    XINERAMA
  if (XineramaQueryExtension(display, &xinerama.event_basep,
                             &xinerama.error_basep) &&
      XineramaQueryVersion(display, &xinerama.major, &xinerama.minor)) {
#ifdef    DEBUG
    fprintf(stderr,
            "BaseDisplay::BaseDisplay: Found Xinerama version %d.%d\n",
            xinerama.major, xinerama.minor);
#endif // DEBUG
    xinerama.extensions = True;
  } else {
    xinerama.extensions = False;
  }
#endif // XINERAMA

  XSetErrorHandler((XErrorHandler) handleXErrors);

  screenInfoList.reserve(ScreenCount(display));
  for (int i = 0; i < ScreenCount(display); ++i)
    screenInfoList.push_back(ScreenInfo(this, i));

  NumLockMask = ScrollLockMask = 0;

  const XModifierKeymap* const modmap = XGetModifierMapping(display);
  if (modmap && modmap->max_keypermod > 0) {
    const int mask_table[] = {
      ShiftMask, LockMask, ControlMask, Mod1Mask,
      Mod2Mask, Mod3Mask, Mod4Mask, Mod5Mask
    };
    const size_t size = (sizeof(mask_table) / sizeof(mask_table[0])) *
      modmap->max_keypermod;
    // get the values of the keyboard lock modifiers
    // Note: Caps lock is not retrieved the same way as Scroll and Num lock
    // since it doesn't need to be.
    const KeyCode num_lock = XKeysymToKeycode(display, XK_Num_Lock);
    const KeyCode scroll_lock = XKeysymToKeycode(display, XK_Scroll_Lock);

    for (size_t cnt = 0; cnt < size; ++cnt) {
      if (! modmap->modifiermap[cnt]) continue;

      if (num_lock == modmap->modifiermap[cnt])
        NumLockMask = mask_table[cnt / modmap->max_keypermod];
      if (scroll_lock == modmap->modifiermap[cnt])
        ScrollLockMask = mask_table[cnt / modmap->max_keypermod];
    }
  }

  MaskList[0] = 0;
  MaskList[1] = LockMask;
  MaskList[2] = NumLockMask;
  MaskList[3] = LockMask | NumLockMask;
  MaskList[4] = ScrollLockMask;
  MaskList[5] = ScrollLockMask | LockMask;
  MaskList[6] = ScrollLockMask | NumLockMask;
  MaskList[7] = ScrollLockMask | LockMask | NumLockMask;
  MaskListLength = sizeof(MaskList) / sizeof(MaskList[0]);

  if (modmap) XFreeModifiermap(const_cast<XModifierKeymap*>(modmap));

  gccache = (BGCCache *) 0;
}


BaseDisplay::~BaseDisplay(void) {
  delete gccache;

  XCloseDisplay(display);
}


void BaseDisplay::eventLoop(void) {
  run();

  const int xfd = ConnectionNumber(display);

  while (run_state == RUNNING && ! internal_error) {
    if (XPending(display)) {
      XEvent e;
      XNextEvent(display, &e);
      process_event(&e);
    } else {
      fd_set rfds;
      timeval now, tm, *timeout = (timeval *) 0;

      FD_ZERO(&rfds);
      FD_SET(xfd, &rfds);

      if (! timerList.empty()) {
        const BTimer* const timer = timerList.top();

        gettimeofday(&now, 0);
        tm = timer->timeRemaining(now);

        timeout = &tm;
      }

      select(xfd + 1, &rfds, 0, 0, timeout);

      // check for timer timeout
      gettimeofday(&now, 0);

      // there is a small chance for deadlock here:
      // *IF* the timer list keeps getting refreshed *AND* the time between
      // timer->start() and timer->shouldFire() is within the timer's period
      // then the timer will keep firing.  This should be VERY near impossible.
      while (! timerList.empty()) {
        BTimer *timer = timerList.top();
        if (! timer->shouldFire(now))
          break;

        timerList.pop();

        timer->fireTimeout();
        timer->halt();
        if (timer->isRecurring())
          timer->start();
      }
    }
  }
}


void BaseDisplay::addTimer(BTimer *timer) {
  if (! timer) return;

  timerList.push(timer);
}


void BaseDisplay::removeTimer(BTimer *timer) {
  timerList.release(timer);
}


/*
 * Grabs a button, but also grabs the button in every possible combination
 * with the keyboard lock keys, so that they do not cancel out the event.

 * if allow_scroll_lock is true then only the top half of the lock mask
 * table is used and scroll lock is ignored.  This value defaults to false.
 */
void BaseDisplay::grabButton(unsigned int button, unsigned int modifiers,
                             Window grab_window, bool owner_events,
                             unsigned int event_mask, int pointer_mode,
                             int keyboard_mode, Window confine_to,
                             Cursor cursor, bool allow_scroll_lock) const {
  unsigned int length = (allow_scroll_lock) ? MaskListLength / 2:
                                              MaskListLength;
  for (size_t cnt = 0; cnt < length; ++cnt)
    XGrabButton(display, button, modifiers | MaskList[cnt], grab_window,
                owner_events, event_mask, pointer_mode, keyboard_mode,
                confine_to, cursor);
}


/*
 * Releases the grab on a button, and ungrabs all possible combinations of the
 * keyboard lock keys.
 */
void BaseDisplay::ungrabButton(unsigned int button, unsigned int modifiers,
                               Window grab_window) const {
  for (size_t cnt = 0; cnt < MaskListLength; ++cnt)
    XUngrabButton(display, button, modifiers | MaskList[cnt], grab_window);
}


const ScreenInfo* BaseDisplay::getScreenInfo(unsigned int s) const {
  if (s < screenInfoList.size())
    return &screenInfoList[s];
  return (const ScreenInfo*) 0;
}


BGCCache* BaseDisplay::gcCache(void) const {
  if (! gccache)
    gccache = new BGCCache(this, screenInfoList.size());
  
  return gccache;
}


ScreenInfo::ScreenInfo(BaseDisplay *d, unsigned int num) {
  basedisplay = d;
  screen_number = num;

  root_window = RootWindow(basedisplay->getXDisplay(), screen_number);

  rect.setSize(WidthOfScreen(ScreenOfDisplay(basedisplay->getXDisplay(),
                                             screen_number)),
               HeightOfScreen(ScreenOfDisplay(basedisplay->getXDisplay(),
                                              screen_number)));
  /*
    If the default depth is at least 8 we will use that,
    otherwise we try to find the largest TrueColor visual.
    Preference is given to 24 bit over larger depths if 24 bit is an option.
  */

  depth = DefaultDepth(basedisplay->getXDisplay(), screen_number);
  visual = DefaultVisual(basedisplay->getXDisplay(), screen_number);
  colormap = DefaultColormap(basedisplay->getXDisplay(), screen_number);
  
  if (depth < 8) {
    // search for a TrueColor Visual... if we can't find one...
    // we will use the default visual for the screen
    XVisualInfo vinfo_template, *vinfo_return;
    int vinfo_nitems;
    int best = -1;

    vinfo_template.screen = screen_number;
    vinfo_template.c_class = TrueColor;

    vinfo_return = XGetVisualInfo(basedisplay->getXDisplay(),
                                  VisualScreenMask | VisualClassMask,
                                  &vinfo_template, &vinfo_nitems);
    if (vinfo_return) {
      int max_depth = 1;
      for (int i = 0; i < vinfo_nitems; ++i) {
        if (vinfo_return[i].depth > max_depth) {
          if (max_depth == 24 && vinfo_return[i].depth > 24)
            break;          // prefer 24 bit over 32
          max_depth = vinfo_return[i].depth;
          best = i;
        }
      }
      if (max_depth < depth) best = -1;
    }

    if (best != -1) {
      depth = vinfo_return[best].depth;
      visual = vinfo_return[best].visual;
      colormap = XCreateColormap(basedisplay->getXDisplay(), root_window,
                                 visual, AllocNone);
    }

    XFree(vinfo_return);
  }

  // get the default display string and strip the screen number
  string default_string = DisplayString(basedisplay->getXDisplay());
  const string::size_type pos = default_string.rfind(".");
  if (pos != string::npos)
    default_string.resize(pos);

  display_string = string("DISPLAY=") + default_string + '.' +
    itostring(static_cast<unsigned long>(screen_number));
  
#ifdef    XINERAMA
  xinerama_active = False;

  if (d->hasXineramaExtensions()) {
    if (d->getXineramaMajorVersion() == 1) {
      // we know the version 1(.1?) protocol

      /*
         in this version of Xinerama, we can't query on a per-screen basis, but
         in future versions we should be able, so the 'activeness' is checked
         on a pre-screen basis anyways.
      */
      if (XineramaIsActive(d->getXDisplay())) {
        /*
           If Xinerama is being used, there there is only going to be one screen
           present. We still, of course, want to use the screen class, but that
           is why no screen number is used in this function call. There should
           never be more than one screen present with Xinerama active.
        */
        int num;
        XineramaScreenInfo *info = XineramaQueryScreens(d->getXDisplay(), &num);
        if (num > 0 && info) {
          xinerama_areas.reserve(num);
          for (int i = 0; i < num; ++i) {
            xinerama_areas.push_back(Rect(info[i].x_org, info[i].y_org,
                                          info[i].width, info[i].height));
          }
          XFree(info);

          // if we can't find any xinerama regions, then we act as if it is not
          // active, even though it said it was
          xinerama_active = True;
        }
      }
    }
  }
#endif // XINERAMA
}
