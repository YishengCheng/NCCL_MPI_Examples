# �������� mpi_test
���룺
mpicc -o3 mpi_test.c -o mpi_test
ִ�У�
mpirun -n 4 mpi_test


# ����ִ�� singleProcess.cpp
nvcc -o first_cuda singleProcess.cpp  -I/usr/local/nccl/include -L/usr/local/nccl/lib -l nccl


# ʹ�ñ���mpi ����
mpicc -o3 oneDevicePerprocess.cpp -o cpi -I/usr/local/cuda/include -L/usr/local/cuda/lib64 -lcudart -lcuda  -I/usr/local/nccl/include -L/usr/local/nccl/lib -l nccl


#ʹ�� run.sh ���������ļ�
./run.sh [source.cpp]  output