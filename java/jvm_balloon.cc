#include <stdio.h>
#include <sys/mman.h>
#include <jni.h>
#include <api/assert.h>
#include <mmu.hh>
#include <align.hh>
#include <exceptions.hh>
#include <boost/intrusive/set.hpp>
#include <osv/trace.hh>
#include "jvm_balloon.hh"
#include "debug.hh"

TRACEPOINT(trace_jvm_balloon_fault, "from=%p, to=%p", const unsigned char *, const unsigned char *);

namespace bi = boost::intrusive;

// We will divide the balloon in units of 128Mb. That should increase the likelyhood
// of having hugepages mapped in and out of it.
//
// Using constant sized balloons should help with the process of giving memory
// back to the JVM, since we don't need to search the list of balloons until
// we find a balloon of the desired size: any will do.
constexpr size_t balloon_size = (128ULL << 20);

class fault_range {
public:
    unsigned char *start;
    unsigned char *end;
};

static constexpr unsigned flags = mmu::mmap_fixed | mmu::mmap_uninitialized;
static constexpr unsigned perms = mmu::perm_read | mmu::perm_write;

class balloon {
public:
    explicit balloon(unsigned char *jvm_addr, jobject jref, int alignment, size_t size);

    bi::set_member_hook<> member_hook;

    size_t move_balloon(unsigned char *dest, unsigned char *src);
    void release(JNIEnv *env);

    unsigned char *start;
    unsigned char *end;
private:
    unsigned char *_jvm_addr;
    unsigned char *_addr;
    unsigned char *_jvm_end_addr;
    void empty_area(void);

    jobject _jref;
    unsigned int _alignment;
    size_t _hole_size;
    size_t _balloon_size = balloon_size;
};

struct addr_cmp {
    // Two balloons never overlap.
    bool operator()(const balloon& bln1, const balloon& bln2) const {
        return bln1.start < bln2.start;
    }

    bool operator()(const fault_range &fr, const balloon& bln) const {
        return fr.end < bln.start;
    }

    bool operator()(const balloon& bln, const fault_range &fr) const {
        return bln.end < fr.start;
    }
};

mutex balloons_lock;
bi::set<balloon,
        bi::compare<addr_cmp>,
        bi::member_hook<balloon,
                       bi::set_member_hook<>,
                       &balloon::member_hook>
       > balloons;

void balloon::empty_area()
{
    _jvm_end_addr = _jvm_addr + _balloon_size;
    _addr = align_up(_jvm_addr, _alignment);

    end = align_down(_jvm_end_addr, _alignment);
    start = _addr;

    _hole_size = end - _addr;

    // For sure a jvm mapping
    mmu::mark_jvm_mapping(_addr);
    mmu::map_jvm(_addr, _hole_size);
    balloons.insert(*this);
}

balloon::balloon(unsigned char *jvm_addr, jobject jref, int alignment = mmu::huge_page_size, size_t size = balloon_size)
    : _jvm_addr(jvm_addr), _jref(jref), _alignment(alignment), _balloon_size(size)
{
    WITH_LOCK(balloons_lock) {
        empty_area();
    }
}

// Giving memory back to the JVM only means deleting the reference.  Without
// any pending references, the Garbage collector will be responsible for
// disposing the object when it really needs to. Note that since we are
// operating in virtual addresses, all we have to do is to mmap the memory
// back. That does not guarantee that it will be backed by pages until the JVM
// decides to reuse it for something else.
void balloon::release(JNIEnv *env)
{
    assert(mutex_owned(&balloons_lock));

    mmu::map_anon(_addr, _hole_size, flags, perms);
    env->DeleteGlobalRef(_jref);
    balloons.erase(*this);
}

size_t balloon::move_balloon(unsigned char *dest, unsigned char *src)
{
    size_t skipped = _addr - _jvm_addr;
    assert(mutex_owned(&balloons_lock));

    _jvm_addr = dest - skipped;

    // We need to erase and reinsert otherwise the element may end up living in
    // the wrong position of the tree.
    balloons.erase(*this);

    // We re-map the area first. Since we won't fault in any pages there unless
    // touched, we need not to worry about memory shortages. It is simpler to
    // do this rather than the other way around because then in case part of
    // the new balloon falls within this area, the vma->split() code will take
    // care of arrange things for us.
    mmu::map_anon(_addr, _hole_size, flags, perms);
    empty_area();
    return end - dest;
}

// We can either be called from a java thread, or from the shrinking code in OSv.
// In the first case we can just grab a pointer to env, but in the later we need
// to attach our C++ thread to the JVM. Only in that case we will need to detach
// later, so keep track through passing the status over as a handler.
int jvm_balloon_shrinker::_attach(JNIEnv **env)
{
    int status = _vm->GetEnv((void **)env, JNI_VERSION_1_6);
    if (status == JNI_EDETACHED) {
        if (_vm->AttachCurrentThread((void **) env, NULL) != 0) {
            assert(0);
        }
    } else {
        assert(status == JNI_OK);
    }
    return status;
}

void jvm_balloon_shrinker::_detach(int status)
{
    if (status != JNI_OK) {
        _vm->DetachCurrentThread();
    }
}

size_t jvm_balloon_shrinker::request_memory(size_t size)
{
    JNIEnv *env = NULL;
    size_t ret = 0;
    int status = _attach(&env);

    do {
        jbyteArray array = env->NewByteArray(balloon_size);
        jthrowable exc = env->ExceptionOccurred();
        if (exc) {
            env->ExceptionClear();
            deactivate_shrinker();
            break;
        }

        jboolean iscopy=0;
        auto p = env->GetPrimitiveArrayCritical(array, &iscopy);

        // OpenJDK7 will always return false when we are using
        // GetPrimitiveArrayCritical, and true when we are using
        // GetPrimitiveArray.  Still, better test it since this is not mandated
        // by the interface. If we receive a copy of the array instead of the
        // actual array, this address is pointless.
        if (!iscopy) {
            // When calling JNI, all allocated objects will have local
            // references that prevent them from being garbage collected. But
            // those references will only prevent the object from being Garbage
            // Collected while we are executing JNI code. We need to acquire a
            // global reference.  Later on, we will invalidate it when we no
            // longer want this balloon to stay around.
            jobject jref = env->NewGlobalRef(array);
            new balloon(static_cast<unsigned char *>(p), jref);
            ret += balloon_size;
        }
        env->ReleasePrimitiveArrayCritical(array, p, 0);
        // Avoid entering any endless loops. Fail imediately
        if (!iscopy)
            break;
    } while (ret < size);

    _detach(status);
    return ret;
}

size_t jvm_balloon_shrinker::release_memory(size_t size)
{
    JNIEnv *env = NULL;
    int status = _attach(&env);

    size_t ret = 0;
    do {
        WITH_LOCK(balloons_lock) {
            if (balloons.empty()) {
                break; // No more balloons to release
            }

            ret += balloon_size;
            auto b = &*balloons.begin();
            b->release(env);
            delete b;
            // It might be that this shrinker was disabled due to excessive memory
            // pressure, so we must take care to activate it. This should be a nop
            // if the shrinker is already active, so do it always.
            activate_shrinker();
        }
    } while (ret < size);

    _detach(status);
    return ret;
}

// We have created a byte array and evacuated its addresses. Java is not ever
// expected to touch the variable itself because no code does it. But when GC
// is called, it will move the array to a different location. Because the array
// is paged out, this will generate a fault. We can trap that fault and then
// manually resolve it.
//
// However, we need to be careful about one thing: The JVM will not move parts
// of the heap in an object-by-object basis, but rather copy large chunks at
// once. So there is no guarantee whatsoever about the kind of addresses we
// will receive here. Only that there is a balloon in the middle. So the best
// thing to do is to emulate the memcpy in its entirety, not only the balloon
// part.  That means copying the part that comes before the ballon, playing
// with the maps for the balloon itself, and then finish copying the part that
// comes after the balloon.
void jvm_balloon_fault(exception_frame *ef)
{

    WITH_LOCK(balloons_lock) {
        assert(!balloons.empty());

        memcpy_decoder *decode = memcpy_find_decoder(ef);
        assert(decode);

        unsigned char *dest = decode->dest(ef);
        unsigned char *src = decode->src(ef);
        size_t size = decode->size(ef);

        fault_range frange = { src, src + size };
        auto it = balloons.find(frange, addr_cmp());
        if (it == balloons.end()) {
            return;
        }
        balloon *b = &*it;
        trace_jvm_balloon_fault(src, dest);
        decode->memcpy_fixup(ef, b->move_balloon(dest, src));
    }
}

jvm_balloon_shrinker::jvm_balloon_shrinker(JavaVM_ *vm)
    : shrinker("jvm_shrinker")
    , _vm(vm)
{
}
