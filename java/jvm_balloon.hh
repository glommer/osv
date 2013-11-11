/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef JVM_BALLOON_HH_
#define JVM_BALLOON_HH_

#include "mempool.hh"
#include "exceptions.hh"

struct JavaVM_;
class jvm_balloon_shrinker : public memory::shrinker {
public:
    explicit jvm_balloon_shrinker(JavaVM_ *vm);
    virtual size_t request_memory(size_t s);
    virtual size_t release_memory(size_t s);
private:
    JavaVM_ *_vm;
    size_t jvm_balloon_shrink(JavaVM_ *vm, size_t size);
    size_t jvm_balloon_expand(JavaVM_ *vm, size_t size);
};

void jvm_balloon_fault(exception_frame *ef);
#endif
