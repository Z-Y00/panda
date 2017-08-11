
/* PANDABEGINCOMMENT
 *
 * Authors:
 *  Ray Wang        raywang@mit.edu
 *
 * This work is licensed under the terms of the GNU GPL, version 2.
 * See the COPYING file in the top-level directory.
 *
PANDAENDCOMMENT */


/*
 * This plugin creates an LLVM trace from a panda replay
 * With dynamic values inlined into the trace. 
 *
 * The C struct is defined in llvm_trace2.proto
 *
 */

#include <vector>
#include <set>
#include <string>

#include "llvm_trace2.h"
#include "Extras.h"

extern "C" {

#include "panda/plugin.h"
#include "panda/tcg-llvm.h"
#include "panda/plugin_plugin.h"
}

#include <iostream>

#include <llvm/PassManager.h>
#include <llvm/PassRegistry.h>
#include <llvm/IR/Intrinsics.h>
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Instruction.h>

#include <llvm/Analysis/Verifier.h>
#include <llvm/ExecutionEngine/ExecutionEngine.h>
#include <llvm/Transforms/IPO/PassManagerBuilder.h>
#include "llvm/ExecutionEngine/GenericValue.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Regex.h"


namespace llvm {


PandaLLVMTracePass *PLTP; 
  // Integer types
  // Removed const modifier since method signatures have changed
  Type *Int8Type;
  Type *Int32Type;
  Type *Int64Type;
  Type *VoidType;
  Type *VoidPtrType;

//void PandaLLVMTraceVisitor::visitPhiInst(){

//}
void recordStartBB(uint64_t fp, uint64_t tb_num){
	
    //giri doesn't write to log, instead pushes onto a bb stack. 
	if (pandalog) {
		Panda__LLVMEntry *llvmentry = (Panda__LLVMEntry *)(malloc(sizeof(Panda__LLVMEntry)));
		*llvmentry = PANDA__LLVMENTRY__INIT;
		llvmentry->has_type = 1;
		llvmentry->type = FunctionCode::BB;
		if (tb_num > 0){
			llvmentry->has_tb_num = 1;
			llvmentry->tb_num = tb_num;
		}
		Panda__LogEntry logEntry = PANDA__LOG_ENTRY__INIT;
		logEntry.llvmentry = llvmentry;
		pandalog_write_entry(&logEntry);
	}
}

void recordCall(uint64_t fp){

	if (pandalog) {
		Panda__LLVMEntry *llvmentry = (Panda__LLVMEntry *)(malloc(sizeof(Panda__LLVMEntry)));
		*llvmentry = PANDA__LLVMENTRY__INIT;
		llvmentry->has_type = 1;
		llvmentry->type = FunctionCode::FUNC_CODE_INST_CALL;
		llvmentry->has_address = 1;
		llvmentry->address = fp;
        Panda__LogEntry logEntry = PANDA__LOG_ENTRY__INIT;
		logEntry.llvmentry = llvmentry;
		pandalog_write_entry(&logEntry);
	}
}
void recordBB(uint64_t fp, unsigned lastBB){
	
	if (pandalog) {
		Panda__LLVMEntry *llvmentry = (Panda__LLVMEntry *)(malloc(sizeof(Panda__LLVMEntry)));
	*llvmentry = PANDA__LLVMENTRY__INIT;
		
		llvmentry->has_type = 1;
		llvmentry->type = FunctionCode::BB;
        Panda__LogEntry logEntry = PANDA__LOG_ENTRY__INIT;
		logEntry.llvmentry = llvmentry;
		pandalog_write_entry(&logEntry);
	}
}

void recordLoad(uint64_t address, uint64_t num_bytes = 8){
	//printf("recording load at address %" PRIx64 "\n", address);

	//printf("recording load from	address %lx\n", address);

	if (pandalog) {
		Panda__LLVMEntry *llvmentry = (Panda__LLVMEntry *)(malloc(sizeof(Panda__LLVMEntry)));
		*llvmentry = PANDA__LLVMENTRY__INIT;
		llvmentry->has_type = 1;
		llvmentry->type = FunctionCode::FUNC_CODE_INST_LOAD;
		
		llvmentry->has_addr_type = 1;	
		if ((address >= (uint64_t)first_cpu) && (address < (uint64_t)first_cpu + sizeof(CPUState))){
			llvmentry->addr_type = REG;	// Something in CPU state, may not necessarily be a register
			//TODO: Fix this and store
		} else {
			llvmentry->addr_type = MEM;	// A memory address
		}

		llvmentry->has_address = 1;
		llvmentry->address = address;
		llvmentry->has_num_bytes = 1;
		llvmentry->num_bytes = num_bytes;
        Panda__LogEntry logEntry = PANDA__LOG_ENTRY__INIT;
		logEntry.llvmentry = llvmentry;
		pandalog_write_entry(&logEntry);
	}
}

void recordStore(uint64_t address, uint64_t num_bytes = 8){

	//printf("recording store to address %lx\n", address);
	
	if (pandalog) {
		Panda__LLVMEntry *llvmentry = (Panda__LLVMEntry *)(malloc(sizeof(Panda__LLVMEntry)));
		*llvmentry = PANDA__LLVMENTRY__INIT;
		llvmentry->has_type = 1;
		llvmentry->type = FunctionCode::FUNC_CODE_INST_STORE;

		llvmentry->has_addr_type = 1;	
		if ((address >= (uint64_t)first_cpu) && (address < (uint64_t)first_cpu + sizeof(CPUState))){
			//printf("store to cpu state\n");
			llvmentry->addr_type = REG;	// Something in CPU state
		}else {
			llvmentry->addr_type = MEM;	// A memory address
		}

		llvmentry->has_address = 1;
		llvmentry->address = address;
		llvmentry->has_num_bytes = 1;
		llvmentry->num_bytes = num_bytes;
        Panda__LogEntry logEntry = PANDA__LOG_ENTRY__INIT;
		logEntry.llvmentry = llvmentry;
		pandalog_write_entry(&logEntry);
	}
}


void recordReturn(){
	if (pandalog) {
		Panda__LLVMEntry *llvmentry = (Panda__LLVMEntry *)(malloc(sizeof(Panda__LLVMEntry)));
		*llvmentry = PANDA__LLVMENTRY__INIT;
		llvmentry->has_type = 1;
		llvmentry->type = FunctionCode::FUNC_CODE_INST_RET;
        Panda__LogEntry logEntry = PANDA__LOG_ENTRY__INIT;
		logEntry.llvmentry = llvmentry;
		pandalog_write_entry(&logEntry);
	}
}

void recordSelect(uint8_t condition){

	if (pandalog) {
		Panda__LLVMEntry *llvmentry = (Panda__LLVMEntry *)(malloc(sizeof(Panda__LLVMEntry)));
		*llvmentry = PANDA__LLVMENTRY__INIT;
		llvmentry->has_type = 1;
		llvmentry->type = FunctionCode::FUNC_CODE_INST_SELECT;
		llvmentry->has_condition = 1;
		llvmentry->condition = condition;
        Panda__LogEntry logEntry = PANDA__LOG_ENTRY__INIT;
		logEntry.llvmentry = llvmentry;
		pandalog_write_entry(&logEntry);
	}
}

void write_trace_log(){
	printf("writing trace log\n");
}

//this function is inlined into LLVM assembly, writes dynamic values to trace log in protobuf format
void log_dynval(){
    write_trace_log();

}


extern "C" { extern TCGLLVMContext *tcg_llvm_ctx; }

static void llvm_init(){

	printf("LLVM_init\n");
	Function *logFunc;
    ExecutionEngine *execEngine = tcg_llvm_ctx->getExecutionEngine();
    FunctionPassManager *passMngr = tcg_llvm_ctx->getFunctionPassManager();
    Module *mod = tcg_llvm_ctx->getModule();
    LLVMContext &ctx = mod->getContext();
	
	std::vector<Type*> argTypes;

	//1st arg, LLVM Instr opcode
	argTypes.push_back(IntegerType::get(ctx, 8*sizeof(unsigned)));
	
	// 2nd arg, num_args
	argTypes.push_back(IntegerType::get(ctx, 8*sizeof(unsigned)));

	// 3rd arg, 
	//argTypes.push_back(Integer

	FunctionType *fType = FunctionType::get(Type::getVoidTy(ctx), argTypes, false);
	logFunc = Function::Create(
		fType, 
		Function::ExternalLinkage, "log_dynval", mod
	);

	execEngine->addGlobalMapping(logFunc, (void*) &log_dynval);

    // Create instrumentation pass and add to function pass manager
    /*llvm::FunctionPass *instfp = new PandaLLVMTracePass(mod);*/
    /*fpm->add(instfp);*/
    /*PIFP = static_cast<PandaInstrFunctionPass*>(instfp);*/

	//TODO: Stick this somewhere
    // Add the taint analysis pass to our taint pass manager
   	PLTP = new llvm::PandaLLVMTracePass(mod);
    passMngr->add(PLTP);

	printf("before passmngr initialization\n");
    passMngr->doInitialization();
	printf("after passmgr initialization\n");
}


void instrumentBasicBlock(BasicBlock &BB){
    Module *module = tcg_llvm_ctx->getModule();
	Value *FP = castTo(BB.getParent(), VoidPtrType, "", BB.getTerminator());
	Value *tb_num_val;
	if (BB.getParent()->getName().startswith("tcg-llvm-tb-")) {
		int tb_num; 
		sscanf(BB.getParent()->getName().str().c_str(), "tcg-llvm-tb-%d-%*d", &tb_num); 
		tb_num_val = ConstantInt::get(Int64Type, tb_num);
	} else {
		tb_num_val = ConstantInt::get(Int64Type, 0);
	}	

	//Function *recordBBF = module->getFunction("recordBB");
	Function *recordStartBBF = module->getFunction("recordStartBB");

	//Value *lastBB;
	//if (isa<ReturnInst>(BB.getTerminator()))
		 //lastBB = ConstantInt::get(Int32Type, 1);
	//else
		 //lastBB = ConstantInt::get(Int32Type, 0);
	
    //std::vector<Value*> args = make_vector<Value*>(FP, lastBB, 0);
	//CallInst::Create(recordBBF, args, "", BB.getTerminator());

	// Insert code at the beginning of the basic block to record that it started
	// execution.

    std::vector<Value*>	args = make_vector<Value *>(FP, tb_num_val, 0);
	Instruction *F = BB.getFirstInsertionPt();
	CallInst::Create(recordStartBBF, args, "", F);
}

char PandaLLVMTracePass::ID = 0;
static RegisterPass<PandaLLVMTracePass>
Y("PandaLLVMTrace", "Instrument instructions that produce dynamic values");

bool PandaLLVMTracePass::runOnBasicBlock(BasicBlock &B){
	//TODO: Iterate over function instrs
	instrumentBasicBlock(B);
	PLTV->visit(B);
	return true; // modified program
};

//What initialization is necessary? 
bool PandaLLVMTracePass::doInitialization(Module &module){
	printf("Doing pandallvmtracepass initialization\n");
    ExecutionEngine *execEngine = tcg_llvm_ctx->getExecutionEngine();
	  // Get references to the different types that we'll need.
  Int8Type  = IntegerType::getInt8Ty(module.getContext());
  Int32Type = IntegerType::getInt32Ty(module.getContext());
  Int64Type = IntegerType::getInt64Ty(module.getContext());
  VoidPtrType = PointerType::getUnqual(Int8Type);
  VoidType = Type::getVoidTy(module.getContext());

  // Insert code at the beginning of the basic block to record that it started
  // execution.
  //std::vector<Value*> args = make_vector<Value *>();
  //Instruction *F = BB.getFirstInsertionPt();
  //Instruction *S = CallInst::Create(recordStartBBF, args, "", F);


	//initialize all the other record/logging functionsu
	//PLTV->log_dynvalF = module.getOrInsertFunction("log_dynval");
	PLTV->recordLoadF = cast<Function>(module.getOrInsertFunction("recordLoad", VoidType, VoidPtrType, nullptr));
	PLTV->recordStoreF = cast<Function>(module.getOrInsertFunction("recordStore", VoidType, VoidPtrType, nullptr));
	PLTV->recordCallF = cast<Function>(module.getOrInsertFunction("recordCall", VoidType, VoidPtrType, nullptr));
	PLTV->recordSelectF = cast<Function>(module.getOrInsertFunction("recordSelect", VoidType, Int8Type, nullptr));
	/*PLTV->recordBranchF = cast<Function>(module.getOrInsertFunction("recordBranch", VoidType, VoidPtrType, nullptr));*/
	// recordStartBB: 
	PLTV->recordStartBBF = cast<Function>(module.getOrInsertFunction("recordStartBB", VoidType, VoidPtrType, Int64Type, nullptr));
	PLTV->recordBBF = cast<Function>(module.getOrInsertFunction("recordBB", VoidType, VoidPtrType, Int32Type, nullptr));
	PLTV->recordReturnF = cast<Function>(module.getOrInsertFunction("recordReturn", VoidType, VoidPtrType, nullptr));


	//add external linkages
	
#define ADD_MAPPING(func) \
    execEngine->addGlobalMapping(module.getFunction(#func), (void *)(func));\
    module.getFunction(#func)->deleteBody();
    ADD_MAPPING(recordLoad);
    ADD_MAPPING(recordStore);
    ADD_MAPPING(recordCall);
    ADD_MAPPING(recordSelect);
    ADD_MAPPING(recordStartBB);
    ADD_MAPPING(recordBB);
    ADD_MAPPING(recordReturn);
	return true; //modified program
};


// Unhandled
void PandaLLVMTraceVisitor::visitInstruction(Instruction &I) {
    //I.dump();
    //printf("Error: Unhandled instruction\n");
    /*assert(1==0);*/
}

//TODO: Do i need to check metadata to see if host instruction?

void PandaLLVMTraceVisitor::visitSelectInst(SelectInst &I){
	//Function *func = module->getFunction("log_dynval");

	//std::vector<GenericValue> noargs;

	// Get the condition and record it
	Value *cond = I.getCondition();
	cond = castTo(cond, Int8Type, cond->getName(), &I);
	std::vector<Value*> args = make_vector(cond, 0);

    CallInst *CI = CallInst::Create(recordSelectF, args);
	
	//insert call into function
	CI->insertAfter(static_cast<Instruction*>(&I));
	
	/*I.dump();*/
}

void PandaLLVMTraceVisitor::visitLoadInst(LoadInst &I){
	//Function *func = module->getFunction("log_dynval");

	//std::vector<GenericValue> noargs;

	//Get the address we're loading from 
	// and cast to void ptr type
	Value *ptr = I.getPointerOperand();
	ptr = castTo(ptr, VoidPtrType, ptr->getName(), &I);
	std::vector<Value*> args = make_vector(ptr, 0);

    CallInst *CI = CallInst::Create(recordLoadF, args);
	
	//insert call into function
	CI->insertAfter(static_cast<Instruction*>(&I));
	
	/*I.dump();*/
}

/**
 * This function handles intrinsics like llvm.memset and llvm.memcpy. 
 * llvm.memset is recorded as a Store, and llvm.memcpy is recorded as a Load from src and Store to dest
 */
void PandaLLVMTraceVisitor::handleVisitSpecialCall(CallInst &I){
	
	Function *calledFunc = I.getCalledFunction();

	std::string name = calledFunc->getName().str();
	printf("func name %s\n", name.c_str());

	if (name.substr(0,12) == "llvm.memset.") {
		// Record store
		
		Value *dest = I.getOperand(0);
		dest = castTo(dest, VoidPtrType, dest->getName(), &I);
		
		Value* numBytes = I.getOperand(2);

		int bytes = 0;
		if (ConstantInt* CI = dyn_cast<ConstantInt>(numBytes)) {
			if (CI->getBitWidth() <= 64) {
				bytes = CI->getSExtValue();
			}
		}

		if (bytes > 100) {
			//This mostly happens in cpu state reset
			printf("Note: dyn log ignoring memset greater than 100 bytes\n");
			return;
		}

		/*std::vector<Value*> args = make_vector(dest, numBytes, 0);*/
		std::vector<Value*> args = make_vector(dest, 0);
		CallInst *CI = CallInst::Create(recordStoreF, args);
		
		//insert call into function
		CI->insertAfter(static_cast<Instruction*>(&I));
		
	} else if (name.substr(0,12) == "llvm.memcpy." ||
             name.substr(0,13) == "llvm.memmove.")  {

		Value *dest = I.getOperand(0);
		Value *src = I.getOperand(1);

		/*Value *numBytes = I.getOperand(2);*/

		dest = castTo(dest, VoidPtrType, dest->getName(), &I);
		src = castTo(src, VoidPtrType, src->getName(), &I);
		/*std::vector<Value*> args = make_vector(src, numBytes, 0);*/
		std::vector<Value*> args = make_vector(src, 0);

		//record load first
		CallInst *CI = CallInst::Create(recordLoadF, args);
		CI->insertAfter(static_cast<Instruction*>(&I));

		/*args = make_vector(dest, numBytes, 0);*/
		args = make_vector(dest, 0);
		CI = CallInst::Create(recordStoreF, args);
		CI->insertAfter(static_cast<Instruction*>(&I));
		
	} else {
		printf("Unhandled special call\n");
	}
}

void PandaLLVMTraceVisitor::handleExternalHelperCall(CallInst &I) {
    
	Function *calledFunc = I.getCalledFunction();

	std::string name = calledFunc->getName().str();
	printf("func name %s\n", name.c_str());

    std::vector<Value*> args;
	if (Regex("helper_[lb]e_ld.*_mmu_panda").match(name)) {
		// Helper load, record load

        Value *ITPI;
        
        // THis should be address
       Value *dest = I.getArgOperand(1);
		dest->dump();

        ITPI = CastInst::Create(Instruction::IntToPtr, dest, VoidPtrType, dest->getName(), &I);

       /*dest = castTo(dest, VoidPtrType, dest->getName(), &I);*/
		args = make_vector(ITPI, 0);
		CallInst *CI = CallInst::Create(recordLoadF, args);
		CI->insertBefore(static_cast<Instruction*>(&I));
    } else if (Regex("helper_[lb]e_st.*_mmu_panda").match(name)) {
        // THis should be address
        Value *ITPI;
        Value *ITPI2;

       Value *dest = I.getArgOperand(1);
       Value *storeVal = I.getArgOperand(2);

       //dest = castTo(dest, VoidPtrType, dest->getName(), &I);
        ITPI = CastInst::Create(Instruction::IntToPtr, dest, VoidPtrType, dest->getName(), &I);
       //storeVal = castTo(storeVal, VoidPtrType, storeVal->getName(), &I);
        //ITPI2 = IRB.CreateIntToPtr(storeVal, VoidPtrType);
        ITPI2 = CastInst::Create(Instruction::IntToPtr, storeVal, VoidPtrType, storeVal->getName(), &I);

		args = make_vector(ITPI, 0);
		CallInst *CI = CallInst::Create(recordStoreF, args);
		CI->insertBefore(static_cast<Instruction*>(&I));
    } else {
        printf("Unhandled external helper call\n");
    }

}

void PandaLLVMTraceVisitor::visitCallInst(CallInst &I){
	//Function *func = module->getFunction("log_dynval");

	Function *calledFunc = I.getCalledFunction();

	if (!calledFunc || !calledFunc->hasName()) { return; }
	 

	Value *fp;
	if (calledFunc->isIntrinsic() && calledFunc->isDeclaration()){
		//this is like a memset or memcpy
		handleVisitSpecialCall(I);
		return;
	}
	else if (external_helper_funcs.count(calledFunc->getName())) {
		// model the MMU load/store functions with regular loads/stores from dmemory
       handleExternalHelperCall(I);
        return;
	}

	std::string name = calledFunc->getName().str();
	fp = castTo(I.getCalledValue(), VoidPtrType, "", &I);

	//TODO: Should i do something about helper functions?? 
	
	std::vector<Value*> args = make_vector(fp, 0);

    CallInst *CI = CallInst::Create(recordCallF, args);

	CI->insertAfter(static_cast<Instruction*>(&I));

	//record return of call inst
	//CallInst *returnInst = CallInst::Create(recordReturn, args, "", &I);
	//I.dump();	

	//handle cases where dynamic values are being used. 
}

void PandaLLVMTraceVisitor::visitStoreInst(StoreInst &I){
	//Function *func = module->getFunction("log_dynval");

    if (I.isVolatile()){
        // Stores to LLVM runtime that we don't care about
        return;
    }

	Value *address = castTo(I.getPointerOperand(), VoidPtrType, "", &I);	

	std::vector<Value*> args = make_vector(address, 0);

    CallInst *CI = CallInst::Create(recordStoreF, args);

	CI->insertAfter(static_cast<Instruction*>(&I));
	//I.dump();	

	//handle cases where dynamic values are being used. 
}


} // namespace llvm
// I'll need a pass manager to traverse stuff. 

int before_block_exec(CPUState *env, TranslationBlock *tb) {
	// write LLVM FUNCTION to pandalog
	
	if (pandalog) {
		Panda__LLVMEntry *llvmentry = (Panda__LLVMEntry *)(malloc(sizeof(Panda__LLVMEntry)));
		*llvmentry = PANDA__LLVMENTRY__INIT;
		llvmentry->has_type = 1;
		llvmentry->type = FunctionCode::LLVM_FN;
		llvmentry->has_address = 0;
		if (tb->llvm_function->getName().startswith("tcg-llvm-tb-")) {
			int tb_num; 
			sscanf(tb->llvm_function->getName().str().c_str(), "tcg-llvm-tb-%d-%*d", &tb_num); 
			llvmentry->has_tb_num = 1;
			llvmentry->tb_num = tb_num;
		}	
		
        Panda__LogEntry logEntry = PANDA__LOG_ENTRY__INIT;
		logEntry.llvmentry = llvmentry;
		pandalog_write_entry(&logEntry);
	}

	return 0;
}

int cb_cpu_restore_state(CPUState *env, TranslationBlock *tb){
    printf("EXCEPTION - logging\n");
		
	if (pandalog) {
			Panda__LLVMEntry *llvmentry = (Panda__LLVMEntry *)(malloc(sizeof(Panda__LLVMEntry)));
			*llvmentry = PANDA__LLVMENTRY__INIT;
			llvmentry->has_type = 1;
			llvmentry->type = FunctionCode::LLVM_EXCEPTION;
			llvmentry->has_address = 0;
			Panda__LogEntry logEntry = PANDA__LOG_ENTRY__INIT;
			logEntry.llvmentry = llvmentry;
			pandalog_write_entry(&logEntry);
		}
	
    return 0; 
}


bool init_plugin(void *self){
    printf("Initializing plugin llvm_trace2\n");

    panda_cb pcb;
    panda_enable_memcb();
    pcb.before_block_exec = before_block_exec;
    panda_register_callback(self, PANDA_CB_BEFORE_BLOCK_EXEC, pcb);

    // Initialize pass manager
	
	// I have to enable llvm to get the tcg_llvm_ctx
	if (!execute_llvm){
        panda_enable_llvm();
    }
    
    panda_enable_llvm_helpers();

    llvm::llvm_init();
  /*
     * Run instrumentation pass over all helper functions that are now in the
     * module, and verify module.
     */
	llvm::Module *module = tcg_llvm_ctx->getModule();

    // Populate module with helper function log ops 
	/*for (llvm::Function f : *mod){*/
		for (llvm::Module::iterator func = module->begin(), mod_end = module->end(); func != mod_end; ++func) {
			for (llvm::Function::iterator b = func->begin(), be = func->end(); b != be; ++b) {
				llvm::BasicBlock* bb = b;
				llvm::PLTP->runOnBasicBlock(*bb);
			}
		}

	return true;
}
void uninit_plugin(void *self){
    printf("Uninitializing plugin\n");

    /*char modpath[256];*/
    /*strcpy(modpath, basedir);*/
    /*strcat(modpath, "/llvm-mod.bc");*/
    tcg_llvm_write_module(tcg_llvm_ctx, "./llvm-mod.bc");
    
	 llvm::PassRegistry *pr = llvm::PassRegistry::getPassRegistry();
    const llvm::PassInfo *pi =
        pr->getPassInfo(llvm::StringRef("PandaLLVMTrace"));
    if (!pi){
        printf("Unable to find 'PandaLLVMTrace' pass in pass registry\n");
    }
    else {
        pr->unregisterPass(*pi);
    }

    panda_disable_llvm_helpers();

    if (execute_llvm){
        panda_disable_llvm();
    }
    panda_disable_memcb();
}