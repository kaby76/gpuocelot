/*!
	\file Executive.h
	\author Andrew Kerr <arkerr@gatech.edu>
	\brief implements Ocelot's executive manager
*/

#ifndef OCELOT_EXECUTIVE_H_INCLUDED
#define OCELOT_EXECUTIVE_H_INCLUDED

// C++
#include <map>
#include <set>

// Ocelot
#include <ocelot/executive/interface/ApplicationState.h>
#include <ocelot/executive/interface/Device.h>
#include <ocelot/translator/interface/Translator.h>

// forward declared classes
namespace ir {
	class Kernel;
	class Module;
}

// Executive class
namespace executive {

	struct dim3 {
		int x, y, z;
	};

	//! maps module name => ir::Modile
	typedef std::map< std::string, ir::Module * > ModuleMap;
	
	//! maps device => address space - 0 is always host
	typedef std::map< int, int > AddressSpaceMap;
	
	typedef std::map< void *, MemoryAllocation > MemoryAllocationMap;
	
	//! maps (address space, ptr) => allocation
	typedef std::map< int, MemoryAllocationMap > DeviceMemoryAllocationMap;

	typedef std::map< std::string, Texture > TextureMap;

	/*!
		Completely specifies the execution environment of an Ocelot application
	*/
	class Executive {
	public:
	
	public:
	
		// textures
		TextureMap textures;
		
		// global variables
		GlobalMap globals;
		
		// memory allocations
		DeviceMemoryAllocationMap memoryAllocations;

		//! maps device => address space - 0 is always host address space
		AddressSpaceMap addressSpaces;
		
		// modules + kernels
		ModuleMap modules;
		
		//! vector of available Ocelot devices
		DeviceVector devices;
		
	public:
	
		Executive();
		~Executive();
	
	public:
	
		//
		// registration functions
		//
		
		/*!
			\brief loads a module with a given name and PTX representation
		*/
		void loadModule(std::string name, bool, std::istream & ptx);
		
		/*!
			\brief registers a global variable
			\param module
			\param name this must be unique - modules aren't quite namespaces
			\param hostPtr pointer in host memory - identifies the global
			\param devicePtr pointer in device memory
			\param size size of global in bytes
			\param addrSpace indicates which address space the global resides in on the device
		*/
		void registerGlobalVariable(const char *module, const char *name, void *hostPtr, 
			void *devicePtr, size_t size, DeviceAddressSpace addrSpace);
		
		/*!
			\brief registers a texture defined in a particular module
			\param module name of module
			\param name name of texture
			\param number of dimensions (1, 2, or 3)
			\param if 1, texture coordinates are normalized
		*/
		void registerTexture(const char *module, const char *name, int dimensions, int normalized);
		
	public:
	
		//
		// memory allocation functions
		//
		
		/*
			Memory - linear
		*/

		/*!
			\brief allocate memory on the selected device's address space
			\param devPtr places resulting pointer
			\param size number of bytes to allocate
			\return true if successful
		*/
		bool malloc(void **devPtr, size_t size);

		/*!
			\brief allocates memory on the host that is well-suited for use with Ocelot
			\param ptr places resulting pointer
			\param size number of bytes to allocate
			\return true if successful
		*/
		bool mallocHost(void **ptr, size_t size);
		
		/*!
			\brief allocates memory on the selected device and returns pitch
			\param devPtr places resulting pointer
			\param pitch [out] pointer to variable containing pitch of resulting allocation
			\param width width in bytes of allocation
			\parma height height in bytes of allocation
			\return true if successful
		*/
		bool mallocPitch(void **devPtr, size_t *pitch, size_t width, size_t height);
		
		/*!
			\brief allocates an array of memory on the selected device
			\param array places resulting pointer here
			\param desc
			\param desc
			\param width
			\param height
			\return true if successful
		*/
		bool mallocArray(struct cudaArray **array, const ChannelFormatDesc & desc, 
			size_t width, size_t height);
		
		/*!
			\brief frees an allocation
			\param devPtr device pointer
		*/
		bool free(void *devPtr);
		
		/*!
			\brief frees host-allocated memory
		*/
		bool freeHost(void *ptr);
		
		/*!
			\brief frees an array
		*/
		bool freeArray(struct cudaArray *array);

		/*! 
			\brief Determine if a memory access is valid 
		
			This should search device specific allocations as well as 
			global allocations.
		
			\param device The device doing the access
			\param base Pointer to the base of the access
			\param size The size of the access
		*/
		bool checkMemoryAccess(int device, const void* base, size_t size) const;

		/*!
			Given a pointer, determine the allocated block and 
			corresponding MemoryAllocation record to which it belongs.

			\param device GUID of device
			\param ptr pointer to some byte
			\return record of memory allocation; if nothing could be found, 
				the record's ISA is Unknown
		*/
		MemoryAllocation getMemoryAllocation(int device, const void *ptr) const;
		
	public:
	
		//
		// texture binding functions
		//
		
		/*!
			\brief binds a texture by name to a pointer to a device memory allocation
			\param offset [out] offset that must be added to fetches to achieved desired pixel
			\param texture name of texture
			\param devPtr device memory allocation
			\param format channel description
			\param size bytes in texture
		*/
		bool bindTexture(size_t *offset, const std::string & texture, const void *devPtr, 
			const ChannelFormatDesc &format, size_t size);
		
		/*!
			\brief binds a 2D texture by name to a device pointer with a given width, height, and pitch
			\param offset [out] offset that must be added to fetches to achieved desired pixel
			\param texture name of texture
			\param devPtr device memory allocation
			\param format channel description
			\param width width of texture in texels
			\param height height of texture in texels
			\param pitch number of bytes between texels of the same column in consecutive rows
		*/
		bool bindTexture2D(size_t *offset, const std::string & texture, const void *devPtr,
			const ChannelFormatDesc &format, size_t width, size_t height, size_t pitch);
		
		/*!
			\brief binds a texture to an array
		*/
		bool bindTextureToArray(const std::string & texture, void *array, const ChannelFormatDesc &desc);
		
		/*!
			\brief unbinds a previously bound texture
		*/
		void unbindTexture(const std::string & texture);
		
		/*!
			gets alignment of a named texture
		*/
		size_t getTextureAlignmentOffset(const std::string & texture);
		
	public:
	
		//
		// kernel launch and synchronization functions
		//
		
		/*!
			Gets a kernel by ISA, module, kernel name.

			\param isa instruction set of desired kernel
			\param module name of module from which kernel is to be selected
			\param kernelName name of kernel

			\return instance of kernel with requested ISA or 0 on failure.
		*/
		ir::Kernel *getKernel(ir::Instruction::Architecture isa, const std::string& module, 
			const std::string& kernel);	
		
		/*!
			\brief translates a kernel to the given ISA
			\param isa ISA to translate to
			\param module name of module to which kernel belongs
			\param kernel name of kernel
			\return translated kernel or NULL on translation failure
		*/
		ir::Kernel *translatedToISA(ir::Instruction::Architecture isa, const std::string &module,
			const std::string &kernel);
		
		/*!
			\brief helper function for launching a kernel
			\param module module name
			\param kernel kernel name
			\param grid grid dimensions
			\param block block dimensions
			\param sharedMemory shared memory size
			\param parameterBlock array of bytes for parameter memory
			\param parameterBlockSize number of bytes in parameter memory
		*/
		void launch(const std::string & module, const std::string & kernel, dim3 grid, dim3 block,
			size_t sharedMemory, unsigned char *parameterBlock, size_t parameterBlockSize);
			
		/*!
			\brief block on kernel executing on selected device
		*/
		void threadSynchronize();
		
	public:
		//
		// device management functions
		//

		//! returns a constant vector of available devices
		const DeviceVector & getDevices() const;

		/*! 
			selects a device [ this is intended to be a low-cost operation called at every CUDA runtime 
			API call]
		*/
		bool selectDevice(int device);
		
		/*! 
			gets selected device identifier
		*/
		int getSelectedDevice() const;
		
		//! indicates preferred ISA [device will be chosen if available]
		void setPreferredISA(int isa);
		
		//! only listed devices may be selected
		void filterDevices(std::vector<int> & devices);
		
		//! only devices with listed ISAs may be selected
		void filterISAs(std::vector<int> & ISAs);
		
		//! returns the address space of the selected device
		int getDeviceAddressSpace() const;
		
	public:
		//
		// translation settings
		//

		/*!
			\brief This sets the optimization level 
		
			\param l The new optimization level.
		*/
		void setOptimizationLevel(translator::Translator::OptimizationLevel l);
		
		/*!
			\brief ensures that all kernels have an executable translation for the selected device
		*/
		void translateToISA();
		
	public:
		//
		// Ocelot native interface functions
		//
		
		
	};

}

#endif

