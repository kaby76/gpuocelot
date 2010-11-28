/*! \file ClockCycleCountInstrumentor.cpp
	\date Monday November 15, 2010
	\author Naila Farooqui <naila@cc.gatech.edu>
	\brief The source file for the ClockCycleCountInstrumentor class.
*/

#ifndef KERNEL_CLOCK_SM_INSTRUMENTOR_CPP_INCLUDED
#define KERNEL_CLOCK_SM_INSTRUMENTOR_CPP_INCLUDED

#include <ocelot/analysis/interface/ClockCycleCountInstrumentor.h>

#include <ocelot/cuda/interface/cuda_runtime.h>

#include <ocelot/analysis/interface/ClockCycleCountPass.h>
#include <ocelot/ir/interface/Module.h>

#include <hydrazine/implementation/ArgumentParser.h>
#include <hydrazine/implementation/string.h>
#include <hydrazine/implementation/debug.h>
#include <hydrazine/implementation/Exception.h>
//#include <hydrazine/interface/Test.h>
#include <hydrazine/implementation/json.h>

#include <fstream>
#include <string>
#include <iostream>
#include <sstream>

using namespace hydrazine;

namespace analysis
{
	void ClockCycleCountInstrumentor::jsonEmitter(size_t* info)
	{
		json::Object *clockCyclesStat = new json::Object;
		json::Object *column;
	
		column = new json::Object;
		json::Number *valueCC;
		clockCyclesStat->dictionary["per_CTA_per_SM_clock_cycle_count"] = column;
		
		for(unsigned int i = 0; i< threadBlocks; i++)
		{
			std::stringstream thread;
			thread << i << "." << info[i*2+1];
			valueCC = new json::Number((int)info[i*2+0]);
			column->dictionary[thread.str()] = valueCC;
		}
	
		//start emitter
		std::ofstream outFile;

		std::string tmpStr = ".clockCycleCount";
		int i=1;
		bool alreadyExists;
		do {
			outFile.open((kernelName + tmpStr + ".json").c_str(), std::ios::in);	
			if( outFile.is_open() )
			{
				alreadyExists=true;
				std::stringstream out;
				out << ".clockCycleCount." << i;
				tmpStr = out.str();
				i++;
			} else {
				alreadyExists=false;
			}
			outFile.close();
		} while(alreadyExists);
	
		outFile.open((kernelName + tmpStr + ".json").c_str());
		json::Emitter emitter;
		emitter.use_tabs = false;
		emitter.emit_pretty(outFile, clockCyclesStat, 2);
	
		return;
	}

    void ClockCycleCountInstrumentor::analyze(ir::Module &module) {
        /* No static analysis necessary for this instrumentation */
    }

    void ClockCycleCountInstrumentor::initialize() {
              
        clock_sm_info = 0;

        cudaMalloc((void **) &clock_sm_info, 2 * threadBlocks * sizeof(size_t));
        cudaMemset( clock_sm_info, 0, 2 * threadBlocks * sizeof( size_t ));

        cudaMemcpyToSymbol(((ClockCycleCountPass *)pass)->kernelClockSMInfo().c_str(), &clock_sm_info, sizeof(size_t *), 0, cudaMemcpyHostToDevice);
    }

    analysis::Pass *ClockCycleCountInstrumentor::createPass() {
        return new analysis::ClockCycleCountPass();
    }

    void ClockCycleCountInstrumentor::finalize() {
                
        size_t *clockSMInfoHost = new size_t[2 * threadBlocks];
        cudaMemcpy(clockSMInfoHost, clock_sm_info, 2 * threadBlocks * sizeof( size_t ), cudaMemcpyDeviceToHost);      
    
        setup();

        *out << "\n\n" << kernelName << ":\n";
        *out << "\n--------------- " << description << " ---------------\n\n";

        for(unsigned int i = 0; i < threadBlocks; i++) {
            *out << "CTA " << i << ":\n";
            *out << "Clock Cycles: " << clockSMInfoHost[i*2] << std::endl;
            *out << "SM (Processor) ID: " << clockSMInfoHost[i*2 + 1] << std::endl;
        }

        cleanup();  
          
		if(enableJSON)
		{
			jsonEmitter(clockSMInfoHost);
		} 
		
        delete[] clockSMInfoHost;
        cudaFree(clock_sm_info);
    }

    ClockCycleCountInstrumentor::ClockCycleCountInstrumentor() : description("Clock Cycles and SM (Processor) ID") {

    }

}

#endif