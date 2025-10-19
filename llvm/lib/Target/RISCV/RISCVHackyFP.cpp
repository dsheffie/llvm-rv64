//===- RISCVOptWInstrs.cpp - MI W instruction optimizations ---------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===---------------------------------------------------------------------===//
//
// This pass does some optimizations for *W instructions at the MI level.
//
// First it removes unneeded sext.w instructions. Either because the sign
// extended bits aren't consumed or because the input was already sign extended
// by an earlier instruction.
//
// Then:
// 1. Unless explicit disabled or the target prefers instructions with W suffix,
//    it removes the -w suffix from opw instructions whenever all users are
//    dependent only on the lower word of the result of the instruction.
//    The cases handled are:
//    * addw because c.add has a larger register encoding than c.addw.
//    * addiw because it helps reduce test differences between RV32 and RV64
//      w/o being a pessimization.
//    * mulw because c.mulw doesn't exist but c.mul does (w/ zcb)
//    * slliw because c.slliw doesn't exist and c.slli does
//
// 2. Or if explicit enabled or the target prefers instructions with W suffix,
//    it adds the W suffix to the instruction whenever all users are dependent
//    only on the lower word of the result of the instruction.
//    The cases handled are:
//    * add/addi/sub/mul.
//    * slli with imm < 32.
//    * ld/lwu.
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

  bool MadeChange = false;

  return MadeChange;
}
