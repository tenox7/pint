//
// The C++ of MSVC 2.0 allocates an array with operator new and frees it with
// operator delete (it has no operator new[]/delete[]); the C++ of today calls the
// array forms, which are those of the module (as rpcrt4's memory.cxx defines them).
//
typedef unsigned int size_t;

void *operator new(size_t);
void operator delete(void *);

void *operator new[](size_t Size)
{
    return operator new(Size);
}

void operator delete[](void *Pointer)
{
    operator delete(Pointer);
}
