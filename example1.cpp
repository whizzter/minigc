#include <stdio.h>
#include "minigc.hpp"

#include <string>
#include <string.h>
#include <iostream>

using namespace minigc;

struct test_obj : public gc_object {
	gc_array<char> *data;

	MINIGC_AUTOMARK(data);         // use this macro if there is outgoing GC pointers in the class
	//MINIGC_NOMARK()              // use this macro if there is no outgoing GC pointers from this class

	test_obj(int sz) {
		data=nullptr;
		printf("test started with size %d\n",sz);
	}
	test_obj(gc_context & ctx,const char *indata) {
		data=ctx.make<gc_array<char>>(strlen(indata)+1);
		strcpy(data->data(),indata);
	}
};

int main(int argc,char **argv) {
	try {
		gc_context gc;


		root_ptr<gc_array<int>> pa1=gc.make<gc_array<int>>(80);
		root_ptr<gc_array<test_obj*>> pa2=gc.make<gc_array<test_obj*>>(2,nullptr);
		for (int i=0;i<2;i++)
		{
			std::string base("Hello");
			base=base+std::to_string(i);
			(*pa2)[i]=gc.make<test_obj>(gc,base.c_str());
		}
		gc.collect();
		// If the MINIGC_AUTOMARK macro would've been missing then the following loop will fail or produce undefined behaviour
		for (int i=0;i<2;i++) {
			char* data=(*pa2)[i]->data->data();
			std::cout<<"String "<<i<<":"<<data<<"\n";
		}

		while(true) {
			root_ptr<test_obj> p2=gc.make<test_obj>(200);
			root_ptr<test_obj> p3=gc.make<test_obj>(200);
			root_ptr<test_obj> p4=gc.make<test_obj>(200);
			root_ptr<test_obj> p5=gc.make<test_obj>(200);
			root_ptr<test_obj> p6=gc.make<test_obj>(200);
			root_ptr<test_obj> p7=gc.make<test_obj>(200);
			root_ptr<test_obj> p8=gc.make<test_obj>(200);
			root_ptr<test_obj> p9=gc.make<test_obj>(200);
			root_ptr<test_obj> p10=gc.make<test_obj>(200);
			root_ptr<test_obj> p11=gc.make<test_obj>(200);
			root_ptr<test_obj> p12=gc.make<test_obj>(200);
		}
	} catch (const std::exception & err) {
		auto logm="[Catching exception]";
		fwrite(logm,strlen(logm),1,stderr);
		logm=err.what();
		fwrite(logm,strlen(logm),1,stderr);
	}

	return 0;
}
