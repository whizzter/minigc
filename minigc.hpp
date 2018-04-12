#include <stdint.h>
#include <new>
#include <cstddef>

//#define MINIGCHPP_VERBOSE

#ifdef MINIGCHPP_VERBOSE
#include <string.h>
#include <stdio.h> 
#endif

namespace minigc {
	class gc_context;

	// This collector is designed to be a per-thread C++ friendly non-moving mark-sweep collector and
	// should mix well with generic modern C++ code.

	// All references to GC managed objects are kept with root_ptr's that should be fairly low cost making
	// them feasible to use both in global contexts and on the stack.

	// The collector make heavy use of "Briggs&Torczon" sparse sets to track membership,
	//  see: https://research.swtch.com/sparse
	//
	// First use is to keep track of object state:
	//
	// the overhead per object is 3 macine words, one word inside the object to keep track of the objects state
	// 1 bit for the current set and the rest of the bits as an index in the dense set of active objects (sparse part)
	// the 2 other words are 2 alternating dense lists for keeping track of garbage and live objects.
	// additionally the dense lists will also double as a work-list for the mark phase of the GC as well
	// as a free-list for the sweep phase.
	//
	// The other use is to register roots:
	// the sparse part is the root_ptr's own index reference and the dense map is inside the GC


	// gc_object is the parent of all collectable objects, regular classes can just inherit them
	// and create objects via gc_context.make<T>(...)
	class gc_object {
		// the context can touch the internals
		friend class gc_context;

		// gc_info has the lowest bit to indicate liveness and the rest of the bits as an dense index
		uintptr_t gc_info;
	protected:
		virtual ~gc_object()=default;

		// this will be auto-implemented unless the object is created via minigc_construct
		virtual size_t minigc_gc_sizeof()=0;

		// the default mark method won't mark any members
		virtual void minigc_mark(gc_context *gc){}
	public:
		gc_object()=default;
		gc_object(const gc_object&)=delete;
		gc_object(gc_object &&)=delete;
		gc_object& operator=(const gc_object &)=delete;
		gc_object& operator=(gc_object &&)=delete;
	};

	template<class T>
	class gc_array : public gc_object {
		int m_count;
		T   m_data[];

		// use SFINAE enabling of a pointer marking method to mark outgoing pointers from the array.
		template<class T>
		auto minigc_mark_impl(gc_context *gc,T*d)->decltype( (gc->mark(m_data[0])),void()) {
			printf("Marking %d members\n",m_count);
			for (int i=0;i<m_count;i++) {
				gc->mark(d[i]);
			}
		}

		// SFINAE default will not mark anything
		void minigc_mark_impl(gc_context *gc,...) {
			// do nothing for plain types (unless we can add detection for subitem marking?)
		}

		// the virtual mark method is dispatched to specializations via an SFINAE impl
		virtual void minigc_mark(gc_context * gc) {
			minigc_mark_impl(gc,m_data);
		}

		// arrays specialize minigc_gc_sizeof() since they can vary in size
		virtual size_t minigc_gc_sizeof() {
			return offsetof(gc_array,m_data[m_count]);
		}

		// minimal constructor to initialize the count
		gc_array(int sz) : m_count(sz) {}

		// we're friends of gc_context since this class shall only be initialized via the GC
		friend class gc_context;

		// construction method that actually creates array objects.
		template<class... ARGS>
		static gc_array* minigc_construct(size_t sz,ARGS&&... args) {
			char* blob=new (std::nothrow) char[offsetof(gc_array,m_data[sz])];
			if (!blob)
				return 0;
			gc_array *p=::new(blob) gc_array(sz);
			for (int i=0;i<sz;i++) {
				// TODO: make this exception safe?
				::new(p->m_data+i) T(std::forward<ARGS>(args)...);
			}
			return p;
		}
	public:
		virtual ~gc_array() {
			// destroy members if they are complex!
			for (int i=m_count-1;i>=0;i--) {
				m_data[i].~T();
			}
		}

		// some operators to enable array-like behaviour
		inline int size() { return m_count; }
		inline int count() { return m_count; }
		inline T& operator[](size_t idx) {
			return m_data[idx];
		}
	};

	// root_ptr is the user-facing ptr container that keeps objects inside the heap alive.
	template<class T>
	class root_ptr {
		friend class gc_context;
		// index into the dense root list
		int idx;
		// the active gc_context
		gc_context * gc;
		// and the actual object pointer
		T *ptr;
	public:
		// default constructor that initializes pointer from a context
		inline root_ptr(gc_context* gc);
		// the destructor will deregister this pointer from the active context
		inline ~root_ptr();

		// it's also possible to initialize from another root_ptr
		inline root_ptr(const root_ptr &other) : root_ptr(other.gc) {
			ptr=other.ptr;
		}

		// pointers can also be changed (as long as this instance is registered the pointer changing won't matter)
		inline root_ptr& operator=(const root_ptr &other) {
			ptr=other.ptr;
		}


		inline T* operator->() {
			return ptr;
		}
		inline T& operator*() {
			return *ptr;
		}
		inline T* get() {
			return ptr;
		}

//		inline decltype( (*ptr)[0] ) operator[](int idx) {
//			return (*ptr)[idx];
//		}
	};

	class gc_context {
		template<class T>
		friend class root_ptr;

		// safety to avoid wedging the collection state.
		bool collecting=false;

		// these variables keeps tracks of the denselist for the roots
		int nextRoot=0;
		int rootMax=0;
		root_ptr<gc_object> **denseRoots=nullptr;

		// variables used to indicate when it's time to run the GC (outside of out-of-memory situations)
		size_t allocBytes=0;
		size_t gcMarkBytes=0;

		// and the dense lists for the allocations
		int setSizes=0;
		int curSet=0;
		int setNexts[2]={0,0};
		gc_object **sets[2]={nullptr,nullptr};

		// when there is insufficient space in the dense sets for the number of objects on the heap we
		// enlarge the ref sets
		void enlargeSets() {
			int newSize=setSizes==0?1024:setSizes+(setSizes>>1);
			for (int i=0;i<2;i++) {
				gc_object ** newSet=nullptr;
				
				// we have a small goto loop here to try running a sweep when enlargement is done if insufficient memory is available.
				bool swept=false;
				again:
				// try making a new set
				newSet=new (std::nothrow) gc_object*[newSize];
				if (!newSet) {
					// not enough memory, if we haven't already then try sweeping.
					if (swept) {
						throw std::bad_alloc();
					}
					sweep();
					swept=true;
					goto again;
				}
				for (int j=0;j<setSizes;j++)
					newSet[j]=sets[i][j];
				if (sets[i])
					delete[] sets[i];
				sets[i]=newSet;
			}
			setSizes=newSize;
		}

		// this method is invoked when the dense-list of the root-set is filled up.
		void compactRootDense() {
			// first do compacting
			int end=nextRoot-1;
			for (int oi=0;oi<end;) {
				if (denseRoots[oi]) {
					oi++;
				} else if (!denseRoots[end]) {
					end--;
				} else {
					denseRoots[oi]=denseRoots[end];
					denseRoots[oi]->idx=oi;
					end--;
					oi++;
				}
			}
			nextRoot=end+1;
			// if we've still used more than half the rootMax list (deep recursion or many roots) then increase the dense root size by 25% for the future.
			if (nextRoot>>1 >= rootMax) {
				int newSize=rootMax==0?256:(rootMax+(rootMax>>2));
				bool swept=false;
				again:
				root_ptr<gc_object> **newRoots=new (std::nothrow) root_ptr<gc_object>*[newSize];
				if (!newRoots) {
					if (swept)
						throw std::bad_alloc();
					sweep();
					swept=true;
					goto again;
				}
				if (denseRoots) {
					for (int i=0;i<nextRoot;i++)
						newRoots[i]=denseRoots[i];
					delete[] denseRoots;
				}
				denseRoots=newRoots;
			}
		}
		
		// gc_sized is an class created to correctly implement an sizeof operation
		template<class T>
		class gc_sized : public T {
		protected:
			virtual size_t minigc_gc_sizeof() { return sizeof(gc_sized); }
		public:
			using T::T;
		};

		// internal construc implementations enabled via SFINAE that either call explicit construct methods or 
		// fall back to a new.
		template<class T,class ... ARGS>
		auto construct(T* dummy,ARGS&& ...args) -> decltype( T::minigc_construct( std::forward<ARGS>(args)... ),(T*)nullptr  ) {
			return T::minigc_construct( std::forward<ARGS>(args)... );
		}

		template<class T,class ... ARGS>
		T* construct(void *,ARGS&& ... args) {
			return new (std::nothrow) gc_sized<T>( std::forward<ARGS>(args)... );
		}

	public:

		// the make method is the function that is responsible to construct user objects
		template<class T,class ... ARGS>
		root_ptr<T> make(ARGS&& ... args) {
			// before allocating we see if we've approaching the time to mark/sweep
			// TODO: add sizeof specializations
			if (allocBytes+sizeof(T)>gcMarkBytes) {
				sweep();
			}
			// also ensure that we have space in live-sets data
			if (setNexts[curSet]+1>=setSizes) {
				enlargeSets();
			}
			// create a temporary root_ptr
			root_ptr<T> out(this);
			// sweep flag before trying an allocation
			bool swept=false;

			again:
			out.ptr=construct<T,ARGS...>((T*)nullptr,std::forward<ARGS>(args)...);
			if (!out.ptr) {
				// if the allocation failed we will run the mark/sweep and then try again ONCE
				if (swept) {
					// even after a sweep we were unable to allocate, bail!
					throw std::bad_alloc();
				}
				sweep();
				swept=true;
				goto again;
			}
			// register allocation size
			allocBytes+=out.ptr->minigc_gc_sizeof();
			// register to liveset
			{
				int idx=setNexts[curSet]++;
				out.ptr->gc_info=curSet|(idx<<1);
				sets[curSet][idx]=out.ptr;
			}
			return out;
		}

		// the sweep method is used to collect garbage, it can be called specifically but is also called periodically or in out of memory situations.
		void sweep() {
			if (collecting)
				return; // not re-entrant
			collecting=true;
#ifdef MINIGCHPP_VERBOSE
			auto logm="[Running sweep function]\n";
			fwrite(logm,strlen(logm),1,stderr);
			fprintf(stderr,"Live objects pre-sweep:%d taking %d bytes\n",setNexts[curSet],allocBytes);
#endif
			// store the old allocation size
			int oldAllocBytes=allocBytes;
			allocBytes=0;

			int oldSet=curSet;
			// flip the cur-set bit so everything live becomes trash until marked.
			curSet^=1;

			// mark roots first
			for (int i=0;i<nextRoot;i++) {
				if (denseRoots[i]) {
					gc_object *ptr=denseRoots[i]->ptr;
					if (ptr) {
						mark(ptr);
					}
				}
			}

			// now mark the liveset as we work through it and make sure everything that is live gets marked
			for (int i=0;i<setNexts[curSet];i++) {
				sets[curSet][i]->minigc_mark(this);
			}

			// finally destroy everything left in the old set
			for (int i=0;i<setNexts[oldSet];i++) {
				if (sets[oldSet][i]) {
					delete sets[oldSet][i];
				}
			}
			// and sero the setsize
			setNexts[oldSet]=0;

#ifdef MINIGCHPP_VERBOSE
			fprintf(stderr,"Live objects post-sweep:%d taking %d bytes\n",setNexts[curSet],allocBytes);
#endif

			// post-sweep we decide on the next sweep boundary
			size_t newMax=allocBytes<<1;
			// first time we put in a max (TODO: make this configurable)
			if (newMax==0)
				newMax=64*1024;
			// TODO: have an upper bound possible?
			if (gcMarkBytes>newMax) {
				// go halfway down to the new mark so we don't start doing frequent collections too early.
				gcMarkBytes=(gcMarkBytes+newMax)>>1;
			} else {
				// new higher boundary, adopt it (and add a slightly higher max).
				gcMarkBytes=newMax;
			}
			collecting=false;
		}

		// this method is called by objects when they are being marked to indicate that there is sub-objects.
		virtual void mark(gc_object * ptr) {
			if (!collecting)
				return;
			if (ptr->gc_info&1 == curSet)
				return; // already marked
			// we're collecting and found an object in the old set, time to move it!
			// First remove it from the old-set
			sets[curSet^1][ptr->gc_info>>1]=nullptr;
			// next register to the live set
			{
				int idx=setNexts[curSet]++;
				ptr->gc_info=curSet|(idx<<1);
				sets[curSet][idx]=ptr;
			}
			// register the size change
			allocBytes+=ptr->minigc_gc_sizeof();
			// and we're done!
		}
		~gc_context() throw(std::exception) {
			// check that all roots are killed before destruction.
			for (int i=0;i<nextRoot;i++) {
				if (denseRoots[i]!=nullptr) {
					throw std::exception("Live roots detected at GC destruction");
				}
			}
			// run a sweep (this should destroy everything since all roots are gone)
			sweep();
			for (int i=0;i<2;i++) {
				if (sets[i])
					delete[] sets[i];
			}
			// remove denseroots
			if (denseRoots)
				delete[] denseRoots;
		}
	};

	// the constructor/destructor responsible for managing
	template<class T>
	inline root_ptr<T>::root_ptr(gc_context* in_gc) : gc(in_gc) {
		if (in_gc->nextRoot>=in_gc->rootMax) {
			in_gc->compactRootDense();
		}
		idx=in_gc->nextRoot++;
		in_gc->denseRoots[idx]=reinterpret_cast<root_ptr<gc_object>*>(this);
		ptr=nullptr;
	}

	template<class T>
	inline root_ptr<T>::~root_ptr() {
		gc->denseRoots[idx]=nullptr;
	}


};

