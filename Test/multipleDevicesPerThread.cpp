#include<stdio.h>
#include "cuda_runtime.h"
#include "nccl.h"
#include "mpi.h"
#include <unistd.h>
#include <stdlib.h>
#include <stdint.h>
#include <malloc.h>

#define MPICHECK(cmd) do{ \
	int e=cmd;				\
	if(e!= MPI_SUCCESS) {    \
	printf("Failed: MPI error %s:%d '%d'\n",        \
		__FILE__,__LINE__, e);                \
	exit(1);												\
	}                                                 \
}while(0)
#define CUDACHECK(cmd) do {                         \
  cudaError_t e = cmd;                              \
  if( e != cudaSuccess ) {                          \
	printf("Failed: Cuda error %s:%d '%s'\n",             \
		__FILE__,__LINE__,cudaGetErrorString(e));   \
	exit(1);                             \
  }                                                 \
} while(0)

#define NCCLCHECK(cmd) do {                         \
  ncclResult_t r = cmd;                             \
  if (r!= ncclSuccess) {                            \
	printf("Failed, NCCL error %s:%d '%s'\n",             \
		__FILE__,__LINE__,ncclGetErrorString(r));   \
	exit(1);                             \
  }                                                 \
} while(0)

static uint64_t getHostHash(const char * string){
	// Based on DJB2, result = result * 33 + char
  uint64_t result = 5381;
  for (int c = 0; string[c] != '\0'; c++){
	result = ((result << 5) + result) + string[c];
  }
  return result;
}

static void getHostName(char* hostname, int maxlen) {
  gethostname(hostname, maxlen);
  for (int i=0; i< maxlen; i++) {
	if (hostname[i] == '.') {
		hostname[i] = '\0';
		return;
	}
  }
}

//测试单个线程多个跨机器节点之间Allreduce
void multipleDevicesPerThreadAllReduce(int argc,char*argv[]){
	int size = 4;  //发送结束buffer的大小

  int myRank, nRanks, localRank = 0;

  //initializing MPI
  MPICHECK(MPI_Init(&argc, &argv));
  MPICHECK(MPI_Comm_rank(MPI_COMM_WORLD, &myRank));
  MPICHECK(MPI_Comm_size(MPI_COMM_WORLD, &nRanks));

  //calculating localRank which is used in selecting a GPU
  uint64_t hostHashs[nRanks];
  char hostname[1024];
  getHostName(hostname, 1024);
  hostHashs[myRank] = getHostHash(hostname);
  MPICHECK(MPI_Allgather(MPI_IN_PLACE, 0, MPI_DATATYPE_NULL, hostHashs, sizeof(uint64_t), MPI_BYTE, MPI_COMM_WORLD));
  for (int p=0; p<nRanks; p++) {
	 if (p == myRank) break;
	 if (hostHashs[p] == hostHashs[myRank]) localRank++;
  }

  printf("my localRank :%d,myRank is:%d\n",localRank,myRank);

  //each process is using two GPUs
  int nDev = 2;

  float** sendbuff = (float**)malloc(nDev * sizeof(float*));
  float** recvbuff = (float**)malloc(nDev * sizeof(float*));
  cudaStream_t* s = (cudaStream_t*)malloc(sizeof(cudaStream_t)*nDev);

  //picking GPUs based on localRank
  for (int i = 0; i < nDev; ++i) {
	CUDACHECK(cudaSetDevice(localRank*nDev + i));
	CUDACHECK(cudaMalloc(sendbuff + i, size * sizeof(float)));
	CUDACHECK(cudaMalloc(recvbuff + i, size * sizeof(float)));
	CUDACHECK(cudaMemset(sendbuff[i], 1, size * sizeof(float)));
	CUDACHECK(cudaMemset(recvbuff[i], 0, size * sizeof(float)));


	float *h_arr;
	h_arr = (float *)malloc(size*sizeof(float));
	for (int i=0; i<size; ++i)
		h_arr[i] = i; // Or other values
	CUDACHECK(cudaMemcpy(sendbuff[i], h_arr, size*sizeof(float), cudaMemcpyHostToDevice));

	CUDACHECK(cudaStreamCreate(s+i));
  
  }
  for (int i = 0; i < nDev; ++i) {
	   CUDACHECK(cudaSetDevice(localRank*nDev + i));
	   float* recvCPU=(float*)malloc(size*sizeof(float));  //将数据从cuda 拷贝到cpu
	   CUDACHECK(cudaMemcpy(recvCPU, recvbuff[i], sizeof(float) * size, cudaMemcpyDeviceToHost));
	  printf("Begin Reduce Dev is %d of process myRank is %d, RecvBUf is %f,%f,%f,%f\n",i,myRank
	  ,recvCPU[0],recvCPU[1],recvCPU[2],recvCPU[3]);

  }
   ncclUniqueId id;
  ncclComm_t comms[nDev];

  //generating NCCL unique ID at one process and broadcasting it to all
  if (myRank == 0) ncclGetUniqueId(&id);
  MPICHECK(MPI_Bcast((void *)&id, sizeof(id), MPI_BYTE, 0, MPI_COMM_WORLD));

  //initializing NCCL, group API is required around ncclCommInitRank as it is
  //called across multiple GPUs in each thread/process
  NCCLCHECK(ncclGroupStart());
  for (int i=0; i<nDev-1; i++) {
	 CUDACHECK(cudaSetDevice(localRank*nDev + i));
	 NCCLCHECK(ncclCommInitRank(comms+i, nRanks*(nDev-1), id, myRank*(nDev-1) + i));
  }
   NCCLCHECK(ncclGroupEnd());

  //calling NCCL communication API. Group API is required when using
  //multiple devices per thread/process
  NCCLCHECK(ncclGroupStart());
  for (int i=0; i<nDev-1; i++)
	 NCCLCHECK(ncclAllReduce((const void*)sendbuff[i], (void*)recvbuff[i], size, ncclFloat, ncclSum,
		   comms[i], s[i]));
  NCCLCHECK(ncclGroupEnd());

  for (int i = 0; i < nDev; ++i) {
	   CUDACHECK(cudaSetDevice(localRank*nDev + i));
	   float* recvCPU=(float*)malloc(size*sizeof(float));  //将数据从cuda 拷贝到cpu
	   CUDACHECK(cudaMemcpy(recvCPU, recvbuff[i], sizeof(float) * size, cudaMemcpyDeviceToHost));
	  printf("End Reduce Dev is %d of process myRank is %d, RecvBUf is %f,%f,%f,%f\n",i,myRank
	  ,recvCPU[0],recvCPU[1],recvCPU[2],recvCPU[3]);

  }

  //synchronizing on CUDA stream to complete NCCL communication
  for (int i=0; i<nDev; i++)
	  CUDACHECK(cudaStreamSynchronize(s[i]));

  //freeing device memory
  for (int i=0; i<nDev; i++) {
	 CUDACHECK(cudaFree(sendbuff[i]));
	 CUDACHECK(cudaFree(recvbuff[i]));
  }

  //finalizing NCCL
  for (int i=0; i<nDev-1; i++) {
	 ncclCommDestroy(comms[i]);
  }
   MPICHECK(MPI_Finalize());

   printf("[MPI Rank %d] Success \n", myRank);

}

//测试单个线程多个跨机器节点之间reduce
void multipleDevicesPerThreadReduce(int argc,char*argv[]){
	int size = 4;  //发送结束buffer的大小

  int myRank, nRanks, localRank = 0;

  //initializing MPI
  MPICHECK(MPI_Init(&argc, &argv));
  MPICHECK(MPI_Comm_rank(MPI_COMM_WORLD, &myRank));
  MPICHECK(MPI_Comm_size(MPI_COMM_WORLD, &nRanks));

  //calculating localRank which is used in selecting a GPU
  uint64_t hostHashs[nRanks];
  char hostname[1024];
  getHostName(hostname, 1024);
  hostHashs[myRank] = getHostHash(hostname);
  MPICHECK(MPI_Allgather(MPI_IN_PLACE, 0, MPI_DATATYPE_NULL, hostHashs, sizeof(uint64_t), MPI_BYTE, MPI_COMM_WORLD));
  for (int p=0; p<nRanks; p++) {
	 if (p == myRank) break;
	 if (hostHashs[p] == hostHashs[myRank]) localRank++;
  }

  printf("my localRank :%d,myRank is:%d\n",localRank,myRank);

  //each process is using two GPUs
  int nDev = 2;

  float** sendbuff = (float**)malloc(nDev * sizeof(float*));
  float** recvbuff = (float**)malloc(nDev * sizeof(float*));
  cudaStream_t* s = (cudaStream_t*)malloc(sizeof(cudaStream_t)*nDev);

  //picking GPUs based on localRank
  for (int i = 0; i < nDev; ++i) {
	CUDACHECK(cudaSetDevice(localRank*nDev + i));
	CUDACHECK(cudaMalloc(sendbuff + i, size * sizeof(float)));
	CUDACHECK(cudaMalloc(recvbuff + i, size * sizeof(float)));
	CUDACHECK(cudaMemset(sendbuff[i], 1, size * sizeof(float)));
	CUDACHECK(cudaMemset(recvbuff[i], 0, size * sizeof(float)));


	float *h_arr;
	h_arr = (float *)malloc(size*sizeof(float));
	for (int i=0; i<size; ++i)
		h_arr[i] = i; // Or other values
	CUDACHECK(cudaMemcpy(sendbuff[i], h_arr, size*sizeof(float), cudaMemcpyHostToDevice));

	CUDACHECK(cudaStreamCreate(s+i));
  
  }
  for (int i = 0; i < nDev; ++i) {
	   CUDACHECK(cudaSetDevice(localRank*nDev + i));
	   float* recvCPU=(float*)malloc(size*sizeof(float));  //将数据从cuda 拷贝到cpu
	   CUDACHECK(cudaMemcpy(recvCPU, recvbuff[i], sizeof(float) * size, cudaMemcpyDeviceToHost));
	  printf("Begin Reduce Dev is %d of process myRank is %d, RecvBUf is %f,%f,%f,%f\n",i,myRank
	  ,recvCPU[0],recvCPU[1],recvCPU[2],recvCPU[3]);

  }
   ncclUniqueId id;
  ncclComm_t comms[nDev];

  //generating NCCL unique ID at one process and broadcasting it to all
  if (myRank == 0) ncclGetUniqueId(&id);
  MPICHECK(MPI_Bcast((void *)&id, sizeof(id), MPI_BYTE, 0, MPI_COMM_WORLD));

  //initializing NCCL, group API is required around ncclCommInitRank as it is
  //called across multiple GPUs in each thread/process
  NCCLCHECK(ncclGroupStart());
  for (int i=0; i<nDev-1; i++) {
	 CUDACHECK(cudaSetDevice(localRank*nDev + i));
	 NCCLCHECK(ncclCommInitRank(comms+i, nRanks*(nDev-1), id, myRank*(nDev-1) + i));
  }
   NCCLCHECK(ncclGroupEnd());

  //calling NCCL communication API. Group API is required when using
  //multiple devices per thread/process
  NCCLCHECK(ncclGroupStart());
  for (int i=0; i<nDev-1; i++)
	 //NCCLCHECK(ncclAllReduce((const void*)sendbuff[i], (void*)recvbuff[i], size, ncclFloat, ncclSum,
	//	   comms[i], s[i]));
		 NCCLCHECK(ncclReduce((const void *)sendbuff[i], (void*)recvbuff[i],size,ncclFloat, ncclSum, 0,comms[i], s[i]));

  NCCLCHECK(ncclGroupEnd());

  for (int i = 0; i < nDev; ++i) {
	   CUDACHECK(cudaSetDevice(localRank*nDev + i));
	   float* recvCPU=(float*)malloc(size*sizeof(float));  //将数据从cuda 拷贝到cpu
	   CUDACHECK(cudaMemcpy(recvCPU, recvbuff[i], sizeof(float) * size, cudaMemcpyDeviceToHost));
	  printf("End Reduce Dev is %d of process myRank is %d, RecvBUf is %f,%f,%f,%f\n",i,myRank
	  ,recvCPU[0],recvCPU[1],recvCPU[2],recvCPU[3]);

  }

  //synchronizing on CUDA stream to complete NCCL communication
  for (int i=0; i<nDev; i++)
	  CUDACHECK(cudaStreamSynchronize(s[i]));

  //freeing device memory
  for (int i=0; i<nDev; i++) {
	 CUDACHECK(cudaFree(sendbuff[i]));
	 CUDACHECK(cudaFree(recvbuff[i]));
  }

  //finalizing NCCL
  for (int i=0; i<nDev-1; i++) {
	 ncclCommDestroy(comms[i]);
  }
   MPICHECK(MPI_Finalize());

   printf("[MPI Rank %d] Success \n", myRank);
}

//测试单个线程多个跨机器节点之间AllGather
int main(int argc, char* argv[]){
  int size = 4;  //发送结束buffer的大小

  int myRank, nRanks, localRank = 0;

  //initializing MPI
  MPICHECK(MPI_Init(&argc, &argv));
  MPICHECK(MPI_Comm_rank(MPI_COMM_WORLD, &myRank));
  MPICHECK(MPI_Comm_size(MPI_COMM_WORLD, &nRanks));

  //calculating localRank which is used in selecting a GPU
  uint64_t hostHashs[nRanks];
  char hostname[1024];
  getHostName(hostname, 1024);
  hostHashs[myRank] = getHostHash(hostname);
  MPICHECK(MPI_Allgather(MPI_IN_PLACE, 0, MPI_DATATYPE_NULL, hostHashs, sizeof(uint64_t), MPI_BYTE, MPI_COMM_WORLD));
  for (int p=0; p<nRanks; p++) {
	 if (p == myRank) break;
	 if (hostHashs[p] == hostHashs[myRank]) localRank++;
  }

  printf("my localRank :%d,myRank is:%d\n",localRank,myRank);

  //each process is using two GPUs
  int nDev = 2;

  float** sendbuff = (float**)malloc(nDev * sizeof(float*));
  float** recvbuff = (float**)malloc(nDev * sizeof(float*));
  cudaStream_t* s = (cudaStream_t*)malloc(sizeof(cudaStream_t)*nDev);

  //picking GPUs based on localRank
  for (int i = 0; i < nDev; ++i) {
	CUDACHECK(cudaSetDevice(localRank*nDev + i));
	CUDACHECK(cudaMalloc(sendbuff + i, size * sizeof(float)));
	CUDACHECK(cudaMalloc(recvbuff + i, nRanks*nDev*size * sizeof(float)));
	CUDACHECK(cudaMemset(sendbuff[i], 1, size * sizeof(float)));
	CUDACHECK(cudaMemset(recvbuff[i], 0, size * sizeof(float)));


	float *h_arr;
	h_arr = (float *)malloc(size*sizeof(float));
	for (int i=0; i<size; ++i)
		h_arr[i] = i; // Or other values
	CUDACHECK(cudaMemcpy(sendbuff[i], h_arr, size*sizeof(float), cudaMemcpyHostToDevice));

	CUDACHECK(cudaStreamCreate(s+i));
  
  }
  for (int i = 0; i < nDev; ++i) {
	   CUDACHECK(cudaSetDevice(localRank*nDev + i));
	   float* recvCPU=(float*)malloc(nRanks*nDev*size*sizeof(float));  //将数据从cuda 拷贝到cpu
	   CUDACHECK(cudaMemcpy(recvCPU, recvbuff[i], sizeof(float) *nRanks*nDev* size, cudaMemcpyDeviceToHost));
	   printf("Begin Reduce Dev is %d of process myRank is %d, RecvBUf is ",i,myRank);
	   for(int j=0;j<nRanks*nDev*size;j++){
			printf("%f,",recvCPU[j]);
	   }
	   printf("\n");
		

  }
   ncclUniqueId id;
  ncclComm_t comms[nDev];

  //generating NCCL unique ID at one process and broadcasting it to all
  if (myRank == 0) ncclGetUniqueId(&id);
  MPICHECK(MPI_Bcast((void *)&id, sizeof(id), MPI_BYTE, 0, MPI_COMM_WORLD));

  //initializing NCCL, group API is required around ncclCommInitRank as it is
  //called across multiple GPUs in each thread/process
  NCCLCHECK(ncclGroupStart());
  for (int i=0; i<nDev-1; i++) {
	 CUDACHECK(cudaSetDevice(localRank*nDev + i));
	 NCCLCHECK(ncclCommInitRank(comms+i, nRanks*(nDev-1), id, myRank*(nDev-1) + i));
  }
   NCCLCHECK(ncclGroupEnd());

  //calling NCCL communication API. Group API is required when using
  //multiple devices per thread/process
  NCCLCHECK(ncclGroupStart());
  for (int i=0; i<nDev-1; i++)
	 //NCCLCHECK(ncclAllReduce((const void*)sendbuff[i], (void*)recvbuff[i], size, ncclFloat, ncclSum,
	//	   comms[i], s[i]));
		 NCCLCHECK(ncclAllGather((const void *)sendbuff[i], (void*)recvbuff[i], size, ncclFloat, comms[i], s[i]));

  NCCLCHECK(ncclGroupEnd());

  for (int i = 0; i < nDev; ++i) {
	   CUDACHECK(cudaSetDevice(localRank*nDev + i));
	   float* recvCPU=(float*)malloc(size*nDev*nRanks*sizeof(float));  //将数据从cuda 拷贝到cpu
	   CUDACHECK(cudaMemcpy(recvCPU, recvbuff[i], sizeof(float) * size*nDev*nRanks, cudaMemcpyDeviceToHost));
	    printf("End Reduce Dev is %d of process myRank is %d, RecvBUf is ",i,myRank);
	   for(int j=0;j<nRanks*nDev*size;j++){
			printf("%f,",recvCPU[j]);
	   }
	   printf("\n");
  }

  //synchronizing on CUDA stream to complete NCCL communication
  for (int i=0; i<nDev; i++)
	  CUDACHECK(cudaStreamSynchronize(s[i]));

  //freeing device memory
  for (int i=0; i<nDev; i++) {
	 CUDACHECK(cudaFree(sendbuff[i]));
	 CUDACHECK(cudaFree(recvbuff[i]));
  }

  //finalizing NCCL
  for (int i=0; i<nDev-1; i++) {
	 ncclCommDestroy(comms[i]);
  }
   MPICHECK(MPI_Finalize());

   printf("[MPI Rank %d] Success \n", myRank);
   return 0;
}
