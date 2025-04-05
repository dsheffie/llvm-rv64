//==- RISCVSchedDsheffie.td - Dsheffie Scheduling Definitions ----*- tablegen -*-=//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

// ===---------------------------------------------------------------------===//
// The following definitions describe the simpler per-operand machine model.
// This works with MachineScheduler. See MCSchedule.h for details.

// Dsheffie machine model for scheduling and other instruction cost heuristics.
def DsheffieModel : SchedMachineModel {
  let MicroOpBufferSize = 32; // Dsheffie out-of-order.
  let IssueWidth = 2;        // 2 micro-op is dispatched per cycle.
  let LoadLatency = 3;
  let MispredictPenalty = 10;
  let CompleteModel = false;
  let UnsupportedFeatures = [HasStdExtZbkb, HasStdExtZbkc, HasStdExtZbkx,
                             HasStdExtZcmt, HasStdExtZknd, HasStdExtZkne,
                             HasStdExtZknh, HasStdExtZksed, HasStdExtZksh,
                             HasStdExtZkr, HasVInstructions, HasVInstructionsI64];
}

//===----------------------------------------------------------------------===//
// Define each kind of processor resource and number available.

let BufferSize = 8 in {
def DsheffieUnitALU        : ProcResource<2>; // Int ALU
def DsheffieUnitIMul       : ProcResource<1>; // Int Multiply
def DsheffieUnitMem        : ProcResource<1>; // Load/Store
def DsheffieUnitB          : ProcResource<2>; // Branch

def DsheffieUnitFPALU      : ProcResource<1>; // FP ALU
}

let BufferSize = 1 in {
def DsheffieUnitIDiv       : ProcResource<1>; // Int Division
def DsheffieUnitFPDivSqrt  : ProcResource<1>; // FP Divide/Sqrt
}

//===----------------------------------------------------------------------===//

let SchedModel = DsheffieModel in {

// Branching
def : WriteRes<WriteJmp, [DsheffieUnitB]>;
def : WriteRes<WriteJal, [DsheffieUnitB]>;
def : WriteRes<WriteJalr, [DsheffieUnitB]>;


// Integer arithmetic and logic
def : WriteRes<WriteIALU32, [DsheffieUnitALU]>;
def : WriteRes<WriteIALU, [DsheffieUnitALU]>;
def : WriteRes<WriteShiftImm32, [DsheffieUnitALU]>;
def : WriteRes<WriteShiftImm, [DsheffieUnitALU]>;
def : WriteRes<WriteShiftReg32, [DsheffieUnitALU]>;
def : WriteRes<WriteShiftReg, [DsheffieUnitALU]>;

def : WriteRes<WriteCLZ, [DsheffieUnitALU]>;
def : WriteRes<WriteCLZ32, [DsheffieUnitALU]>;
def : WriteRes<WriteCTZ, [DsheffieUnitALU]>;
def : WriteRes<WriteCTZ32, [DsheffieUnitALU]>;
def : WriteRes<WriteCPOP, [DsheffieUnitALU]>;
def : WriteRes<WriteCPOP32, [DsheffieUnitALU]>;

def : WriteRes<WriteIMinMax, [DsheffieUnitALU]>;
def : WriteRes<WriteORCB, [DsheffieUnitALU]>;
def : WriteRes<WriteREV8, [DsheffieUnitALU]>;
def : WriteRes<WriteRotateReg, [DsheffieUnitALU]>;
def : WriteRes<WriteRotateReg32, [DsheffieUnitALU]>;
def : WriteRes<WriteRotateImm,  [DsheffieUnitALU]>;
def : WriteRes<WriteRotateImm32,  [DsheffieUnitALU]>;
def : WriteRes<WriteSHXADD, [DsheffieUnitALU]>;
def : WriteRes<WriteSHXADD32, [DsheffieUnitALU]>;

// Integer multiplication
let Latency = 4 in {
def : WriteRes<WriteIMul, [DsheffieUnitIMul]>;
def : WriteRes<WriteIMul32, [DsheffieUnitIMul]>;
}

// Integer division
// Worst case latency is used.
def : WriteRes<WriteIDiv32, [DsheffieUnitIDiv]> {
  let Latency = 34;
  let ReleaseAtCycles = [34];
}
def : WriteRes<WriteIDiv, [DsheffieUnitIDiv]> {
  let Latency = 33;
  let ReleaseAtCycles = [33];
}

// Integer remainder
def : WriteRes<WriteIRem32, [DsheffieUnitIDiv]> {
  let Latency = 34;
  let ReleaseAtCycles = [34];
}
def : WriteRes<WriteIRem, [DsheffieUnitIDiv]> {
  let Latency = 33;
  let ReleaseAtCycles = [33];
}

// Memory
def : WriteRes<WriteSTB, [DsheffieUnitMem]>;
def : WriteRes<WriteSTH, [DsheffieUnitMem]>;
def : WriteRes<WriteSTW, [DsheffieUnitMem]>;
def : WriteRes<WriteSTD, [DsheffieUnitMem]>;
def : WriteRes<WriteFST32, [DsheffieUnitMem]>;
def : WriteRes<WriteFST64, [DsheffieUnitMem]>;

let Latency = 3 in {
def : WriteRes<WriteLDB, [DsheffieUnitMem]>;
def : WriteRes<WriteLDH, [DsheffieUnitMem]>;
}

let Latency = 2 in {
def : WriteRes<WriteLDW, [DsheffieUnitMem]>;
def : WriteRes<WriteLDD, [DsheffieUnitMem]>;
def : WriteRes<WriteFLD32, [DsheffieUnitMem]>;
def : WriteRes<WriteFLD64, [DsheffieUnitMem]>;

// Atomic memory
def : WriteRes<WriteAtomicW, [DsheffieUnitMem]>;
def : WriteRes<WriteAtomicD, [DsheffieUnitMem]>;

def : WriteRes<WriteAtomicLDW, [DsheffieUnitMem]>;
def : WriteRes<WriteAtomicLDD, [DsheffieUnitMem]>;
}

def : WriteRes<WriteAtomicSTW, [DsheffieUnitMem]>;
def : WriteRes<WriteAtomicSTD, [DsheffieUnitMem]>;

// Single precision.
let Latency = 4 in {
def : WriteRes<WriteFAdd32, [DsheffieUnitFPALU]>;
def : WriteRes<WriteFSGNJ32, [DsheffieUnitFPALU]>;
def : WriteRes<WriteFMinMax32, [DsheffieUnitFPALU]>;
}

// Double precision
let Latency = 6 in {
def : WriteRes<WriteFAdd64, [DsheffieUnitFPALU]>;
def : WriteRes<WriteFSGNJ64, [DsheffieUnitFPALU]>;
def : WriteRes<WriteFMinMax64, [DsheffieUnitFPALU]>;
}

// Conversions
let Latency = 2 in {
def : WriteRes<WriteFCvtI32ToF32, [DsheffieUnitFPALU]>;
def : WriteRes<WriteFCvtI32ToF64, [DsheffieUnitFPALU]>;
def : WriteRes<WriteFCvtI64ToF32, [DsheffieUnitFPALU]>;
def : WriteRes<WriteFCvtI64ToF64, [DsheffieUnitFPALU]>;
def : WriteRes<WriteFCvtF32ToI32, [DsheffieUnitFPALU]>;
def : WriteRes<WriteFCvtF32ToI64, [DsheffieUnitFPALU]>;
def : WriteRes<WriteFCvtF64ToI32, [DsheffieUnitFPALU]>;
def : WriteRes<WriteFCvtF64ToI64, [DsheffieUnitFPALU]>;
def : WriteRes<WriteFCvtF32ToF64, [DsheffieUnitFPALU]>;
def : WriteRes<WriteFCvtF64ToF32, [DsheffieUnitFPALU]>;

def : WriteRes<WriteFClass32, [DsheffieUnitFPALU]>;
def : WriteRes<WriteFClass64, [DsheffieUnitFPALU]>;
def : WriteRes<WriteFCmp32, [DsheffieUnitFPALU]>;
def : WriteRes<WriteFCmp64, [DsheffieUnitFPALU]>;
def : WriteRes<WriteFMovF32ToI32, [DsheffieUnitFPALU]>;
def : WriteRes<WriteFMovI32ToF32, [DsheffieUnitFPALU]>;
def : WriteRes<WriteFMovF64ToI64, [DsheffieUnitFPALU]>;
def : WriteRes<WriteFMovI64ToF64, [DsheffieUnitFPALU]>;
}

// FP multiplication
let Latency = 5 in {
def : WriteRes<WriteFMul32, [DsheffieUnitFPALU]>;
def : WriteRes<WriteFMA32, [DsheffieUnitFPALU]>;
}

let Latency = 7 in {
def : WriteRes<WriteFMul64, [DsheffieUnitFPALU]>;
def : WriteRes<WriteFMA64, [DsheffieUnitFPALU]>;
}

// FP division
// FP division unit on Dsheffie is not pipelined, so set resource cycles to latency.
let Latency = 20, ReleaseAtCycles = [20] in {
def : WriteRes<WriteFDiv32, [DsheffieUnitFPDivSqrt]>;
def : WriteRes<WriteFDiv64, [DsheffieUnitFPDivSqrt]>;
}

// FP square root unit on Dsheffie is not pipelined, so set resource cycles to latency.
def : WriteRes<WriteFSqrt32, [DsheffieUnitFPDivSqrt]> { let Latency = 20;
                                                      let ReleaseAtCycles = [20]; }
def : WriteRes<WriteFSqrt64, [DsheffieUnitFPDivSqrt]> { let Latency = 25;
                                                      let ReleaseAtCycles = [25]; }

// Others
def : WriteRes<WriteCSR, []>;
def : WriteRes<WriteNop, []>;

def : InstRW<[WriteIALU], (instrs COPY)>;

//===----------------------------------------------------------------------===//
// Bypass and advance
def : ReadAdvance<ReadJmp, 0>;
def : ReadAdvance<ReadJalr, 0>;
def : ReadAdvance<ReadCSR, 0>;
def : ReadAdvance<ReadStoreData, 0>;
def : ReadAdvance<ReadMemBase, 0>;
def : ReadAdvance<ReadIALU, 0>;
def : ReadAdvance<ReadIALU32, 0>;
def : ReadAdvance<ReadShiftImm, 0>;
def : ReadAdvance<ReadShiftImm32, 0>;
def : ReadAdvance<ReadShiftReg, 0>;
def : ReadAdvance<ReadShiftReg32, 0>;
def : ReadAdvance<ReadIDiv, 0>;
def : ReadAdvance<ReadIDiv32, 0>;
def : ReadAdvance<ReadIRem, 0>;
def : ReadAdvance<ReadIRem32, 0>;
def : ReadAdvance<ReadIMul, 0>;
def : ReadAdvance<ReadIMul32, 0>;
def : ReadAdvance<ReadCLZ, 0>;
def : ReadAdvance<ReadCLZ32, 0>;
def : ReadAdvance<ReadCTZ, 0>;
def : ReadAdvance<ReadCTZ32, 0>;

def : ReadAdvance<ReadCPOP, 0>;
def : ReadAdvance<ReadCPOP32, 0>;

def : ReadAdvance<ReadIMinMax, 0>;
def : ReadAdvance<ReadORCB, 0>;
def : ReadAdvance<ReadREV8, 0>;
def : ReadAdvance<ReadRotateReg, 0>;
def : ReadAdvance<ReadRotateReg32, 0>;
def : ReadAdvance<ReadRotateImm, 0>;
def : ReadAdvance<ReadRotateImm32, 0>;
def : ReadAdvance<ReadSHXADD, 0>;
def : ReadAdvance<ReadSHXADD32, 0>;

def : ReadAdvance<ReadAtomicWA, 0>;
def : ReadAdvance<ReadAtomicWD, 0>;
def : ReadAdvance<ReadAtomicDA, 0>;
def : ReadAdvance<ReadAtomicDD, 0>;
def : ReadAdvance<ReadAtomicLDW, 0>;
def : ReadAdvance<ReadAtomicLDD, 0>;
def : ReadAdvance<ReadAtomicSTW, 0>;
def : ReadAdvance<ReadAtomicSTD, 0>;
def : ReadAdvance<ReadFStoreData, 0>;
def : ReadAdvance<ReadFMemBase, 0>;
def : ReadAdvance<ReadFAdd32, 0>;
def : ReadAdvance<ReadFAdd64, 0>;
def : ReadAdvance<ReadFMul32, 0>;
def : ReadAdvance<ReadFMul64, 0>;
def : ReadAdvance<ReadFMA32, 0>;
def : ReadAdvance<ReadFMA32Addend, 0>;
def : ReadAdvance<ReadFMA64, 0>;
def : ReadAdvance<ReadFMA64Addend, 0>;
def : ReadAdvance<ReadFDiv32, 0>;
def : ReadAdvance<ReadFDiv64, 0>;
def : ReadAdvance<ReadFSqrt32, 0>;
def : ReadAdvance<ReadFSqrt64, 0>;
def : ReadAdvance<ReadFCmp32, 0>;
def : ReadAdvance<ReadFCmp64, 0>;
def : ReadAdvance<ReadFSGNJ32, 0>;
def : ReadAdvance<ReadFSGNJ64, 0>;
def : ReadAdvance<ReadFMinMax32, 0>;
def : ReadAdvance<ReadFMinMax64, 0>;
def : ReadAdvance<ReadFCvtF32ToI32, 0>;
def : ReadAdvance<ReadFCvtF32ToI64, 0>;
def : ReadAdvance<ReadFCvtF64ToI32, 0>;
def : ReadAdvance<ReadFCvtF64ToI64, 0>;
def : ReadAdvance<ReadFCvtI32ToF32, 0>;
def : ReadAdvance<ReadFCvtI32ToF64, 0>;
def : ReadAdvance<ReadFCvtI64ToF32, 0>;
def : ReadAdvance<ReadFCvtI64ToF64, 0>;
def : ReadAdvance<ReadFCvtF32ToF64, 0>;
def : ReadAdvance<ReadFCvtF64ToF32, 0>;
def : ReadAdvance<ReadFMovF32ToI32, 0>;
def : ReadAdvance<ReadFMovI32ToF32, 0>;
def : ReadAdvance<ReadFMovF64ToI64, 0>;
def : ReadAdvance<ReadFMovI64ToF64, 0>;
def : ReadAdvance<ReadFClass32, 0>;
def : ReadAdvance<ReadFClass64, 0>;

//===----------------------------------------------------------------------===//
// Unsupported extensions
defm : UnsupportedSchedV;
defm : UnsupportedSchedZabha;
defm : UnsupportedSchedZbc;
defm : UnsupportedSchedZbs;
defm : UnsupportedSchedZbkb;
defm : UnsupportedSchedZbkx;
defm : UnsupportedSchedZfa;
defm : UnsupportedSchedZfhmin;
defm : UnsupportedSchedSFB;
defm : UnsupportedSchedXsfvcp;
defm : UnsupportedSchedZvk;
}
