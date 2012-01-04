/*!
	\file DynamicTranslationCache.cpp
	\author Andrew Kerr <arkerr@gatech.edu>
	\date November 11, 2011
	\brief cache of translations
*/

// Ocelot includes
#include <ocelot/executive/interface/DynamicTranslationCache.h>
#include <ocelot/executive/interface/DynamicMulticoreKernel.h>

// Hydrazine includes
#include <hydrazine/implementation/debug.h>
#include <hydrazine/implementation/Exception.h>
#include <hydrazine/implementation/math.h>
#include <hydrazine/interface/Casts.h>

// LLVM Includes
#ifdef HAVE_LLVM
#include <llvm/PassManager.h>
#include <llvm/Transforms/Utils/Cloning.h>
#include <llvm/Transforms/Scalar.h>
#include <llvm/PassManager.h>
#include <llvm/Target/TargetData.h>
#include <llvm/ExecutionEngine/JIT.h>
#include <llvm/ExecutionEngine/ExecutionEngine.h>
#include <llvm/Assembly/Parser.h>
#include <llvm/Analysis/Verifier.h>
#include <llvm/LLVMContext.h>
#include <llvm/Module.h>
#include <llvm/Support/SourceMgr.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Instructions.h>
#include <llvm/Support/CFG.h>
#include <llvm/Support/raw_os_ostream.h>
#endif

//////////////////////////////////////////////////////////////////////////////////////////////////

#define Ocelot_Exception(x) { std::stringstream ss; ss << x; std::cerr << x << std::endl; \
	throw hydrazine::Exception(ss.str()); }

// Preprocessor Macros
#ifdef REPORT_BASE
#undef REPORT_BASE
#endif

/////////////////////////////////////////////////////////////////////////////////////////////////

#define REPORT_PTX_MASTER 1								// master toggle for reporting PTX kernels
#define REPORT_SOURCE_PTX_KERNELS 0				// PTX prior to transformations
#define REPORT_PARITIONED_PTX_KERNELS 0		// final output PTX ready to be translated
#define REPORT_PTX_SUBKERNELS 0

#define REPORT_LLVM_MASTER 1							// master toggle for reporting LLVM kernels
#define REPORT_SOURCE_LLVM_ASSEMBLY 1			// assembly output of translator
#define REPORT_ALL_LLVM_ASSEMBLY 0				// turns on LLOVM assembly at each state
#define REPORT_OPTIMIZED_LLVM_ASSEMBLY 1	// final output of LLVM translation and optimization
#define REPORT_LLVM_VERIFY_FAILURE 1			// emit assembly if verification fails
#define REPORT_SCHEDULE_OPERATIONS 0			// scheduling events
#define REPORT_TRANSLATION_OPERATIONS 1		// translation events

#define REPORT_TRANSLATIONS 0

#define REPORT_BASE 0

/////////////////////////////////////////////////////////////////////////////////////////////////

std::ostream &Instruction_print(std::ostream &out, llvm::Instruction * inst);


///////////////////////////////////////////////////////////////////////////////////////////////////

executive::DynamicTranslationCache::DynamicTranslationCache() {

}

executive::DynamicTranslationCache::~DynamicTranslationCache() {

}

executive::DynamicTranslationCache::Translation *
executive::DynamicTranslationCache::getOrInsertTranslation(
	int warpsize, SubkernelId subkernelId, unsigned int specialization) {
	
	report(" DynamicTranslationCache::getOrInsertTranslation(ws: " << warpsize 
		<< ", skId: " << subkernel << ", specialization = " << specialization << ")");
		
	Translation *translation = 0;
		
	TranslationCacheMap::iterator translation_it = translationCache.find(subkernelId);
	if (translation_it != translationCache.end()) {
		WarpTranslationMap::iterator warp_it = translation_it->second.find(warpsize);
		if (warp_it != translation_it->second.end()) {
			translation = warp_it->second;
			report("  found in translation cache");
		}
	}
	
	if (!translation) {	
		translation = _specializeTranslation(*subkernelsToKernel[subkernelId], subkernelId, 
			optimizationLevel, warpSize, specialization);
		
		report("  inserted in translation cache");
	}
	
	return translation;
}

//! \brief indicates to the translation cache a kernel is about to be executed
void executive::DynamicTranslationCache::registerKernel(DynamicMulticoreKernel *kernel) {
	report("DynamicTranslationCache::registerKernel(" << kernel->name << ")");
	
	ModuleMap::iterator module_it = modules.find(kernel->module()->name);
	assert(module_it != modules.end());
	
	ModuleMetadata &module = module_it->second;
	TranslatedKernelNameMap::iterator kernel_it = module.kernels.find(kernel->name);
	if (kernel_it == module.kernels.end()) {
		TranslatedKernel *translatedKernel = new TranslatedKernel(kernel);
		translatedKernel->localMemorySize = 0;
		translatedKernel->sharedMemorySize = 0;
		kernels[kernel] = translatedKernel;
				
		_translateKernel(*translatedKernel);
		report("  registered new kernel");
	}
	else {
		// do nothing.
		repoort("  kernel already registered");
	}	
}

//! \brief loads a module into the translation cache
bool executive::DynamicTranslationCache::loadModule(const ir::Module *module, 
	executive::DynamicMulticoreDevice *device) {
	
	report("DynamicTranslationCache::loadModule(" << module->name << ")");
	
	ModuleMetadata newModule;
	newModule.ptxModule = module;
	newModule.device = device;
	modules[module->name] = newModule;
	
	return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////

executive::DynamicTranslationCache::TranslatedKernel::TranslatedKernel(DynamicMulticoreKernel *_kernel):
	llvmModule(0), kernel(_kernel), metadata(0), localMemorySize(0), sharedMemorySize(0) {
	
	llvmModule = new llvm::Module(kernel->name.c_str(), llvm::getGlobalContext());
}

///////////////////////////////////////////////////////////////////////////////////////////////////

executive::DynamicTranslationCache::Translation::Translation(llvm::Function *_llvmFunction):
	llvmFunction(_llvmFunction), function(0) {

}

void executive::DynamicTranslationCache::Translation::execute(LLVMContext **contexts) const {
	assert(function);
}

///////////////////////////////////////////////////////////////////////////////////////////////////


#ifdef HAVE_LLVM
static unsigned int pad(unsigned int& size, unsigned int alignment) {

	unsigned int padding = alignment - (size % alignment);
	padding = (alignment == padding) ? 0 : padding;
	size += padding;
	return padding;
}

/*!
	\brief sets addres space identifier to global for uses of global variables 
*/
static void setupGlobalMemoryReferences(ir::PTXKernel& kernel) {

	for (ir::ControlFlowGraph::iterator block = kernel.cfg()->begin(); 
		block != kernel.cfg()->end(); ++block) {
		
		for (ir::ControlFlowGraph::InstructionList::iterator 
			instruction = block->instructions.begin(); 
			instruction != block->instructions.end(); ++instruction ) {
			
			ir::PTXInstruction& ptx = static_cast<ir::PTXInstruction&>(**instruction);

			if (ptx.opcode == ir::PTXInstruction::Mov 
				&& (ptx.a.addressMode == ir::PTXOperand::Address
				|| ptx.a.addressMode == ir::PTXOperand::Indirect)) {
				
				ir::Module::GlobalMap::const_iterator global = kernel.module->globals().find(ptx.a.identifier);
					
				if(global == kernel.module->globals().end() ||
					global->second.statement.directive != ir::PTXStatement::Global) {
					continue;
				}
					
				ptx.addressSpace = ir::PTXInstruction::Global;
						
				reportE(REPORT_TRANSLATION_OPERATIONS, "   For instruction \"" << ptx.toString() 
					<< "\" setting address space to global.");
			}
		}
	}
}

/*!
	\brief computes layout of kernel arguments
*/
static void setupArgumentMemoryReferences(ir::PTXKernel& kernel,
	DynamicMulticoreExecutive::Metadata* metadata) {
	
	typedef std::unordered_map<std::string, unsigned int> OffsetMap;
	reportE(REPORT_TRANSLATION_OPERATIONS, "  Setting up argument memory references.");

	unsigned int offset = 0;
	
	OffsetMap offsets;
	
	for(ir::Kernel::ParameterVector::const_iterator
		argument = kernel.arguments.begin();
		argument != kernel.arguments.end(); ++argument) {
		
		pad(offset, argument->getAlignment());
		offsets.insert(std::make_pair(argument->name, offset));
		
		reportE(REPORT_TRANSLATION_OPERATIONS, "   Argument " << argument->name << ", offset " << offset);
		offset += argument->getSize();
	}

	for (ir::ControlFlowGraph::iterator block = kernel.cfg()->begin(); block != kernel.cfg()->end(); 
		++block) {
		
		for (ir::ControlFlowGraph::InstructionList::iterator instruction = block->instructions.begin(); 
			instruction != block->instructions.end(); ++instruction ) {
			
			ir::PTXInstruction &ptx = static_cast<ir::PTXInstruction&>(**instruction);
			ir::PTXOperand *operands[] = {&ptx.d, &ptx.a, &ptx.b, &ptx.c};

			if (ptx.opcode == ir::PTXInstruction::Mov || ptx.opcode == ir::PTXInstruction::Ld
				|| ptx.opcode == ir::PTXInstruction::St) {
				
				for (unsigned int i = 0; i != 4; ++i) {
				
					if (operands[i]->addressMode == ir::PTXOperand::Address) {
					
						OffsetMap::iterator argument = offsets.find(operands[i]->identifier);
						
						if (argument != offsets.end()) {
							reportE(REPORT_TRANSLATION_OPERATIONS, "   For instruction \"" << ptx.toString() 
								<< "\" mapping \"" << argument->first 
								<< "\" to "
								<< (operands[i]->offset + argument->second));
							operands[i]->offset     += argument->second;
							operands[i]->isArgument = true;
						}
					}
				}
			}
		}
	}
	
	metadata->argumentSize = offset;
	
	reportE(REPORT_TRANSLATION_OPERATIONS, "   total argument memory size is " 
		<< metadata->argumentSize);
}

/*!
	\brief 
*/
static void setupParameterMemoryReferences(ir::PTXKernel& kernel,
	DynamicMulticoreExecutive::Metadata* metadata) {
	
	typedef std::unordered_map<std::string, unsigned int> OffsetMap;
	reportE(REPORT_TRANSLATION_OPERATIONS, "  Setting up parameter memory references.");

	metadata->parameterSize	= 0;
	
	OffsetMap offsets;
	
	// Determine the order that parameters are passed as arguments to calls
	for (ir::ControlFlowGraph::iterator block = kernel.cfg()->begin(); 
		block != kernel.cfg()->end(); ++block) {
		
		for (ir::ControlFlowGraph::InstructionList::iterator instruction = block->instructions.begin(); 
			instruction != block->instructions.end(); ++instruction) {
			
			ir::PTXInstruction& ptx = static_cast<ir::PTXInstruction&>(**instruction);
			if (ptx.opcode != ir::PTXInstruction::Call) continue;
			
			if (ptx.a.identifier == "ptx.warp.divergent") {
				continue;
			}
			
			unsigned int offset = 0;
			
			reportE(REPORT_TRANSLATION_OPERATIONS, "   For arguments of call instruction '"
				<< ptx.toString() << "'");
				
			for (ir::PTXOperand::Array::const_iterator argument = ptx.d.array.begin();
				argument != ptx.d.array.end(); ++argument) {
				
				pad(offset, ir::PTXOperand::bytes(argument->type));
				assert(offsets.count(argument->identifier) == 0);
				offsets.insert(std::make_pair(argument->identifier, offset));
				
				reportE(REPORT_TRANSLATION_OPERATIONS, "    mapping '" << argument->identifier
					<< "' to " << offset);
					
				offset += ir::PTXOperand::bytes(argument->type);
			}
			
			for (ir::PTXOperand::Array::const_iterator argument = ptx.b.array.begin();
				argument != ptx.b.array.end(); ++argument) {
				
				pad(offset, ir::PTXOperand::bytes(argument->type));
				assert(offsets.count(argument->identifier) == 0);
				offsets.insert(std::make_pair(argument->identifier, offset));
				
				reportE(REPORT_TRANSLATION_OPERATIONS, "    mapping '" << argument->identifier
					<< "' to " << offset);
					
				offset += ir::PTXOperand::bytes(argument->type);
			}
			
			metadata->parameterSize = std::max(offset, metadata->parameterSize);
		}
	}
	
	for(ir::ControlFlowGraph::iterator block = kernel.cfg()->begin(); block != kernel.cfg()->end(); 
		++block) {
		
		for (ir::ControlFlowGraph::InstructionList::iterator instruction = block->instructions.begin(); 
			instruction != block->instructions.end(); ++instruction) {
			
			ir::PTXInstruction& ptx = static_cast<ir::PTXInstruction&>(**instruction);

			ir::PTXOperand* operands[] = {&ptx.d, &ptx.a, &ptx.b, &ptx.c};

			if(ptx.opcode == ir::PTXInstruction::Mov || ptx.opcode == ir::PTXInstruction::Ld
				|| ptx.opcode == ir::PTXInstruction::St) {
				
				for(unsigned int i = 0; i != 4; ++i) {
				
					if(operands[i]->addressMode == ir::PTXOperand::Address) {
					
						OffsetMap::iterator parameter = offsets.find(operands[i]->identifier);
						
						if(parameter != offsets.end()) {
							reportE(REPORT_TRANSLATION_OPERATIONS, "   For instruction \"" 
								<< ptx.toString() << "\" mapping \"" 
								<< parameter->first << "\" to " 
								<< (parameter->second + operands[i]->offset));
							
							operands[i]->offset += parameter->second;
							operands[i]->isArgument = false;
						}
					}
				}
			}
		}
	}
	
	// In order to handle tail calls resuing the current stack frame, allocate 
	//  enough space for the max number of parameters in the module
	for(ir::Module::KernelMap::const_iterator function = kernel.module->kernels().begin();
		function != kernel.module->kernels().end(); ++function) {
		
		if (!function->second->function()) continue;
		
		unsigned int bytes = 0;
		
		for (ir::Kernel::ParameterVector::const_iterator argument = function->second->arguments.begin();
			argument != function->second->arguments.end(); ++argument) {
			pad(bytes, argument->getSize());
			bytes += argument->getSize();
		}
		
		metadata->parameterSize = std::max(bytes, metadata->parameterSize);
	}
	
	reportE(REPORT_TRANSLATION_OPERATIONS, "   total parameter memory size is " 
		<< metadata->parameterSize);
}

/*!
	\brief lays out shared memory
*/
static void setupSharedMemoryReferences(ir::PTXKernel& kernel, 
	DynamicMulticoreExecutive::Metadata* metadata) {
	
	typedef std::unordered_map<std::string, unsigned int> OffsetMap;
	typedef std::unordered_set<std::string> StringSet;
	typedef std::deque<ir::PTXOperand*> OperandVector;
	typedef std::unordered_map<std::string, ir::Module::GlobalMap::const_iterator> GlobalMap;

	reportE(REPORT_TRANSLATION_OPERATIONS,  "  Setting up shared memory references." );

	OffsetMap offsets;
	StringSet external;
	OperandVector externalOperands;

	unsigned int externalAlignment = 1;             
	metadata->sharedSize = 0;

	for(ir::Module::GlobalMap::const_iterator global = kernel.module->globals().begin(); 
		global != kernel.module->globals().end(); ++global) {
		
		if (global->second.statement.directive == ir::PTXStatement::Shared) {
			if (global->second.statement.attribute == ir::PTXStatement::Extern) {
				reportE(REPORT_TRANSLATION_OPERATIONS, "   Allocating global external shared variable " 
					<< global->second.statement.name);
					
				assertM(external.count(global->second.statement.name) == 0, 
					"External global " << global->second.statement.name 
					<< " more than once.");
					
				external.insert(global->second.statement.name);
				externalAlignment = std::max(externalAlignment, 
					(unsigned) global->second.statement.alignment);
				externalAlignment = std::max(externalAlignment, 
					ir::PTXOperand::bytes(global->second.statement.type));
			}
			else {
				reportE(REPORT_TRANSLATION_OPERATIONS, "   Allocating global shared variable " 
					<< global->second.statement.name);
				pad(metadata->sharedSize, global->second.statement.alignment);
				offsets.insert(std::make_pair(global->second.statement.name, 
					metadata->sharedSize));
				metadata->sharedSize += global->second.statement.bytes();
			}
		}
	}

	for(ir::Kernel::LocalMap::const_iterator local = kernel.locals.begin(); 
		local != kernel.locals.end(); ++local) {
		
		if (local->second.space == ir::PTXInstruction::Shared) {
			if (local->second.attribute == ir::PTXStatement::Extern) {
				reportE(REPORT_TRANSLATION_OPERATIONS, "    Found local external shared variable " 
					<< local->second.name);
				
				assert(external.count(local->second.name) == 0);
				
				external.insert(local->second.name);
				externalAlignment = std::max(externalAlignment, (unsigned) local->second.alignment);
				externalAlignment = std::max(externalAlignment, ir::PTXOperand::bytes(local->second.type));
			}
			else {
				reportE(REPORT_TRANSLATION_OPERATIONS, "   Found local shared variable " 
					<< local->second.name << " of size " 
					<< local->second.getSize());
				
				pad(metadata->sharedSize, local->second.alignment);
				offsets.insert(std::make_pair(local->second.name, metadata->sharedSize));
				metadata->sharedSize += local->second.getSize();
			}
		}
	}
                
	for(ir::ControlFlowGraph::iterator block = kernel.cfg()->begin(); 
		block != kernel.cfg()->end(); ++block) {
		
		for(ir::ControlFlowGraph::InstructionList::iterator instruction = block->instructions.begin(); 
			instruction != block->instructions.end(); ++instruction) {
			
			ir::PTXInstruction& ptx = static_cast<ir::PTXInstruction&>(**instruction);

			ir::PTXOperand* operands[] = {&ptx.d, &ptx.a, &ptx.b, &ptx.c};

			if(ptx.opcode == ir::PTXInstruction::Mov
				|| ptx.opcode == ir::PTXInstruction::Ld
				|| ptx.opcode == ir::PTXInstruction::St
				|| ptx.opcode == ir::PTXInstruction::Cvta)
			{
				for (unsigned int i = 0; i != 4; ++i) {
					if (operands[i]->addressMode == ir::PTXOperand::Address) {
						StringSet::iterator si = external.find(operands[i]->identifier);
						
						if (si != external.end()) {
							reportE(REPORT_TRANSLATION_OPERATIONS, "   For instruction \"" 
								<< ptx.toString() 
								<< "\", mapping shared label \"" << *si 
								<< "\" to external shared memory.");
							externalOperands.push_back(operands[i]);
							continue;
						}
	
						OffsetMap::iterator offset = offsets.find(operands[i]->identifier);
						if (offsets.end() != offset) {
							ptx.addressSpace = ir::PTXInstruction::Shared;
							operands[i]->offset += offset->second;
							
							reportE(REPORT_TRANSLATION_OPERATIONS, "   For instruction " 
								<< ptx.toString() << ", mapping shared label " 
								<< offset->first << " to " << offset->second);
						}
					}
				}
			}
		}
	}

	pad(metadata->sharedSize, externalAlignment);

	reportE(REPORT_TRANSLATION_OPERATIONS, "   Mapping external shared variables.");
	for(OperandVector::iterator operand = externalOperands.begin(); 
		operand != externalOperands.end(); ++operand) {
		
		reportE(REPORT_TRANSLATION_OPERATIONS, "    Mapping external shared label " 
			<< (*operand)->identifier << " to " << metadata->sharedSize);
		(*operand)->offset += metadata->sharedSize;
	}

	reportE(REPORT_TRANSLATION_OPERATIONS,  "   Total shared memory size is " 
		<< metadata->sharedSize << "." );
}

/*!
	\brief 
*/
static void setupConstantMemoryReferences(ir::PTXKernel& kernel,
	DynamicMulticoreExecutive::Metadata* metadata)
{
	reportE(REPORT_TRANSLATION_OPERATIONS,  "  Setting up constant memory references." );
	typedef std::unordered_map<std::string, unsigned int> OffsetMap;

	metadata->constantSize = 0;
	OffsetMap constants;
	
	for(ir::Module::GlobalMap::const_iterator global = kernel.module->globals().begin(); 
		global != kernel.module->globals().end(); ++global) {
		
		if (global->second.statement.directive == ir::PTXStatement::Const) {
		
			reportE(REPORT_TRANSLATION_OPERATIONS,  "   Found global constant variable " 
				<< global->second.statement.name << " of size " 
				<< global->second.statement.bytes() );
			
			pad(metadata->constantSize, global->second.statement.alignment);
			constants.insert(std::make_pair(global->second.statement.name,
				metadata->constantSize));
			metadata->constantSize += global->second.statement.bytes();
		}
	}

	reportE(REPORT_TRANSLATION_OPERATIONS, "   Total constant memory size is " 
		<< metadata->constantSize);

	for(ir::ControlFlowGraph::iterator block = kernel.cfg()->begin(); 
		block != kernel.cfg()->end(); ++block) {
		
		for(ir::ControlFlowGraph::InstructionList::iterator instruction = block->instructions.begin(); 
			instruction != block->instructions.end(); ++instruction) {
			
			ir::PTXInstruction& ptx = static_cast<ir::PTXInstruction&>(**instruction);
			ir::PTXOperand* operands[] = {&ptx.d, &ptx.a, &ptx.b, &ptx.c};

			if(ptx.opcode == ir::PTXInstruction::Mov
				|| ptx.opcode == ir::PTXInstruction::Ld
				|| ptx.opcode == ir::PTXInstruction::St)
			{
				for(unsigned int i = 0; i != 4; ++i) {
					if(operands[i]->addressMode == ir::PTXOperand::Address) {
						OffsetMap::iterator mapping = constants.find(operands[i]->identifier);
						
						if(constants.end() != mapping) {
						
							ptx.addressSpace = ir::PTXInstruction::Const;
							operands[i]->offset += mapping->second;
							reportE(REPORT_TRANSLATION_OPERATIONS, "   For instruction " 
								<< ptx.toString() 
								<< ", mapping constant label " << mapping->first 
								<< " to " << mapping->second);
						}
					}
				}
			}
		}
	}
}

/*!
	\brief
*/
static void setupTextureMemoryReferences(ir::PTXKernel& kernel, 
	DynamicMulticoreExecutive::Metadata* metadata, executive::Device* device) {
	
	typedef std::unordered_map<std::string, unsigned int> TextureMap;
	reportE(REPORT_TRANSLATION_OPERATIONS, " Setting up texture memory references.");
	
	TextureMap textures;
	
	for (ir::ControlFlowGraph::iterator block = kernel.cfg()->begin(); 
		block != kernel.cfg()->end(); ++block) {
		
		for (ir::ControlFlowGraph::InstructionList::iterator instruction = block->instructions.begin(); 
			instruction != block->instructions.end(); ++instruction) {
			
			ir::PTXInstruction& ptx = static_cast<ir::PTXInstruction&>(**instruction);
			
			if (ptx.opcode == ir::PTXInstruction::Tex) {
				reportE(REPORT_TRANSLATION_OPERATIONS, "  found texture instruction: " << ptx.toString());

				TextureMap::iterator reference = textures.find(ptx.a.identifier);
				if (reference != textures.end()) {
					ptx.a.reg = reference->second;
					reportE(REPORT_TRANSLATION_OPERATIONS, " recognized as as texture " << ptx.a.reg);
				}
				else {
					ptx.a.reg = textures.size();
					textures.insert(std::make_pair(ptx.a.identifier, textures.size()));
						
					ir::Texture* texture = (ir::Texture*)device->getTextureReference(
						kernel.module->path(), ptx.a.identifier);
					
					assert(texture != 0);
					
					metadata->textures.push_back(texture);
					reportE(REPORT_TRANSLATION_OPERATIONS, " adding as texture " << ptx.a.reg);
				}
				assert(metadata->textures.size());
			}
		}
	}
}

/*!
	\brief 
*/
static void setupLocalMemoryReferences(ir::PTXKernel& kernel,
	DynamicMulticoreExecutive::Metadata* metadata) {
	
	reportE(REPORT_TRANSLATION_OPERATIONS,  "  Setting up local memory references." );
	typedef std::unordered_map<std::string, unsigned int> OffsetMap;

	OffsetMap offsets;
	
	// Reserve the first few 32-bit words
	// [0] == kernel-id
	// [1] == call type
	// [2] == barrier resume point if it exists
	metadata->localSize = 0;
	
	// give preference to barrier resume point
	ir::Kernel::LocalMap::const_iterator local = kernel.locals.find(
		"_Zocelot_barrier_next_kernel");
	if(local != kernel.locals.end())
	{
		if(local->second.space == ir::PTXInstruction::Local)
		{
			reportE(REPORT_TRANSLATION_OPERATIONS, "   Found local local variable " 
				<< local->second.name << " of size " 
				<< local->second.getSize());
			
			pad(metadata->localSize, local->second.alignment);
			offsets.insert(std::make_pair(local->second.name,
				metadata->localSize));
			metadata->localSize += local->second.getSize();
		}
	}
	
	pad(metadata->localSize, sizeof(int));
	offsets.insert(std::make_pair("_Zocelot_resume_status", metadata->localSize));
	metadata->localSize += sizeof(int);

	pad(metadata->localSize, sizeof(int));
	offsets.insert(std::make_pair("_Zocelot_resume_point", metadata->localSize));
	metadata->localSize += sizeof(int);
	
	/*
	if (api::OcelotConfiguration::get().executive.yieldOverheadInstrumentation) {
		pad(metadata->localSize, sizeof(size_t));
		offsets.insert(std::make_pair("_Zocelot_entry_cycles", metadata->localSize));
		metadata->localSize += sizeof(size_t);
	
		pad(metadata->localSize, sizeof(int));
		offsets.insert(std::make_pair("_Zocelot_entry_id", metadata->localSize));
		metadata->localSize += sizeof(int);
	
		pad(metadata->localSize, sizeof(int));
		offsets.insert(std::make_pair("_Zocelot_entry_liveness", metadata->localSize));
		metadata->localSize += sizeof(int);
	
		pad(metadata->localSize, sizeof(size_t));
		offsets.insert(std::make_pair("_Zocelot_exit_cycles", metadata->localSize));
		metadata->localSize += sizeof(size_t);
	
		pad(metadata->localSize, sizeof(int));
		offsets.insert(std::make_pair("_Zocelot_exit_id", metadata->localSize));
		metadata->localSize += sizeof(int);
	
		pad(metadata->localSize, sizeof(int));
		offsets.insert(std::make_pair("_Zocelot_exit_liveness", metadata->localSize));
		metadata->localSize += sizeof(int);
		
		pad(metadata->localSize, sizeof(size_t));
		offsets.insert(std::make_pair("_Zocelot_subkernel_cycles", metadata->localSize));
		metadata->localSize += sizeof(size_t);
	}
	*/
	
	for(ir::Kernel::LocalMap::const_iterator local = kernel.locals.begin(); 
		local != kernel.locals.end(); ++local)
	{
		if(local->first == "_Zocelot_barrier_next_kernel") continue;
		if(local->first == "_Zocelot_spill_area")          continue;
		if(local->first == "_Zocelot_resume_point")        continue;
		if(local->first == "_Zocelot_resume_status")        continue;
		
		/*
		if (api::OcelotConfiguration::get().executive.yieldOverheadInstrumentation) {
			if(local->first == "_Zocelot_entry_cycles")        continue;
			if(local->first == "_Zocelot_entry_id")        continue;
			if(local->first == "_Zocelot_entry_liveness")        continue;
			if(local->first == "_Zocelot_exit_cycles")        continue;
			if(local->first == "_Zocelot_exit_id")        continue;
			if(local->first == "_Zocelot_exit_liveness")        continue;
			if (local->first == "_Zocelot_subkernel_cycles") continue;
		}
		*/
		
		if (local->second.space == ir::PTXInstruction::Local) {
			reportE(REPORT_TRANSLATION_OPERATIONS, "   Found local local variable " 
				<< local->second.name << " of size " 
				<< local->second.getSize());
			
			pad(metadata->localSize, local->second.alignment);
			offsets.insert(std::make_pair(local->second.name,
				metadata->localSize));
			metadata->localSize += local->second.getSize();
		}
	}

	// defer the spill area
	local = kernel.locals.find("_Zocelot_spill_area");
	if (local != kernel.locals.end()) {
		if (local->second.space == ir::PTXInstruction::Local) {
			reportE(REPORT_TRANSLATION_OPERATIONS, "   Found local local variable " 
				<< local->second.name << " of size " 
				<< local->second.getSize());
			
			pad(metadata->localSize, local->second.alignment);
			offsets.insert(std::make_pair(local->second.name,
				metadata->localSize));
			metadata->localSize += local->second.getSize();
		}
	}
    
	for (ir::ControlFlowGraph::iterator block = kernel.cfg()->begin(); 
		block != kernel.cfg()->end(); ++block) {
		
		for (ir::ControlFlowGraph::InstructionList::iterator instruction = block->instructions.begin(); 
			instruction != block->instructions.end(); ++instruction) {
			
			ir::PTXInstruction& ptx = static_cast<ir::PTXInstruction&>(**instruction);
			ir::PTXOperand* operands[] = {&ptx.d, &ptx.a, &ptx.b, &ptx.c};
	
			if (ptx.opcode == ir::PTXInstruction::Mov || ptx.opcode == ir::PTXInstruction::Ld
				|| ptx.opcode == ir::PTXInstruction::St) {
				
				for (unsigned int i = 0; i != 4; ++i) {
					if (operands[i]->addressMode == ir::PTXOperand::Address) {
						OffsetMap::iterator offset = offsets.find(operands[i]->identifier);
						if (offsets.end() != offset) {
							ptx.addressSpace = ir::PTXInstruction::Local;
							operands[i]->offset += offset->second;
							
							reportE(REPORT_TRANSLATION_OPERATIONS, "   For instruction " 
								<< ptx.toString() << ", mapping local label " 
								<< offset->first << " to " << offset->second);
						}
					}
				}
			}
		}
	}
	reportE(REPORT_TRANSLATION_OPERATIONS, "   Total local memory size is " 
		<< metadata->localSize << ".");
}

/*!

*/
static void setupPTXMemoryReferences(ir::PTXKernel& kernel,
	DynamicmMulticoreExecutive::Metadata* metadata, executive::Device* device) {
	
	reportE(REPORT_TRANSLATION_OPERATIONS, " Setting up memory references for kernel variables.");
	
	setupGlobalMemoryReferences(kernel);
	setupArgumentMemoryReferences(kernel, metadata);
	setupParameterMemoryReferences(kernel, metadata);
	setupSharedMemoryReferences(kernel, metadata);
	setupConstantMemoryReferences(kernel, metadata);
	setupTextureMemoryReferences(kernel, metadata, device);
	setupLocalMemoryReferences(kernel, metadata);
}

/*!
	\brief apply a set of optimizations and transformations to the PTX representation
		of the whole kernel
*/
static void optimizePTX(ir::PTXKernel& kernel, translator::Translator::OptimizationLevel optimization) {
	
	reportE(REPORT_TRANSLATION_OPERATIONS, " Building dataflow graph.");
	kernel.dfg();

	reportE(REPORT_TRANSLATION_OPERATIONS, " Optimizing PTX");
		
	analysis::ConvertPredicationToSelectPass convertPredicationToSelect;
	reportE(REPORT_TRANSLATION_OPERATIONS, "  Running convert predication to select pass");
	convertPredicationToSelect.initialize(*kernel.module);
	convertPredicationToSelect.runOnKernel(kernel);
	convertPredicationToSelect.finalize();

	kernel.dfg()->toSsa();
}

/*!
	\brief 
*/
static void setupCallTargets(ir::PTXKernel& kernel,
	const LLVMDynamicTranslationCache & translationCache) {
	
	// replace all call instruction operands with kernel id
	reportE(REPORT_TRANSLATION_OPERATIONS, "  Setting up targets of call instructions.");
	
	for (ir::ControlFlowGraph::iterator block = kernel.cfg()->begin(); 
		block != kernel.cfg()->end(); ++block) {
		
		for (ir::ControlFlowGraph::InstructionList::iterator instruction = block->instructions.begin(); 
			instruction != block->instructions.end(); ++instruction) {
			
			ir::PTXInstruction& ptx = static_cast<ir::PTXInstruction&>(**instruction);
			
			if (ptx.opcode != ir::PTXInstruction::Call && ptx.opcode != ir::PTXInstruction::Mov) {
				continue;
			}
			if (ptx.tailCall) {
				continue;
			}
			
			if(ptx.a.addressMode == ir::PTXOperand::FunctionName) {
				if (ptx.a.identifier == "ptx.warp.divergent") {
					// this is a special intrinsic that will be lowered
				}
				else {
					assert(0 && "arbitrary function calls not yet supported");
					reportE(REPORT_TRANSLATION_OPERATIONS, "   setting target '" << ptx.a.identifier 
						<< "' of instruction '" << ptx.toString());
//					ptx.reentryPoint = id;
				}
			}
		}
	}
}

static std::string getTranslatedName(const std::string &kernelName) {
	return "_Z_ocelotTranslated_" + kernelName;
}

/*!
	\brief 
*/
static llvm::Function *translatePTXtoLLVM(llvm::Module* module, ir::PTXKernel& kernel,
	translator::Translator::OptimizationLevel optimization) {
	

}

/*!
	\brief constructs a metadata instance
*/
static LLVMDynamicExecutive::Metadata* generateMetadata(
	ir::PTXKernel& kernel, 
	translator::Translator::OptimizationLevel level,
	int warpSize = 1)
{
	LLVMDynamicExecutive::Metadata *metadata = new LLVMDynamicExecutive::Metadata;
	reportE(REPORT_TRANSLATION_OPERATIONS, " Building metadata.");
	
	if(level == translator::Translator::DebugOptimization
		|| level == translator::Translator::ReportOptimization) {
						
		assert(0 && "unimplemented");
	}
	
	metadata->kernel = &kernel;
	metadata->warpSize = warpSize;
	
	return metadata;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////

/*!
	\brief given a translated function, apply a selection of LLVM transformation
		passes before JIT compilation
	
	\param module
	\param optimization
	\param warpSize number of threads executed per function call
*/
static void cloneAndOptimizeTranslation(
	executive::DynamicTranslationCache::TranslatedKernel &translatedKernel,
	executive::DynamicTranslationCache::Translation *translation,
	translator::Translator::OptimizationLevel optimization,
	int warpSize) {
	
	reportE(REPORT_TRANSLATION_OPERATIONS, " Optimizing kernel at level " 
		<< translator::Translator::toString(optimization));

	unsigned int level = 0;
	bool space = false;

	switch (optimization) {
		case translator::Translator::BasicOptimization:
			level = 1;
			break;
		case translator::Translator::AggressiveOptimization:
			level = 2;
			break;
		case translator::Translator::SpaceOptimization:
			level = 2;
			space = true;
			break;
		case translator::Translator::FullOptimization:
			level = 3;
			break;
		default:
			level = 0;
			break;
	}


	std::stringstream ss;
	ss << translatedKernel.scalarTranslation->getNameStr() << "_opt" << level << "_ws" << warpSize;

	translation->llvmFunction = llvm::CloneFunction(translatedKernel.llvmFunction);
	translation->llvmFunction->setName(ss.str());
	translation->llvmFunction->setLinkage(llvm::GlobalValue::InternalLinkage);
	
	translatedKernel.llvmModule->getFunctionList().push_back(translation->llvmFunction);
		
	llvm::FunctionPassManager manager(translatedKernel.llvmModule);
	manager.add(new llvm::TargetData(*LLVMState::jit()->getTargetData()));
	
	level = 1;

	if (level == 0) {
		reportE(REPORT_TRANSLATION_OPERATIONS, "no optimizations");
	}
	else if (level == 1) {
		manager.add(llvm::createInstructionCombiningPass());
		manager.add(llvm::createReassociatePass());
		manager.add(llvm::createGVNPass());
		manager.add(llvm::createCFGSimplificationPass());
	}
	else {
		manager.add(llvm::createSimplifyLibCallsPass());
		manager.add(llvm::createInstructionCombiningPass());
		manager.add(llvm::createJumpThreadingPass());
		manager.add(llvm::createCFGSimplificationPass());
		manager.add(llvm::createScalarReplAggregatesPass());
		manager.add(llvm::createInstructionCombiningPass());
		manager.add(llvm::createTailCallEliminationPass());
		manager.add(llvm::createCFGSimplificationPass());
		manager.add(llvm::createReassociatePass());
		manager.add(llvm::createLoopRotatePass());
		manager.add(llvm::createLICMPass());
		manager.add(llvm::createLoopUnswitchPass(space || level < 3));
		manager.add(llvm::createInstructionCombiningPass());
		manager.add(llvm::createIndVarSimplifyPass());
		manager.add(llvm::createLoopDeletionPass());
		if (level > 2) {
			manager.add(llvm::createLoopUnrollPass());
		}
		manager.add(llvm::createInstructionCombiningPass());
		manager.add(llvm::createGVNPass());
		manager.add(llvm::createMemCpyOptPass());
		manager.add(llvm::createSCCPPass());

		// Run instcombine after redundancy elimination to exploit opportunities
		// opened up by them.
		manager.add(llvm::createInstructionCombiningPass());
		manager.add(llvm::createDeadStoreEliminationPass());
		manager.add(llvm::createAggressiveDCEPass());
		manager.add(llvm::createCFGSimplificationPass());
	}
	
	manager.run(*translation->llvmFunction);
	
	// we can't verify errors until this point
	reportE(REPORT_TRANSLATION_OPERATIONS, "  Checking llvm module for errors.");
	std::string verifyError;
	
	if (llvm::verifyModule(*translatedKernel.llvmModule, llvm::ReturnStatusAction, &verifyError)) {
	
		std::cerr << "verification failed for kernel " << translatedKernel.kernel->name << " : \"" 
			<< verifyError << "\"" << std::endl;
			
#if REPORT_BASE && REPORT_LLVM_VERIFY_FAILURE
		std::cerr << "LLVMDynamicTranslationCache.cpp:" << __LINE__ << ":" << std::endl;
		translatedKernel.kernelModule->dump();
#endif

		assert(0 && "quitting");
		
		delete translatedKernel.llvmModule;
		translatedKernel.llvmModule = 0;
				
		throw hydrazine::Exception("LLVM Verifier failed for kernel: " 
			+ translatedKernel.kernel->name + " : \"" + verifyError + "\"");
	}
	else {
		reportE(REPORT_TRANSLATION_OPERATIONS, " verified module");
	}

	report("performed transformations");
}

static void linkLLVMModule(llvm::Module& module, const ir::PTXKernel& kernel, Device* device) {

	reportE(REPORT_TRANSLATION_OPERATIONS, "  Linking global variables.");
	
	for(ir::Module::GlobalMap::const_iterator global = kernel.module->globals().begin(); 
		global != kernel.module->globals().end(); ++global) {
		
		if(global->second.statement.directive == ir::PTXStatement::Global) {
			assert(device != 0);

			llvm::GlobalValue* value = module.getNamedValue(global->first);
			
			assertM(value != 0, "Global variable " << global->first 
				<< " not found in llvm module.");
			Device::MemoryAllocation* allocation = device->getGlobalAllocation(kernel.module->path(), 
				global->first);
			
			assert(allocation != 0);
			reportE(REPORT_TRANSLATION_OPERATIONS, "  Binding global variable " << global->first 
				<< " to " << allocation->pointer());
			
			LLVMState::jit()->addGlobalMapping(value, allocation->pointer());
		}
	}
}

#endif

void executive::DynamicTranslationCache::_translateKernel(TranslatedKernel &translatedKernel) {
	
	report("__translateKernel()");
	
#ifdef HAVE_LLVM
	reportE(REPORT_TRANSLATION_OPERATIONS, "Getting metadata for kernel '" 
		<< translatedKernel.kernel->name << "' subkernel " << subkernelId);
	
	KernelGraph::SubkernelMap &subkernels = translatedKernel.kernel->kernelGraph()->subkernels;

	for (KernelGraph::SubkernelMap::iterator subkernel_it = subkernels.begin(); 
		subkernel_it != subkernels.end(); ++subkernel_it) {
	
		subkernelsToKernel[subkernel_it->first] = &translatedKernel;
	
		ir::PTXKernel *subkernelPtx = subkernel_it->second.subkernel;
	
		Metadata *metadata = 0;
	
		// apply PTX optimizations and transformations needed to support the dynamic translation cache
		optimizePTX(*subkernelPtx, optimization);
		
		try {
	
			// compte memory sizes and layouts
			metadata = generateMetadata(*subkernelPtx, optimization);
		
			// translate global memory references
			setupPTXMemoryReferences(*subkernelPtx, metadata, device);

			// rewrite call functions with hyperblock exits chained to target functions
			setupCallTargets(*subkernelPtx, *this);
	
			translator::PTXToLLVMTranslator translator(optimization);

			ir::LLVMKernel* llvmKernel = static_cast<ir::LLVMKernel*>(translator.translate(&kernel));

			reportE(REPORT_TRANSLATION_OPERATIONS, "  Assembling LLVM kernel.");
			llvmKernel->assemble();

			llvm::SMDiagnostic error;

		#if REPORT_LLVM_MASTER
			report("translated PTX to LLVM");
			reportE(REPORT_SOURCE_LLVM_ASSEMBLY, llvmKernel->code());
		#endif

			reportE(REPORT_TRANSLATION_OPERATIONS, "  Parsing LLVM assembly.");
			llvm::Module *newModule = llvm::ParseAssemblyString(llvmAssembly.str().c_str(), 
				translatedKernel.llvmModule, error, llvm::getGlobalContext());

			if (newModule == 0) {
				reportE(REPORT_TRANSLATION_OPERATIONS, "   Parsing kernel failed, dumping code:\n" 
					<< llvmKernel->numberedCode());
	
				std::string m;
				llvm::raw_string_ostream message(m);
				message << "LLVM Parser failed: ";

				error.Print(kernel.name.c_str(), message);
				kernel.dfg()->fromSsa();

				throw hydrazine::Exception(message.str());
			}
			else {
				reportE(REPORT_TRANSLATION_OPERATIONS, " parsed kernel");
			}

			delete llvmKernel;
			
			TranslatedSubkernel newSubkernel;
			newSubkernel.llvmFunction = module->getFunction(getTranslatedName(kernel.name));
			newSubkernel.metadata = metadata;
			translatedKernel.subkernels[subkernelId] = newSubkernel;
		
			// Converting out of ssa makes the assembly easier to read
			if(optimization == translator::Translator::ReportOptimization 
				|| optimization == translator::Translator::DebugOptimization) {
			
				subkernelPtx->dfg()->fromSsa();
			}
			translatedKernel.metadata = metadata;
		}
		catch(...)
		{
			delete metadata;
			metadata = 0;
			throw;
		}
	
		#else
		assertM(false, "LLVM support not compiled into ocelot.");
		#endif
	}
}

executive::DynamicTranslationCache::Translation *
	executive::DynamicTranslationCache::_specializeTranslation(
		executive::DynamicTranslationCache::TranslatedKernel &translatedKernel, SubkernelId subkernelId, 
		OptimizationLevel optimizationLevel, int warpSize, unsigned int specialization) {
	
	report("_specializeTranslation()");
	
	TranslatedSubkernel &subkernel = translatedKernel.subkernels[subkernelId];
	
	Translation *translation = new Translation;
	
	translation->metadata = subkernel.metadata;
	
	#ifdef HAVE_LLVM
	try {
	
		report("  cloning and optimizing");
	
		// apply optimizations on the resulting LLVM function
		cloneAndOptimizeTranslation(translatedKernel, translation, optimization, translation->warpSize);
		
		// dynamically compile LLVM to host ISA
		reportE(REPORT_TRANSLATION_OPERATIONS, " Generating native code.");
	
	
		report("  JIT compiling");
		
		LLVMState::jit()->addModule(translatedKernel.llvmModule);
		linkLLVMModule(*translatedKernel.llvmModule, *subkernel.subkernel, device);
		
		reportE(REPORT_TRANSLATION_OPERATIONS, "  Invoking LLVM to Native JIT");
		assertM(translation->llvmFunction != 0, "Could not find function ");
	
		translation->function = hydrazine::bit_cast<DynamicTranslationCache::TranslatedFunction>(
			LLVMState::jit()->getPointerToFunction(translation->llvmFunction));
		
		report("  verifying");
		
		// this step may be ellided for performance
		std::string errors;
		if (llvm::verifyModule(*translatedKernel.llvmModule, llvm::ReturnStatusAction, &errors)) {
			std::cerr << "llvm::verifyModule failed:" << errors << std::endl;
		}
		
		report("  updating translation cache data structures");
		subkernel.translations[warpSize] = translation;
		
		// update the translation cache
		translationCache[subkernelid][warpSize] = translation;
	}
	catch(...) {
		LLVMState::jit()->removeModule(translatedKernel.llvmModule);
		delete translation;
		throw;
	}
	return translation;
}


