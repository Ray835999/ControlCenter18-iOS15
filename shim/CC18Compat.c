/*
 * CC18Compat - iOS 15 compatibility shim for ControlCenter18 3.0-beta.
 *
 * The original tweak is built with the iOS 16 SDK and the iOS 16 Objective-C
 * ABI.  It imports objc_release_xN / objc_retain_xN / objc_claimAutoreleased-
 * ReturnValue, none of which exist in the iOS 15 libobjc, and it imports the
 * Substrate hooking API.  On a Dopamine/ElleKit device CydiaSubstrate is
 * usually absent, so MSHookMessageEx resolves to NULL and the tweak dies in
 * its constructor before it can draw anything.
 *
 * This dylib exports:
 *   - objc_release_x0 .. objc_release_x30   (assembly, CC18Compat.S)
 *   - objc_retain_x0  .. objc_retain_x30    (assembly, CC18Compat.S)
 *   - objc_claimAutoreleasedReturnValue     (assembly, CC18Compat.S)
 *   - MSHookMessageEx / MSHookFunction / MSHookMemory / MSHookClassPair
 *
 * The hook API is forwarded to ElleKit's exported MobileSubstrate API when it
 * is already loaded.  If it is not, MSHookMessageEx falls back to a
 * conservative Objective-C runtime method swizzle and MSHookMemory falls back
 * to mprotect.  MSHookFunction cannot be implemented safely without an
 * inline-hook engine, so it is a no-op in that case (ElleKit is present on
 * every supported Dopamine installation).
 *
 * Build: clang -target arm64-apple-ios15.0 -dynamiclib -nostdlib ...
 * No SDK, no libSystem, no libobjc: everything is resolved at runtime.
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
#define RTLD_DEFAULT ((void *)-2)

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

/*
 * Look for an already-loaded implementation, never load a second copy of the
 * hook engine into the process.  If RTLD_DEFAULT only returns this shim's own
 * symbol, report "not available" so the caller uses its fallback.
 */
static int same_pointer(void *a, void *b)
{
    /* arm64e function pointers may carry PAC bits; compare the canonical
     * low 36 bits so we never mistake our own symbol for ElleKit. */
    return ((unsigned long)a & 0xFFFFFFFFFUL) == ((unsigned long)b & 0xFFFFFFFFFUL);
}

static void *other_hook_engine(const char *name, void *self)
{
    void *p = dlsym(RTLD_DEFAULT, name);
    if (p && !same_pointer(p, self))
        return p;

    static const char *paths[] = {
        "/var/jb/usr/lib/libellekit.dylib",
        "/usr/lib/libellekit.dylib",
        "/var/jb/usr/lib/libsubstrate.dylib",
        "/usr/lib/libsubstrate.dylib",
        "/var/jb/Library/Frameworks/CydiaSubstrate.framework/CydiaSubstrate",
        "/Library/Frameworks/CydiaSubstrate.framework/CydiaSubstrate",
        0
    };
    for (int i = 0; paths[i]; i++) {
        void *h = dlopen(paths[i], RTLD_NOW | RTLD_GLOBAL | RTLD_NOLOAD);
        if (!h)
            continue;
        p = dlsym(h, name);
        if (p && !same_pointer(p, self))
            return p;
    }
    return 0;
}

/*
 * CydiaSubstrate-compatible API.  Keep the exact names and signatures: the
 * patched ControlCenter18.dylib binds these through the flat namespace.
 */
void MSHookMessageEx(Class cls, SEL sel, IMP imp, IMP *result)
{
    typedef void (*fn_t)(Class, SEL, IMP, IMP *);
    fn_t fn = (fn_t)other_hook_engine("MSHookMessageEx", (void *)MSHookMessageEx);
    if (fn) {
        fn(cls, sel, imp, result);
        return;
    }

    if (!cls || !sel) {
        if (result)
            *result = 0;
        return;
    }

    /* Conservative fallback.  It handles the normal "hook an existing
     * method" case, which is what ControlCenter18 uses on SpringBoard. */
    Method method = class_getInstanceMethod(cls, sel);
    if (method) {
        IMP old = method_setImplementation(method, imp);
        if (result)
            *result = old;
        return;
    }

    /* No existing method.  Add one with a generic signature.  We cannot know
     * the real type encoding here, so this is best-effort only. */
    const char *types = "v@:";
    class_addMethod(cls, sel, imp, types);
    if (result)
        *result = 0;
    compat_log("CC18Compat: MSHookMessageEx added a missing selector");
}

void MSHookFunction(void *symbol, void *replace, void **result)
{
    typedef void (*fn_t)(void *, void *, void **);
    fn_t fn = (fn_t)other_hook_engine("MSHookFunction", (void *)MSHookFunction);
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
    fn_t fn = (fn_t)other_hook_engine("MSHookMemory", (void *)MSHookMemory);
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
    fn_t fn = (fn_t)other_hook_engine("MSHookClassPair", (void *)MSHookClassPair);
    if (fn)
        fn(target_class, hook_class, base_class);
}
