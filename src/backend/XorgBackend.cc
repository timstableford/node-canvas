#include "XorgBackend.h"

#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>
#include <cairo.h>
#include <cairo-xlib.h>

#define EXIT_MESSAGE (32)

using namespace v8;

XorgBackend::XorgBackend(int width, int height)
    : Backend("xorg", width, height)
{
    this->createSurface();
}

XorgBackend::~XorgBackend()
{
    this->destroySurface();
}

void XorgBackend::destroySurface()
{
    if (cairo_surface_status(this->surface) == CAIRO_STATUS_SUCCESS) {
        abortCallback = nullptr;
        Display *display = cairo_xlib_surface_get_display(this->surface);
        cairo_surface_destroy(this->surface);
        XCloseDisplay(display);
    }
}

cairo_surface_t* XorgBackend::recreateSurface()
{
    this->destroySurface();
    return this->createSurface();
}

cairo_surface_t* XorgBackend::createSurface()
{
    Display *display = XOpenDisplay(NULL);
    if (display == nullptr) {
        Nan::ThrowError("Could not open display");
        return nullptr;
    }
    Window window = DefaultRootWindow(display);
    Drawable drawable = XCreateSimpleWindow(display, window, 0, 0, this->width, this->height, 0, 0, 0);
    XSelectInput(display, drawable, ButtonPressMask | KeyPressMask | StructureNotifyMask);
    XMapWindow(display, drawable);

    abortCallback = [drawable, display]() {
        XClientMessageEvent dummyEvent;
        memset(&dummyEvent, 0, sizeof(XClientMessageEvent));
        dummyEvent.type = ClientMessage;
        dummyEvent.window = drawable;
        dummyEvent.format = EXIT_MESSAGE;
        XSendEvent(display, drawable, 0, 0, reinterpret_cast<XEvent*>(&dummyEvent));
        XFlush(display);
    };

    cairo_surface_t *screenSurface = cairo_xlib_surface_create(
            display,
            drawable,
            DefaultVisual(display, DefaultScreen(display)),
            this->width,
            this->height);
    cairo_xlib_surface_set_size(screenSurface, this->width, this->height);

    this->surface = screenSurface;

    return screenSurface;
}

Backend *XorgBackend::construct(int width, int height){
    return new XorgBackend(width, height);
}

Nan::Persistent<FunctionTemplate> XorgBackend::constructor;

void XorgBackend::Initialize(Handle<Object> target) {
    Nan::HandleScope scope;

    Local<FunctionTemplate> ctor = Nan::New<FunctionTemplate>(XorgBackend::New);
    XorgBackend::constructor.Reset(ctor);
    ctor->InstanceTemplate()->SetInternalFieldCount(1);
    ctor->SetClassName(Nan::New<String>("XorgBackend").ToLocalChecked());
    Nan::SetPrototypeMethod(ctor, "poll", XorgBackend::Poll);
    Nan::SetPrototypeMethod(ctor, "abort", XorgBackend::Abort);

    target->Set(Nan::New<String>("XorgBackend").ToLocalChecked(), ctor->GetFunction());
}

class PollWorker : public Nan::AsyncWorker {
  public:
    PollWorker(Nan::Callback *callback, XorgBackend &backend)
        : AsyncWorker(callback), backend(backend) {}
    ~PollWorker() {}

    // Executed inside the worker-thread.
    // It is not safe to access V8, or V8 data structures
    // here, so everything we need for input and output
    // should go on `this`.
    void Execute () {
        XNextEvent(cairo_xlib_surface_get_display(backend.getSurface()), &this->event);
    }

  // Executed when the async work is complete
  // this function will be run inside the main event loop
  // so it is safe to use V8 again
  void HandleOKCallback () {
    Nan::HandleScope scope;

    v8::Local<v8::Object> obj = Nan::New<v8::Object>();

    switch (event.type) {
        case ButtonPress:
            obj->Set(Nan::New("type").ToLocalChecked(), Nan::New("button").ToLocalChecked());
            obj->Set(Nan::New("button").ToLocalChecked(), Nan::New(event.xbutton.button));
            obj->Set(Nan::New("x").ToLocalChecked(), Nan::New(event.xbutton.x));
            obj->Set(Nan::New("y").ToLocalChecked(), Nan::New(event.xbutton.y));
            break;
        case KeyPress: {
            KeySym key;
            char keybuf[8];
            XLookupString(&event.xkey, keybuf, sizeof(keybuf), &key, NULL);
            // Truncate the other characters. In some cases like backspace it's just a unicode
            // mess.
            keybuf[1] = 0;

            obj->Set(Nan::New("type").ToLocalChecked(), Nan::New("key").ToLocalChecked());
            if (keybuf[0] != 0) {
                obj->Set(Nan::New("value").ToLocalChecked(), Nan::New(keybuf).ToLocalChecked());
            }
            // Sym included because some buttons like left shift don't have a value.
            // Consider using this to translate keysyms https://github.com/substack/node-keysym
            obj->Set(Nan::New("sym").ToLocalChecked(), Nan::New(static_cast<uint32_t>(key)));
            break;
        }
        case Expose:
            obj->Set(Nan::New("type").ToLocalChecked(), Nan::New("expose").ToLocalChecked());
            break;
        case ClientMessage:
            obj->Set(Nan::New("type").ToLocalChecked(), Nan::New("message").ToLocalChecked());
            switch (event.xclient.format) {
                case EXIT_MESSAGE:
                    obj->Set(Nan::New("value").ToLocalChecked(), Nan::New("exit").ToLocalChecked());
                    break;
                default:
                    obj->Set(Nan::New("value").ToLocalChecked(), Nan::New(event.xclient.format));
                    break;
            }
            break;
        case ConfigureNotify:
            // If the width or height changes, this resizes the canvas.
            if (this->backend.resize(event.xconfigure.width, event.xconfigure.height)) {
                obj->Set(Nan::New("type").ToLocalChecked(), Nan::New("resize").ToLocalChecked());
            } else {
                obj->Set(Nan::New("type").ToLocalChecked(), Nan::New("configure_notify").ToLocalChecked());
                obj->Set(Nan::New("x").ToLocalChecked(), Nan::New(event.xconfigure.x));
                obj->Set(Nan::New("y").ToLocalChecked(), Nan::New(event.xconfigure.y));
            }

            obj->Set(Nan::New("width").ToLocalChecked(), Nan::New(event.xconfigure.width));
            obj->Set(Nan::New("height").ToLocalChecked(), Nan::New(event.xconfigure.height));

            break;
        default:
            obj->Set(Nan::New("type").ToLocalChecked(), Nan::New("unhandled").ToLocalChecked());
            obj->Set(Nan::New("value").ToLocalChecked(), Nan::New(event.type));
            break;
    }

    v8::Local<v8::Value> argv[] = {
        Nan::Null(),
        obj
    };

    callback->Call(2, argv, async_resource);
  }

  private:
    XorgBackend &backend;
    XEvent event;
};

bool XorgBackend::resize(int width, int height) {
    if (this->width != width || this->height != height) {
        this->width = width;
        this->height = height;
        cairo_xlib_surface_set_size(this->surface, this->width, this->height);
        return true;
    }
    return false;
}

NAN_METHOD(XorgBackend::Abort) {
    XorgBackend *obj = Nan::ObjectWrap::Unwrap<XorgBackend>(info.Holder());
    if (obj->abortCallback) {
        obj->abortCallback();
    }
}

NAN_METHOD(XorgBackend::Poll) {
    XorgBackend *obj = Nan::ObjectWrap::Unwrap<XorgBackend>(info.Holder());
    Nan::Callback *callback = new Nan::Callback(info[0].As<v8::Function>());
    Nan::AsyncQueueWorker(new PollWorker(callback, *obj));
}

NAN_METHOD(XorgBackend::New) {
    int width  = 0;
    int height = 0;
    if (info[0]->IsNumber()) width  = Nan::To<uint32_t>(info[0]).FromMaybe(0);
    if (info[1]->IsNumber()) height = Nan::To<uint32_t>(info[1]).FromMaybe(0);
    XorgBackend *backend = new XorgBackend(width, height);
    backend->Wrap(info.This());
    info.GetReturnValue().Set(info.This());
}
