
#include <iostream>       // std::cout
#include <atomic>         // std::atomic, std::memory_order_relaxed
#include <thread>         // std::thread
#include<string>
#include <stdio.h>
#include <stdlib.h>
#include <memory>
#include <chrono>
//#include <ratio>
#include <vector>
using namespace std;
struct HorovodGlobalState {

public: std::atomic_flag initialize_flag ;//= ATOMIC_FLAG_INIT;
public:	thread background_thread;

	 // Whether the background thread should shutdown.
	 bool shut_down ;//= false;
	   // Time point when coordinator last checked for stalled tensors.
	std::chrono::steady_clock::time_point last_stall_check;

	~HorovodGlobalState() {
	// Make sure that the destructor of the background thread is safe to
	// call. If a thread is still joinable (not detached or complete) its
	// destructor cannot be called.
	cout<<"ִ����������..."<<endl;

	if (background_thread.joinable()) {
	  shut_down = true;
	  cout<<"�ǿ���join��"<<endl;
	  background_thread.join();
	}
	cout<<"��������"<<endl;
	}

};

//HorovodGlobalState horovod_global;

void BackgroundThreadLoop(HorovodGlobalState &state){

	cout<<"����ִ��"<<endl;
}
int main ()
{

	std::chrono::steady_clock::time_point last_stall_check;
	auto now = std::chrono::steady_clock::now();
	last_stall_check=now;

	cout<<"ִ��main��ϣ�"<<endl;
	auto t2 = std::chrono::steady_clock::now();
	std::chrono::duration<double> time_span = std::chrono::duration_cast<std::chrono::duration<double>>(t2 - now);
	printf("%f\n",time_span);
	std::cout << "It took me " << time_span.count() << " seconds.";
	std::cout << std::endl;
    return 0;
}