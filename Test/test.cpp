
#include <iostream>       // std::cout
#include <atomic>         // std::atomic, std::memory_order_relaxed
#include <thread>         // std::thread
#include<string>
#include <stdio.h>
#include <stdlib.h>
std::atomic<int> foo(0); // ȫ�ֵ�ԭ�Ӷ��� foo
 
void set_foo(int x)
{
	std::cout<<"set foo"<<std::endl;
    foo.store(x, std::memory_order_relaxed); // ����(store) ԭ�Ӷ��� foo ��ֵ
}
 
void print_foo()
{
    int x;
    do {
        x = foo.load(std::memory_order_relaxed); // ��ȡ(load) ԭ�Ӷ��� foo ��ֵ
    } while (x == 0);
    std::cout << "foo: " << x << '\n';
}
 
int main ()
{
    //std::thread first(print_foo); // �߳� first ��ӡ foo ��ֵ
   // std::thread second(set_foo, 10); // �߳� second ���� foo ��ֵ
    //first.join();
    //second.join();
	 int i=10;
  char buffer [33];
  _itoa (i,buffer,10);
  std::string s="children"+std::string(buffer);
  std::cout<<s<<std::endl;

    return 0;
}