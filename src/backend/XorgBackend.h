#ifndef __XORG_BACKEND_H__
#define __XORG_BACKEND_H__

#include <v8.h>
#include <cairo/cairo.h>
#include <string>
#include <thread>
#include <functional>

#include "Backend.h"

using namespace std;

class XorgBackend : public Backend
{
  private:
    std::function<void()> abortCallback;
  public:
    XorgBackend(int width, int height);
    ~XorgBackend();
    static Backend *construct(int width, int height);

    static Nan::Persistent<v8::FunctionTemplate> constructor;
    static void Initialize(v8::Handle<v8::Object> target);
    static NAN_METHOD(New);
    static NAN_METHOD(Poll);
    static NAN_METHOD(Abort);

    virtual cairo_surface_t* createSurface();
    virtual cairo_surface_t* recreateSurface();
    virtual void destroySurface();
};

#endif
