/*
 * C18Force - explicit loader/attacher for ControlCenter18 on iOS 15.
 *
 * ElleKit can fail to inject ControlCenter18.dylib for reasons that are hard
 * to see from userspace (TweakInject path mismatch, a stale filter, a signature
 * rejection).  This small dylib is injected into SpringBoard on its own and:
 *
 *   1. dlopen()s ControlCenter18.dylib with RTLD_GLOBAL;
 *   2. wraps the overlay's -viewDidLoad;
 *   3. after the original/CC18 implementation runs, checks C18Shell's rootView;
 *   4. if it is nil, calls +[C18Shell shared] and -attachToOverlay:.
 *
 * If CC18's own hook already attached the shell, rootView is non-nil and the
 * force path does nothing.  If CC18 was never injected but its dylib can be
 * loaded, this attaches the shell anyway.
 *
 * Build: clang -target arm64-apple-ios15.0 -dynamiclib -nostdlib ...
 */

typedef void *id;
typedef void *Class;
typedef void *SEL;
typedef void *IMP;
typedef void *Method;

extern id objc_getClass(const char *name);
extern SEL sel_registerName(const char *name);
extern id objc_msgSend(id self, SEL op, ...);
extern Method class_getInstanceMethod(Class cls, SEL sel);
extern Method class_getClassMethod(Class cls, SEL sel);
extern IMP method_setImplementation(Method m, IMP imp);

extern void *dlopen(const char *path, int mode);
extern char *dlerror(void);
extern int open(const char *path, int flags, ...);
extern long write(int fd, const void *buf, unsigned long n);
extern int close(int fd);
extern unsigned long strlen(const char *s);

#define RTLD_NOW 2
#define RTLD_GLOBAL 8
#define O_WRONLY 0x0001
#define O_CREAT 0x0200
#define O_APPEND 0x0008

static IMP g_orig_viewDidLoad;
static int g_did_force;

static void force_log(const char *msg)
{
    int fd = open("/var/mobile/cc18force.log", O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (fd < 0)
        return;
    write(fd, msg, strlen(msg));
    write(fd, "\n", 1);
    close(fd);
}

static void force_attach(id overlay)
{
    Class shell_class = (Class)objc_getClass("C18Shell");
    if (!shell_class) {
        force_log("C18Force: C18Shell class missing after dlopen");
        return;
    }

    SEL shared_sel = sel_registerName("shared");
    if (!class_getClassMethod(shell_class, shared_sel)) {
        force_log("C18Force: +[C18Shell shared] missing");
        return;
    }

    id shell = ((id (*)(id, SEL))objc_msgSend)((id)shell_class, shared_sel);
    if (!shell) {
        force_log("C18Force: +[C18Shell shared] returned nil");
        return;
    }

    SEL root_sel = sel_registerName("rootView");
    SEL attach_sel = sel_registerName("attachToOverlay:");
    if (!class_getInstanceMethod(shell_class, root_sel) ||
        !class_getInstanceMethod(shell_class, attach_sel)) {
        force_log("C18Force: C18Shell API missing");
        return;
    }

    id root = ((id (*)(id, SEL))objc_msgSend)(shell, root_sel);
    if (root) {
        force_log("C18Force: C18Shell already attached");
        return;
    }

    ((void (*)(id, SEL, id))objc_msgSend)(shell, attach_sel, overlay);
    force_log("C18Force: attachToOverlay called");
}

static void force_viewDidLoad(id self, SEL _cmd)
{
    if (g_orig_viewDidLoad)
        ((void (*)(id, SEL))g_orig_viewDidLoad)(self, _cmd);

    if (g_did_force)
        return;
    g_did_force = 1;
    force_attach(self);
}

__attribute__((constructor)) static void c18force_init(void)
{
    force_log("C18Force: constructor");

    void *main = dlopen(
        "/var/jb/Library/MobileSubstrate/DynamicLibraries/ControlCenter18.dylib",
        RTLD_NOW | RTLD_GLOBAL);
    if (!main) {
        char *err = dlerror();
        force_log(err ? err : "C18Force: dlopen failed");
        return;
    }
    force_log("C18Force: ControlCenter18.dylib loaded");

    Class overlay = (Class)objc_getClass("CCUIModularControlCenterOverlayViewController");
    if (!overlay) {
        force_log("C18Force: overlay class missing");
        return;
    }

    Method method = class_getInstanceMethod(overlay, sel_registerName("viewDidLoad"));
    if (!method) {
        force_log("C18Force: overlay -viewDidLoad missing");
        return;
    }

    g_orig_viewDidLoad = method_setImplementation(method, (IMP)force_viewDidLoad);
    force_log("C18Force: -viewDidLoad wrapped");
}
