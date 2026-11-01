/*
 * CC18Compat - iOS 15 compatibility shim for ControlCenter18 3.0-beta.
 *
 * Two separate iOS 16 dependencies are handled here:
 *
 * 1. Objective-C ABI.  The tweak imports objc_release_xN / objc_retain_xN /
 *    objc_claimAutoreleasedReturnValue.  The assembly file exports those as
 *    thin register-moving trampolines to the iOS 15 runtime functions.
 *
 * 2. Substrate API.  ControlCenter18 calls MSHookMessageEx for its SpringBoard
 *    hooks.  Dopamine ships a CydiaSubstrate compatibility module, but it can
 *    shadow a real implementation with a stub.  The Substrate imports in the
 *    patched binary are therefore bound *directly* to this dylib's ordinal,
 *    not through flat lookup.  MSHookMessageEx here is a self-contained
 *    Objective-C runtime swizzle; it never calls into CydiaSubstrate.
 *
 * MSHookFunction needs an inline-hook engine.  It is forwarded to an
 * already-loaded ElleKit (looked up by handle, never by RTLD_DEFAULT), and is
 * a no-op otherwise.  MSHookMemory has an mprotect fallback.
 *
 * Build: clang -target arm64-apple-ios15.0 -dynamiclib -nostdlib ...
 */

typedef void *Class;
typedef void *SEL;
typedef void *IMP;
typedef void *Method;
typedef signed char BOOL;

extern void *dlopen(const char *path, int mode);
extern void *dlsym(void *handle, const char *symbol);
extern int getpagesize(void);
extern int mprotect(void *addr, unsigned long len, int prot);
extern void *memcpy(void *dst, const void *src, unsigned long n);
extern unsigned long strlen(const char *s);

extern Method class_getInstanceMethod(Class cls, SEL sel);
extern IMP method_setImplementation(Method m, IMP imp);
extern BOOL class_addMethod(Class cls, SEL name, IMP imp, const char *types);

extern int open(const char *path, int flags, ...);
extern long write(int fd, const void *buf, unsigned long n);
extern int close(int fd);

#define RTLD_NOW 2
#define RTLD_GLOBAL 8
#define RTLD_NOLOAD 0x10

#define O_WRONLY 0x0001
#define O_CREAT 0x0200
#define O_APPEND 0x0008

#define PROT_READ 0x1
#define PROT_WRITE 0x2
#define PROT_EXEC 0x4

static void compat_log(const char *msg)
{
    int fd = open("/var/mobile/cc18compat.log", O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (fd < 0)
        return;
    write(fd, msg, strlen(msg));
    write(fd, "\n", 1);
    close(fd);
}

__attribute__((constructor)) static void cc18compat_loaded(void)
{
    compat_log("CC18Compat loaded");
}

/*
 * Only ElleKit, and only if it is already mapped.  Loading a second copy of
 * the hook engine into SpringBoard would be worse than not having one.
 */
static void *ellekit_symbol(const char *name)
{
    static const char *paths[] = {
        "/var/jb/usr/lib/libellekit.dylib",
        "/usr/lib/libellekit.dylib",
        0
    };
    for (int i = 0; paths[i]; i++) {
        void *h = dlopen(paths[i], RTLD_NOW | RTLD_GLOBAL | RTLD_NOLOAD);
        if (!h)
            continue;
        void *p = dlsym(h, name);
        if (p)
            return p;
    }
    return 0;
}

/*
 * CydiaSubstrate-compatible message hook.  This deliberately does not forward
 * to anything: it is the implementation that installs ControlCenter18's
 * overlay hooks.
 */
void MSHookMessageEx(Class cls, SEL sel, IMP imp, IMP *result)
{
    if (!cls || !sel) {
        if (result)
            *result = 0;
        return;
    }

    Method method = class_getInstanceMethod(cls, sel);
    if (method) {
        IMP old = method_setImplementation(method, imp);
        if (result)
            *result = old;
        return;
    }

    /* The selector does not exist on this iOS version.  Add a generic method
     * so the hook can still be called; the real implementation would have
     * supplied the type encoding on iOS 16. */
    class_addMethod(cls, sel, imp, "v@:");
    if (result)
        *result = 0;
    compat_log("CC18Compat: MSHookMessageEx added a missing selector");
}

void MSHookFunction(void *symbol, void *replace, void **result)
{
    typedef void (*fn_t)(void *, void *, void **);
    fn_t fn = (fn_t)ellekit_symbol("MSHookFunction");
    if (fn) {
        fn(symbol, replace, result);
        return;
    }
    if (result)
        *result = 0;
    compat_log("CC18Compat: MSHookFunction unavailable (no inline hook engine)");
}

void MSHookMemory(void *target, const void *data, unsigned long size)
{
    typedef void (*fn_t)(void *, const void *, unsigned long);
    fn_t fn = (fn_t)ellekit_symbol("MSHookMemory");
    if (fn) {
        fn(target, data, size);
        return;
    }

    long page = getpagesize();
    unsigned long start = (unsigned long)target & ~((unsigned long)page - 1);
    unsigned long end = ((unsigned long)target + size + (unsigned long)page - 1) &
                        ~((unsigned long)page - 1);
    mprotect((void *)start, end - start, PROT_READ | PROT_WRITE | PROT_EXEC);
    memcpy(target, data, size);
}

void MSHookClassPair(Class target_class, Class hook_class, Class base_class)
{
    typedef void (*fn_t)(Class, Class, Class);
    fn_t fn = (fn_t)ellekit_symbol("MSHookClassPair");
    if (fn)
        fn(target_class, hook_class, base_class);
}
