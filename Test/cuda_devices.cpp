#include "cuda_runtime.h"
#include "device_launch_parameters.h"
#include <iostream>
#include <cstring>
#include<cstdio>
using namespace std;

int main(){
	//������Ҫ���豸������
	cudaDeviceProp devicePropDefined;
	memset(&devicePropDefined,0,sizeof(cudaDeviceProp));
	devicePropDefined.major=5;  //�豸�ļ��㹦�ܼ���Compute Capability���İ汾��
	devicePropDefined.minor=2;

	int devicedChoosed; //ѡ�е��豸��ID
	cudaError_t cudaError;
	cudaGetDevice(&devicedChoosed); //��ȡ��ǰ�豸��ID

	cout << "��ǰʹ���豸�ı�ţ� " << devicedChoosed << endl;
 
	cudaChooseDevice(&devicedChoosed, &devicePropDefined);  //���ҷ���Ҫ����豸ID
	cout << "����ָ������Ҫ����豸�ı�ţ� " << devicedChoosed << endl;
	cudaError = cudaSetDevice(devicedChoosed); //����ѡ�е��豸Ϊ���ĵ������豸
 
	if (cudaError == cudaSuccess)
		cout << "�豸ѡȡ�ɹ���" << endl;
	else
		cout << "�豸ѡȡʧ�ܣ�" << endl;
	getchar();
	return 0;

}