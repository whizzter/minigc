# minigc
Minimal C++ header-only garbage collector. This collector is designed to be a per-thread C++ friendly non-moving mark-sweep collector and should mix well with generic modern C++ code.

All references to GC managed objects are kept with `minigc::root_ptr`'s that should be fairly low cost making them feasible to use both in global contexts and on the stack.

The collector make heavy use of a variation of the "Briggs&Torczon" sparse sets to track membership, see: https://research.swtch.com/sparse

Simple usage:
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
