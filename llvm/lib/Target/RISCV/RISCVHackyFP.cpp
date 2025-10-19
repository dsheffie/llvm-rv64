//===- RISCVOptWInstrs.cpp - MI W instruction optimizations ---------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===---------------------------------------------------------------------===//

#include "RISCV.h"
#include "RISCVMachineFunctionInfo.h"
#include "RISCVSubtarget.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/TargetInstrInfo.h"

using namespace llvm;

#define DEBUG_TYPE "riscv-hacky-fp"
#define RISCV_HACKY_FP_NAME "RISC-V softfloat tomfoolery"


namespace {

class RISCVHackyFP : public MachineFunctionPass {
public:
  static char ID;

  RISCVHackyFP() : MachineFunctionPass(ID) {}

  bool runOnMachineFunction(MachineFunction &MF) override;

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesCFG();
    MachineFunctionPass::getAnalysisUsage(AU);
  }

  StringRef getPassName() const override { return RISCV_HACKY_FP_NAME; }
};

} // end anonymous namespace

char RISCVHackyFP::ID = 0;
INITIALIZE_PASS(RISCVHackyFP, DEBUG_TYPE, RISCV_HACKY_FP_NAME, false,
                false)

FunctionPass *llvm::createRISCVHackyFPPass() {
  return new RISCVHackyFP();
}


bool RISCVHackyFP::runOnMachineFunction(MachineFunction &MF) {
  if (skipFunction(MF.getFunction()))
    return false;

  MachineRegisterInfo &MRI = MF.getRegInfo();
  const RISCVSubtarget &ST = MF.getSubtarget<RISCVSubtarget>();
  const RISCVInstrInfo &TII = *ST.getInstrInfo();

  if (!ST.is64Bit())
    return false;

  //return false;

  bool MadeChange = false;
  SmallVector<MachineInstr*> junk;
  
  for (MachineBasicBlock &MBB : MF) {
    for (MachineInstr &MI : llvm::make_early_inc_range(MBB)) {
      /* llvm::errs() << MI << "\n"; */
      
      if(MI.getOpcode() != RISCV::PseudoCALL) {
	continue;
      }
      auto II = MI.getIterator();

      MachineOperand Func = MI.getOperand(0);
      if(not(Func.isSymbol())) {
	continue;
      }
      const char *symbolName = Func.getSymbolName();
      if(strcmp("__mulsf3", symbolName) == 0) {
	BuildMI(MBB, II, DebugLoc(), TII.get(RISCV::FP32MUL))
	  .addUse(RISCV::X10)
	  .addUse(RISCV::X11)
	  .addDef(RISCV::X10);
	junk.push_back(&MI);
	MadeChange = true;
      }
      else if(strcmp("__addsf3", symbolName) == 0) {
	BuildMI(MBB, II, DebugLoc(), TII.get(RISCV::FP32ADD))
	  .addUse(RISCV::X10)
	  .addUse(RISCV::X11)
	  .addDef(RISCV::X10);
	junk.push_back(&MI);
	MadeChange = true;
      }
#if 0                  
      else if(strcmp("__subsf3", symbolName) == 0) {
	BuildMI(MBB, II, DebugLoc(), TII.get(RISCV::FP32SUB))
	  .addUse(RISCV::X10)
	  .addUse(RISCV::X11)
	  .addDef(RISCV::X10);
	junk.push_back(&MI);
	MadeChange = true;
      }
#endif
      else if(strcmp("__floatsisf", symbolName) == 0) {
	BuildMI(MBB, II, DebugLoc(), TII.get(RISCV::INT32TOFP32))
	  .addUse(RISCV::X10)
	  .addDef(RISCV::X10);
	junk.push_back(&MI);
	MadeChange = true;
      }
      else if(strcmp("__fixsfsi", symbolName) == 0) {
	BuildMI(MBB, II, DebugLoc(), TII.get(RISCV::FP32TOINT32))
	  .addUse(RISCV::X10)
	  .addDef(RISCV::X10);
	junk.push_back(&MI);
	MadeChange = true;
      }
    }
  }

  for(MachineInstr *MI : junk) {
    MI->eraseFromParent();
  }

  
  

  return MadeChange;
}
