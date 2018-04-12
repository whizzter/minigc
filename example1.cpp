#include <stdio.h>
#include "minigc.hpp"

#include <random>
#include <iostream>

using namespace minigc;

struct test_obj : public gc_object {
	char bla[3000];
	test_obj(int sz) {
		printf("test allocated with size %d\n",sz);
	}
};

int main(int argc,char **argv) {
	try {
		gc_context gc;


		root_ptr<gc_array<int>> pa1=gc.make<gc_array<int>>(80);
		root_ptr<gc_array<test_obj*>> pa2=gc.make<gc_array<test_obj*>>(2,nullptr);
		for (int i=0;i<2;i++)
		{
			(*pa2)[i]=gc.make<test_obj>(200);
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
