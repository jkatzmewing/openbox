// -*- mode: C++; indent-tabs-mode: nil; c-basic-offset: 2; -*-

#ifdef HAVE_CONFIG_H
# include "../config.h"
#endif

extern "C" {
#ifdef    SHAPE
#include <X11/extensions/shape.h>
#endif // SHAPE
}

#include "frame.hh"
#include "client.hh"
#include "otk/display.hh"

namespace ob {

OBFrame::OBFrame(const OBClient *client, const otk::Style *style)
  : _client(client),
    _screen(otk::OBDisplay::screenInfo(client->screen()))
{
  assert(client);
  assert(style);
  
  _style = 0;
  loadStyle(style);

  _window = createFrame();
  assert(_window);

  grabClient();
}


OBFrame::~OBFrame()
{
  releaseClient(false);
}


void OBFrame::loadStyle(const otk::Style *style)
{
  assert(style);

  // if a style was previously set, then 'replace' is true, cause we're
  // replacing a style
  // NOTE: if this is false, then DO NOT DO SHIT WITH _window, it doesnt exist
  bool replace = (_style);

  if (replace) {
    // XXX: do shit here whatever
  }
  
  _style = style;

  // XXX: load shit like this from the style!
  _size.left = _size.top = _size.bottom = _size.right = 2;

  if (replace) {
    resize();
    
    XSetWindowBorderWidth(otk::OBDisplay::display, _window,
                          _style->getBorderWidth());

    XMoveWindow(otk::OBDisplay::display, _client->window(),
                _size.left, _size.top);

    // XXX: make everything redraw
  }
}


void OBFrame::resize()
{
  XResizeWindow(otk::OBDisplay::display, _window,
                _size.left + _size.right + _client->area().width(),
                _size.top + _size.bottom + _client->area().height());
  // XXX: more is gunna have to happen here
}


void OBFrame::shape()
{
#ifdef SHAPE
  if (!_client->shaped()) {
    // clear the shape on the frame window
    XShapeCombineMask(otk::OBDisplay::display, _window, ShapeBounding,
                      _size.left - 2,//frame.margin.left - frame.border_w,
                      _size.top - 2,//frame.margin.top - frame.border_w,
                      None, ShapeSet);
  } else {
    // make the frame's shape match the clients
    XShapeCombineShape(otk::OBDisplay::display, _window, ShapeBounding,
                       _size.left - 2,
                       _size.top - 2,
                       _client->window(), ShapeBounding, ShapeSet);

  int num = 0;
    XRectangle xrect[2];

    /*
    if (decorations & Decor_Titlebar) {
    xrect[0].x = xrect[0].y = -frame.border_w;
    xrect[0].width = frame.rect.width();
    xrect[0].height = frame.title_h + (frame.border_w * 2);
    ++num;
    }

    if (decorations & Decor_Handle) {
    xrect[1].x = -frame.border_w;
    xrect[1].y = frame.rect.height() - frame.margin.bottom +
    frame.mwm_border_w - frame.border_w;
    xrect[1].width = frame.rect.width();
    xrect[1].height = frame.handle_h + (frame.border_w * 2);
    ++num;
    }*/

    XShapeCombineRectangles(otk::OBDisplay::display, _window,
                            ShapeBounding, 0, 0, xrect, num,
                            ShapeUnion, Unsorted);
  }
#endif // SHAPE
}


void OBFrame::grabClient()
{
  
  XGrabServer(otk::OBDisplay::display);

  // select the event mask on the frame
  XSelectInput(otk::OBDisplay::display, _window, SubstructureRedirectMask);

  // reparent the client to the frame
  XSelectInput(otk::OBDisplay::display, _client->window(),
               OBClient::event_mask & ~StructureNotifyMask);
  XReparentWindow(otk::OBDisplay::display, _client->window(), _window,
                  _size.left, _size.top);
  XSelectInput(otk::OBDisplay::display, _client->window(),
               OBClient::event_mask);

  // raise the client above the frame
  XRaiseWindow(otk::OBDisplay::display, _client->window());
  // map the client so it maps when the frame does
  XMapWindow(otk::OBDisplay::display, _client->window());
  
  XUngrabServer(otk::OBDisplay::display);

  resize();
  shape();
}


void OBFrame::releaseClient(bool remap)
{
  // check if the app has already reparented its window to the root window
  XEvent ev;
  if (XCheckTypedWindowEvent(otk::OBDisplay::display, _client->window(),
                             ReparentNotify, &ev)) {
    remap = true; // XXX: why do we remap the window if they already
                  // reparented to root?
  } else {
    // according to the ICCCM - if the client doesn't reparent to
    // root, then we have to do it for them
    XReparentWindow(otk::OBDisplay::display, _client->window(),
                    _screen->getRootWindow(),
                    _client->area().x(), _client->area().y());
  }

  // if we want to remap the window, do so now
  if (remap)
    XMapWindow(otk::OBDisplay::display, _client->window());
}


Window OBFrame::createFrame()
{
  XSetWindowAttributes attrib_create;
  unsigned long create_mask = CWBackPixmap | CWBorderPixel | CWColormap |
                              CWOverrideRedirect | CWEventMask;

  attrib_create.background_pixmap = None;
  attrib_create.colormap = _screen->getColormap();
  attrib_create.override_redirect = True;
  attrib_create.event_mask = EnterWindowMask | LeaveWindowMask | ButtonPress;
  /*
    We catch button presses because other wise they get passed down to the
    root window, which will then cause root menus to show when you click the
    window's frame.
  */

  return XCreateWindow(otk::OBDisplay::display, _screen->getRootWindow(),
                       0, 0, 1, 1, _style->getBorderWidth(),
                       _screen->getDepth(), InputOutput, _screen->getVisual(),
                       create_mask, &attrib_create);
}

}
