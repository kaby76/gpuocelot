/*!
	\file CudaRuntime.h
	\author Andrew Kerr <arkerr@gatech.edu>
	\brief defines the CudaRuntime interface
	\date 11 Dec 2009
*/

#ifndef OCELOT_CUDA_RUNTIME_INTERFACE_H_INCLUDED
#define OCELOT_CUDA_RUNTIME_INTERFACE_H_INCLUDED

#include <ocelot/cuda/interface/cuda_runtime.h>

#define __dv(x) =x

namespace cuda {

	/*!
		Singleton object called directly by CUDA Runtime API wrapper - on instantiation, selects
			appropriate CUDA Runtime implementation and dispatches calls
	*/
	class CudaRuntimeInterface {
	public:
		/*
			singleton accessors
		*/
		static CudaRuntimeInterface *instance;
		
		static CudaRuntimeInterface *get();
		
		CudaRuntimeInterface();
		
		virtual ~CudaRuntimeInterface();
		
	public:
		/*
			Registration
		*/

		virtual void** cudaRegisterFatBinary(void *fatCubin);

		virtual void cudaUnregisterFatBinary(void **fatCubinHandle);

		virtual void cudaRegisterVar(void **fatCubinHandle, char *hostVar, char *deviceAddress, const char *deviceName, int ext, int size, int constant, int global);

		virtual void cudaRegisterTexture(
			void **fatCubinHandle,
			const struct textureReference *hostVar,
			const void **deviceAddress,
			const char *deviceName,
			int dim,
			int norm,
			int ext
		);

		virtual void cudaRegisterShared(
			void **fatCubinHandle,
			void **devicePtr
		);

		virtual void cudaRegisterSharedVar(
			void **fatCubinHandle,
			void **devicePtr,
			size_t size,
			size_t alignment,
			int storage
		);

		virtual void cudaRegisterFunction(
			void **fatCubinHandle,
			const char *hostFun,
			char *deviceFun,
			const char *deviceName,
			int thread_limit,
			uint3 *tid,
			uint3 *bid,
			dim3 *bDim,
			dim3 *gDim,
			int *wSize
		);
	
		/*
			Memory - 3D
		*/
		virtual cudaError_t  cudaMalloc3D(struct cudaPitchedPtr* pitchedDevPtr, struct cudaExtent extent);
		virtual cudaError_t  cudaMalloc3DArray(struct cudaArray** arrayPtr, const struct cudaChannelFormatDesc* desc, struct cudaExtent extent);
		virtual cudaError_t  cudaMemset3D(struct cudaPitchedPtr pitchedDevPtr, int value, struct cudaExtent extent);
		virtual cudaError_t  cudaMemcpy3D(const struct cudaMemcpy3DParms *p);
		virtual cudaError_t  cudaMemcpy3DAsync(const struct cudaMemcpy3DParms *p, cudaStream_t stream);

		/*
			Memory - linear
		*/

		virtual cudaError_t  cudaMalloc(void **devPtr, size_t size);
		virtual cudaError_t  cudaMallocHost(void **ptr, size_t size);
		virtual cudaError_t  cudaMallocPitch(void **devPtr, size_t *pitch, size_t width, size_t height);
		virtual cudaError_t  cudaMallocArray(struct cudaArray **array, const struct cudaChannelFormatDesc *desc, size_t width, size_t height __dv(1));
		virtual cudaError_t  cudaFree(void *devPtr);
		virtual cudaError_t  cudaFreeHost(void *ptr);
		virtual cudaError_t  cudaFreeArray(struct cudaArray *array);

		/*
			Memory - host allocations
		*/

		virtual cudaError_t  cudaHostAlloc(void **pHost, size_t bytes, unsigned int flags);
		virtual cudaError_t  cudaHostGetDevicePointer(void **pDevice, void *pHost, unsigned int flags);
		virtual cudaError_t  cudaHostGetFlags(unsigned int *pFlags, void *pHost);


		/*
			Memcpy
		*/

		virtual cudaError_t  cudaMemcpy(void *dst, const void *src, size_t count, enum cudaMemcpyKind kind);
		virtual cudaError_t  cudaMemcpyToArray(struct cudaArray *dst, size_t wOffset, size_t hOffset, const void *src, size_t count, enum cudaMemcpyKind kind);
		virtual cudaError_t  cudaMemcpyFromArray(void *dst, const struct cudaArray *src, size_t wOffset, size_t hOffset, size_t count, enum cudaMemcpyKind kind);
		virtual cudaError_t  cudaMemcpyArrayToArray(struct cudaArray *dst, size_t wOffsetDst, size_t hOffsetDst, const struct cudaArray *src, size_t wOffsetSrc, size_t hOffsetSrc, size_t count, enum cudaMemcpyKind kind __dv(cudaMemcpyDeviceToDevice));
		virtual cudaError_t  cudaMemcpy2D(void *dst, size_t dpitch, const void *src, size_t spitch, size_t width, size_t height, enum cudaMemcpyKind kind);
		virtual cudaError_t  cudaMemcpy2DToArray(struct cudaArray *dst, size_t wOffset, size_t hOffset, const void *src, size_t spitch, size_t width, size_t height, enum cudaMemcpyKind kind);
		virtual cudaError_t  cudaMemcpy2DFromArray(void *dst, size_t dpitch, const struct cudaArray *src, size_t wOffset, size_t hOffset, size_t width, size_t height, enum cudaMemcpyKind kind);
		virtual cudaError_t  cudaMemcpy2DArrayToArray(struct cudaArray *dst, size_t wOffsetDst, size_t hOffsetDst, const struct cudaArray *src, size_t wOffsetSrc, size_t hOffsetSrc, size_t width, size_t height, enum cudaMemcpyKind kind __dv(cudaMemcpyDeviceToDevice));
		virtual cudaError_t  cudaMemcpyToSymbol(const char *symbol, const void *src, size_t count, size_t offset __dv(0), enum cudaMemcpyKind kind __dv(cudaMemcpyHostToDevice));
		virtual cudaError_t  cudaMemcpyFromSymbol(void *dst, const char *symbol, size_t count, size_t offset __dv(0), enum cudaMemcpyKind kind __dv(cudaMemcpyDeviceToHost));

		/*
			Memcpy - async
		*/

		virtual cudaError_t  cudaMemcpyAsync(void *dst, const void *src, size_t count, enum cudaMemcpyKind kind, cudaStream_t stream);
		virtual cudaError_t  cudaMemcpyToArrayAsync(struct cudaArray *dst, size_t wOffset, size_t hOffset, const void *src, size_t count, enum cudaMemcpyKind kind, cudaStream_t stream);
		virtual cudaError_t  cudaMemcpyFromArrayAsync(void *dst, const struct cudaArray *src, size_t wOffset, size_t hOffset, size_t count, enum cudaMemcpyKind kind, cudaStream_t stream);
		virtual cudaError_t  cudaMemcpy2DAsync(void *dst, size_t dpitch, const void *src, size_t spitch, size_t width, size_t height, enum cudaMemcpyKind kind, cudaStream_t stream);
		virtual cudaError_t  cudaMemcpy2DToArrayAsync(struct cudaArray *dst, size_t wOffset, size_t hOffset, const void *src, size_t spitch, size_t width, size_t height, enum cudaMemcpyKind kind, cudaStream_t stream);
		virtual cudaError_t  cudaMemcpy2DFromArrayAsync(void *dst, size_t dpitch, const struct cudaArray *src, size_t wOffset, size_t hOffset, size_t width, size_t height, enum cudaMemcpyKind kind, cudaStream_t stream);
		virtual cudaError_t  cudaMemcpyToSymbolAsync(const char *symbol, const void *src, size_t count, size_t offset, enum cudaMemcpyKind kind, cudaStream_t stream);
		virtual cudaError_t  cudaMemcpyFromSymbolAsync(void *dst, const char *symbol, size_t count, size_t offset, enum cudaMemcpyKind kind, cudaStream_t stream);

		/*
			Memset
		*/

		virtual cudaError_t  cudaMemset(void *devPtr, int value, size_t count);
		virtual cudaError_t  cudaMemset2D(void *devPtr, size_t pitch, int value, size_t width, size_t height);

		/*
			Symbols
		*/

		virtual cudaError_t  cudaGetSymbolAddress(void **devPtr, const char *symbol);
		virtual cudaError_t  cudaGetSymbolSize(size_t *size, const char *symbol);

		/*
			Device enumeration and count
		*/

		virtual cudaError_t  cudaGetDeviceCount(int *count);
		virtual cudaError_t  cudaGetDeviceProperties(struct cudaDeviceProp *prop, int device);
		virtual cudaError_t  cudaChooseDevice(int *device, const struct cudaDeviceProp *prop);
		virtual cudaError_t  cudaSetDevice(int device);
		virtual cudaError_t  cudaGetDevice(int *device);
		virtual cudaError_t  cudaSetValidDevices(int *device_arr, int len);
		virtual cudaError_t  cudaSetDeviceFlags( int flags );

		/*
			Texture binding
		*/

		virtual cudaError_t  cudaBindTexture(size_t *offset, const struct textureReference *texref, const void *devPtr, const struct cudaChannelFormatDesc *desc, size_t size __dv(UINT_MAX));
		virtual cudaError_t  cudaBindTexture2D(size_t *offset,const struct textureReference *texref,const void *devPtr, const struct cudaChannelFormatDesc *desc,size_t width, size_t height, size_t pitch);
		virtual cudaError_t  cudaBindTextureToArray(const struct textureReference *texref, const struct cudaArray *array, const struct cudaChannelFormatDesc *desc);
		virtual cudaError_t  cudaUnbindTexture(const struct textureReference *texref);
		virtual cudaError_t  cudaGetTextureAlignmentOffset(size_t *offset, const struct textureReference *texref);
		virtual cudaError_t  cudaGetTextureReference(const struct textureReference **texref, const char *symbol);

		/*
			Channel creation
		*/

		virtual cudaError_t  cudaGetChannelDesc(struct cudaChannelFormatDesc *desc, const struct cudaArray *array);
		struct cudaChannelFormatDesc  cudaCreateChannelDesc(int x, int y, int z, int w, enum cudaChannelFormatKind f);

		/*
			Error enumeration
		*/

		virtual cudaError_t  cudaGetLastError(void);

		/*
			Kernel launch
		*/

		virtual cudaError_t  cudaConfigureCall(dim3 gridDim, dim3 blockDim, size_t sharedMem __dv(0), cudaStream_t stream __dv(0));
		virtual cudaError_t  cudaSetupArgument(const void *arg, size_t size, size_t offset);
		virtual cudaError_t  cudaLaunch(const char *entry);
		virtual cudaError_t  cudaFuncGetAttributes(struct cudaFuncAttributes *attr, const char *func);

		/*
			Stream creation
		*/

		virtual cudaError_t  cudaStreamCreate(cudaStream_t *pStream);
		virtual cudaError_t  cudaStreamDestroy(cudaStream_t stream);
		virtual cudaError_t  cudaStreamSynchronize(cudaStream_t stream);
		virtual cudaError_t  cudaStreamQuery(cudaStream_t stream);

		/*
			Event creation
		*/

		virtual cudaError_t  cudaEventCreate(cudaEvent_t *event);
		virtual cudaError_t  cudaEventCreateWithFlags(cudaEvent_t *event, int flags);
		virtual cudaError_t  cudaEventRecord(cudaEvent_t event, cudaStream_t stream);
		virtual cudaError_t  cudaEventQuery(cudaEvent_t event);
		virtual cudaError_t  cudaEventSynchronize(cudaEvent_t event);
		virtual cudaError_t  cudaEventDestroy(cudaEvent_t event);
		virtual cudaError_t  cudaEventElapsedTime(float *ms, cudaEvent_t start, cudaEvent_t end);


		virtual cudaError_t cudaGLMapBufferObject(void **devPtr, GLuint bufObj);
		virtual cudaError_t cudaGLMapBufferObjectAsync(void **devPtr, GLuint bufObj, cudaStream_t stream);
		virtual cudaError_t cudaGLRegisterBufferObject(GLuint bufObj);
		virtual cudaError_t cudaGLSetBufferObjectMapFlags(GLuint bufObj, unsigned int flags);
		virtual cudaError_t cudaGLSetGLDevice(int device);
		virtual cudaError_t cudaGLUnmapBufferObject(GLuint bufObj);
		virtual cudaError_t cudaGLUnmapBufferObjectAsync(GLuint bufObj, cudaStream_t stream);
		virtual cudaError_t cudaGLUnregisterBufferObject(GLuint bufObj);

		/*
			double precision
		*/

		virtual cudaError_t  cudaSetDoubleForDevice(double *d);
		virtual cudaError_t  cudaSetDoubleForHost(double *d);

		/*
			Thread synchronization
		*/

		virtual cudaError_t  cudaThreadExit(void);
		virtual cudaError_t  cudaThreadSynchronize(void);

		/*
			Version accessors
		*/

		virtual cudaError_t  cudaDriverGetVersion(int *driverVersion);
		virtual cudaError_t  cudaRuntimeGetVersion(int *runtimeVersion);
	};

}

#endif

