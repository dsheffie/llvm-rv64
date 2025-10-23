//===----- RISCVCodeGenPrepare.cpp ----------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This is a RISC-V specific version of CodeGenPrepare.
// It munges the code in the input function to better prepare it for
// SelectionDAG-based code generation. This works around limitations in it's
// basic-block-at-a-time approach.
//
//===----------------------------------------------------------------------===//

#include "RISCV.h"
#include "RISCVTargetMachine.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/CodeGen/TargetPassConfig.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstVisitor.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/IntrinsicsRISCV.h"
#include "llvm/IR/PatternMatch.h"
#include "llvm/InitializePasses.h"
#include "llvm/Pass.h"

using namespace llvm;

#define DEBUG_TYPE "riscv-codegenprepare"
#define PASS_NAME "RISC-V CodeGenPrepare"

namespace {

class RISCVCodeGenPrepare : public FunctionPass,
                            public InstVisitor<RISCVCodeGenPrepare, bool> {
  const DataLayout *DL;
  const DominatorTree *DT;
  const RISCVSubtarget *ST;

public:
  static char ID;

  RISCVCodeGenPrepare() : FunctionPass(ID) {}

  bool runOnFunction(Function &F) override;

  StringRef getPassName() const override { return PASS_NAME; }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesCFG();
    AU.addRequired<DominatorTreeWrapperPass>();
    AU.addRequired<TargetPassConfig>();
  }

  bool visitInstruction(Instruction &I) { return false; }
  bool visitAnd(BinaryOperator &BO);
  bool visitIntrinsicInst(IntrinsicInst &I);
  bool expandVPStrideLoad(IntrinsicInst &I);
};

} // end anonymous namespace

// Try to optimize (i64 (and (zext/sext (i32 X), C1))) if C1 has bit 31 set,
// but bits 63:32 are zero. If we know that bit 31 of X is 0, we can fill
// the upper 32 bits with ones.
bool RISCVCodeGenPrepare::visitAnd(BinaryOperator &BO) {
  if (!ST->is64Bit())
    return false;

  if (!BO.getType()->isIntegerTy(64))
    return false;

  using namespace PatternMatch;

  // Left hand side should be a zext nneg.
  Value *LHSSrc;
  if (!match(BO.getOperand(0), m_NNegZExt(m_Value(LHSSrc))))
    return false;

  if (!LHSSrc->getType()->isIntegerTy(32))
    return false;

  // Right hand side should be a constant.
  Value *RHS = BO.getOperand(1);

  auto *CI = dyn_cast<ConstantInt>(RHS);
  if (!CI)
    return false;
  uint64_t C = CI->getZExtValue();

  // Look for constants that fit in 32 bits but not simm12, and can be made
  // into simm12 by sign extending bit 31. This will allow use of ANDI.
  // TODO: Is worth making simm32?
  if (!isUInt<32>(C) || isInt<12>(C) || !isInt<12>(SignExtend64<32>(C)))
    return false;

  // Sign extend the constant and replace the And operand.
  C = SignExtend64<32>(C);
  BO.setOperand(1, ConstantInt::get(RHS->getType(), C));

  return true;
}

// LLVM vector reduction intrinsics return a scalar result, but on RISC-V vector
// reduction instructions write the result in the first element of a vector
// register. So when a reduction in a loop uses a scalar phi, we end up with
// unnecessary scalar moves:
//
// loop:
// vfmv.s.f v10, fa0
// vfredosum.vs v8, v8, v10
// vfmv.f.s fa0, v8
//
// This mainly affects ordered fadd reductions, since other types of reduction
// typically use element-wise vectorisation in the loop body. This tries to
// vectorize any scalar phis that feed into a fadd reduction:
//
// loop:
// %phi = phi <float> [ ..., %entry ], [ %acc, %loop ]
// %acc = call float @llvm.vector.reduce.fadd.nxv2f32(float %phi,
//                                                    <vscale x 2 x float> %vec)
//
// ->
//
// loop:
// %phi = phi <vscale x 2 x float> [ ..., %entry ], [ %acc.vec, %loop ]
// %phi.scalar = extractelement <vscale x 2 x float> %phi, i64 0
// %acc = call float @llvm.vector.reduce.fadd.nxv2f32(float %x,
//                                                    <vscale x 2 x float> %vec)
// %acc.vec = insertelement <vscale x 2 x float> poison, float %acc.next, i64 0
//
// Which eliminates the scalar -> vector -> scalar crossing during instruction
// selection.
bool RISCVCodeGenPrepare::visitIntrinsicInst(IntrinsicInst &I) {
  if (expandVPStrideLoad(I))
    return true;

  if (I.getIntrinsicID() != Intrinsic::vector_reduce_fadd)
    return false;

  auto *PHI = dyn_cast<PHINode>(I.getOperand(0));
  if (!PHI || !PHI->hasOneUse() ||
      !llvm::is_contained(PHI->incoming_values(), &I))
    return false;

  Type *VecTy = I.getOperand(1)->getType();
  IRBuilder<> Builder(PHI);
  auto *VecPHI = Builder.CreatePHI(VecTy, PHI->getNumIncomingValues());

  for (auto *BB : PHI->blocks()) {
    Builder.SetInsertPoint(BB->getTerminator());
    Value *InsertElt = Builder.CreateInsertElement(
        VecTy, PHI->getIncomingValueForBlock(BB), (uint64_t)0);
    VecPHI->addIncoming(InsertElt, BB);
  }

  Builder.SetInsertPoint(&I);
  I.setOperand(0, Builder.CreateExtractElement(VecPHI, (uint64_t)0));

  PHI->eraseFromParent();

  return true;
}

// Always expand zero strided loads so we match more .vx splat patterns, even if
// we have +optimized-zero-stride-loads. RISCVDAGToDAGISel::Select will convert
// it back to a strided load if it's optimized.
bool RISCVCodeGenPrepare::expandVPStrideLoad(IntrinsicInst &II) {
  Value *BasePtr, *VL;

  using namespace PatternMatch;
  if (!match(&II, m_Intrinsic<Intrinsic::experimental_vp_strided_load>(
                      m_Value(BasePtr), m_Zero(), m_AllOnes(), m_Value(VL))))
    return false;

  // If SEW>XLEN then a splat will get lowered as a zero strided load anyway, so
  // avoid expanding here.
  if (II.getType()->getScalarSizeInBits() > ST->getXLen())
    return false;

  if (!isKnownNonZero(VL, {*DL, DT, nullptr, &II}))
    return false;

  auto *VTy = cast<VectorType>(II.getType());

  IRBuilder<> Builder(&II);
  Type *STy = VTy->getElementType();
  Value *Val = Builder.CreateLoad(STy, BasePtr);
  Value *Res = Builder.CreateIntrinsic(Intrinsic::experimental_vp_splat, {VTy},
                                       {Val, II.getOperand(2), VL});

  II.replaceAllUsesWith(Res);
  II.eraseFromParent();
  return true;
}

bool RISCVCodeGenPrepare::runOnFunction(Function &F) {
  if (skipFunction(F))
    return false;

  auto &TPC = getAnalysis<TargetPassConfig>();
  auto &TM = TPC.getTM<RISCVTargetMachine>();
  ST = &TM.getSubtarget<RISCVSubtarget>(F);

  DL = &F.getDataLayout();
  DT = &getAnalysis<DominatorTreeWrapperPass>().getDomTree();

  bool MadeChange = false;
  for (auto &BB : F) {
    for (Instruction &I : llvm::make_early_inc_range(BB)) {
      MadeChange |= visit(I);
    }
  }
  

 for (auto &BB : F) {
    for (Instruction &I : llvm::make_early_inc_range(BB)) {
      if(not(I.getType()->isFloatTy())) {
        continue;
      }
      CallInst *CI = dyn_cast<CallInst>(&I);
      if(CI == nullptr) {
        continue;
      }
      if(CI->getIntrinsicID() != Intrinsic::fmuladd) {
	continue;
      }
      IRBuilder<> Builder(&I);
      Value *M = Builder.CreateFMul(I.getOperand(0), I.getOperand(1));
      Value *A = Builder.CreateFAdd(M, I.getOperand(2));
      I.replaceAllUsesWith(A);
      I.eraseFromParent();	  
      MadeChange = true;
    }
 }


  
  for (auto &BB : F) {
    for (Instruction &I : llvm::make_early_inc_range(BB)) {
      // %2 = sitofp i32 %0 to float
      SIToFPInst *SI = dyn_cast<SIToFPInst>(&I);
      if(SI and I.getType()->isFloatTy() and I.getOperand(0)->getType()->isIntegerTy(32) ) {
	IRBuilder<> Builder(&I);
	auto ty32 = Builder.getInt32Ty();	
	auto ty64 = Builder.getInt64Ty();
	auto s = Builder.CreateSExt(I.getOperand(0), ty64);
	Value * Res = Builder.CreateIntrinsic(ty64, Intrinsic::riscv_hacky_int32_fp32_cvt, s);
	Res = Builder.CreateTrunc(Res, ty32);
	auto fp = Builder.CreateBitCast(Res, Builder.getFloatTy());	    	  
	I.replaceAllUsesWith(fp);
	I.eraseFromParent();	  
	MadeChange = true; 	
      }
      UIToFPInst *UI = dyn_cast<UIToFPInst>(&I);
      if(UI and I.getType()->isFloatTy() and I.getOperand(0)->getType()->isIntegerTy(32) ) {
	IRBuilder<> Builder(&I);
	auto ty32 = Builder.getInt32Ty();	
	auto ty64 = Builder.getInt64Ty();
	auto s = Builder.CreateSExt(I.getOperand(0), ty64);
	Value * Res = Builder.CreateIntrinsic(ty64, Intrinsic::riscv_hacky_uint32_fp32_cvt, s);
	Res = Builder.CreateTrunc(Res, ty32);
	auto fp = Builder.CreateBitCast(Res, Builder.getFloatTy());	    	  
	I.replaceAllUsesWith(fp);
	I.eraseFromParent();	  
	MadeChange = true; 	
      }
      
      FPToSIInst *FP = dyn_cast<FPToSIInst>(&I);      
      if(FP and I.getType()->isIntegerTy(32) and I.getOperand(0)->getType()->isFloatTy() ) {
	IRBuilder<> Builder(&I);
	auto ty32 = Builder.getInt32Ty();	
	auto ty64 = Builder.getInt64Ty();
	auto s = Builder.CreateBitCast(I.getOperand(0), ty32);
	s = Builder.CreateSExt(s, ty64);	
	Value * Res = Builder.CreateIntrinsic(ty64, Intrinsic::riscv_hacky_fp32_int32_cvt, s);
	Res = Builder.CreateTrunc(Res, ty32);
	I.replaceAllUsesWith(Res);
	I.eraseFromParent();	  
	MadeChange = true; 	
      }
      //fcmp ogt
      unsigned op = I.getOpcode();

      FCmpInst *FC = dyn_cast<FCmpInst>(&I);

      if(FC and I.getType()->isIntegerTy(1) and I.getOperand(0)->getType()->isFloatTy() and I.getOperand(1)->getType()->isFloatTy()) {
	switch(FC->getPredicate())
	  {
	  case FCmpInst::FCMP_OGT:
	  case FCmpInst::FCMP_OLT:
	  case FCmpInst::FCMP_ONE:
	    break;
	  default:
	    continue;
	  }
	IRBuilder<> Builder(&I);
	auto ty32 = Builder.getInt32Ty();
	auto ty64 = Builder.getInt64Ty();	  
	auto s0 = Builder.CreateBitCast(I.getOperand(0), ty32);
	auto s1 = Builder.CreateBitCast(I.getOperand(1), ty32);
	s0 = Builder.CreateSExt(s0, ty64);
	s1 = Builder.CreateSExt(s1, ty64);
	Value * Res = nullptr;
	if(FC->getPredicate() == FCmpInst::FCMP_OGT) {
	  Res = Builder.CreateIntrinsic(ty64, Intrinsic::riscv_hacky_fp32_cmpgt, {s0, s1});
	}
	else if(FC->getPredicate() == FCmpInst::FCMP_OLT) {
	  Res = Builder.CreateIntrinsic(ty64, Intrinsic::riscv_hacky_fp32_cmplt, {s0, s1});
	}
	else if(FC->getPredicate() == FCmpInst::FCMP_ONE) {
	  Res = Builder.CreateIntrinsic(ty64, Intrinsic::riscv_hacky_fp32_cmpne, {s0, s1});
	}	
	Res = Builder.CreateTrunc(Res, Builder.getInt1Ty());
	I.replaceAllUsesWith(Res);
	I.eraseFromParent();	  
	MadeChange = true; 	
      }
      
      if(not(I.getType()->isFloatTy())) {
	continue;
      }

      if(op == Instruction::FAdd or op == Instruction::FSub or op == Instruction::FMul) {
	IRBuilder<> Builder(&I);
	auto ty32 = Builder.getInt32Ty();
	auto ty64 = Builder.getInt64Ty();	  
	auto s0 = Builder.CreateBitCast(I.getOperand(0), ty32);
	auto s1 = Builder.CreateBitCast(I.getOperand(1), ty32);
	s0 = Builder.CreateSExt(s0, ty64);
	s1 = Builder.CreateSExt(s1, ty64);
	Value * Res = nullptr;
	switch(op)
	  {
	  case Instruction::FAdd:
	    Res = Builder.CreateIntrinsic(ty64, Intrinsic::riscv_hacky_fp32_add, {s0, s1});
	    break;
	  case Instruction::FSub:
	    Res = Builder.CreateIntrinsic(ty64, Intrinsic::riscv_hacky_fp32_sub, {s0, s1});
	    break;
	  case Instruction::FMul:
	    Res = Builder.CreateIntrinsic(ty64, Intrinsic::riscv_hacky_fp32_mul, {s0, s1});
	    break;	  	  
	  default:
	    break;
	  }
	Res = Builder.CreateTrunc(Res, ty32);
	auto fp = Builder.CreateBitCast(Res, Builder.getFloatTy());	    	  
	I.replaceAllUsesWith(fp);
	I.eraseFromParent();	  
	MadeChange = true; 
      }
      
    }
  }

  
  return MadeChange;
}

INITIALIZE_PASS_BEGIN(RISCVCodeGenPrepare, DEBUG_TYPE, PASS_NAME, false, false)
INITIALIZE_PASS_DEPENDENCY(TargetPassConfig)
INITIALIZE_PASS_END(RISCVCodeGenPrepare, DEBUG_TYPE, PASS_NAME, false, false)

char RISCVCodeGenPrepare::ID = 0;

FunctionPass *llvm::createRISCVCodeGenPreparePass() {
  return new RISCVCodeGenPrepare();
}
