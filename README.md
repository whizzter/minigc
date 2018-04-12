# minigc

## Summary
Minimal C++ header-only garbage collector. This collector is designed to be a per-thread C++ friendly non-moving mark-sweep collector and should mix well with generic modern C++ code.

All references to GC managed objects are kept with `minigc::root_ptr`'s that should be fairly low cost making them feasible to use both in global contexts and on the stack.

The collector make heavy use of a variation of the "Briggs&Torczon" sparse sets to track membership, see: https://research.swtch.com/sparse

## Usage

```
struct dummy : gc_object {
  int data;
}
void test(){
  minigc::gc_context gc; // creates a context that will keep track of objects
  {
    // create a root_ptr with an array 
    minigc::root_ptr<gc_array<dummy*>> arr=gc.make<gc_array<dummy*>>(1,nullptr);
    // add another object to the array
    (*arr)[0]=gc.make<dummy>();
    // collect garbage (none)
    gc.collect();
    // everything should still be live due to the arr root_ptr but it's due to go out of scope
  }
  // collecting garbage again..
  gc.collect();
  // .. and now things should be collected since the arr root went out of scope
}
  
```

see `example1.cpp` for a more complete example.

## API
`class gc_context;` manages everything, once this object dies all memory managed by it is destroyed, also used to create and collect garbage.

`gc_context::make<T>(...)` is used to create managed object instances of type T with the arguments passed on to the types constructor, when the object is allocated it will be passed in a `minigc::root_ptr`

`gc_context::collect()` can be used to enforce a garbage collection, the collector however is designed to be self-tuning so this call should hopefully not be needed for more than debugging.

`template<class T> class root_ptr;` is way objects are kept alive, can be used mostly like other smart ptrs.

`class gc_object;` base class of all user objects, programs will inherit from this class. Unless your class is specialized you should use one of the following 2 macros inside the class.

the `MINIGC_NOMARK()` macro is used to indicate that there is no member pointers inside the type.
the `MINIGC_AUTOMARK(...)` macro greatly simplifies managing pointers, just add a list of the pointer names inside the class and the collector will mark all references.

`template<class T> class gc_array;` is an gc_object that can hold a multiple of other objects inside it, initialized with a size as the first argument when made through `gc_context::make<T>(args)` and the rest of the arguments is passed onto the members being initialized.


## FAQ

Why create another GC?
I wanted a GC that melded well with regular C++ code whilst at the same time not slowing down the code too much with expensive book-keeping.

I found a bug!
Triage it and please report it, GC bugs can be notoriously hard to track down. 
