// Copyright 2019-2020 CERN and copyright holders of ALICE O2.
// See https://alice-o2.web.cern.ch/copyright for details of the copyright holders.
// All rights not expressly granted are reserved.
//
// This software is distributed under the terms of the GNU General Public
// License v3 (GPL Version 3), copied verbatim in the file "COPYING".
//
// In applying this license CERN does not waive the privileges and immunities
// granted to it by virtue of its status as an Intergovernmental Organization
// or submit itself to any jurisdiction.

/// \file GPUTPCDecompression.h
/// \author Gabriele Cimador

#ifndef GPUTPCDECOMPRESSION_H
#define GPUTPCDECOMPRESSION_H

#include "GPUDef.h"
#include "GPUProcessor.h"
#include "GPUCommonMath.h"
#include "GPUParam.h"
#include "GPUO2DataTypes.h"

#ifdef GPUCA_HAVE_O2HEADERS
#include "DataFormatsTPC/CompressedClusters.h"
#else
namespace o2::tpc
{
struct CompressedClustersPtrs {
};
struct CompressedClusters {
};
struct CompressedClustersFlat {
};
} // namespace o2::tpc
#endif

namespace GPUCA_NAMESPACE::gpu
{

class GPUTPCDecompression : public GPUProcessor
{
  friend class GPUTPCDecompressionKernels;
  friend class GPUTPCDecompressionUtilKernels;
  friend class GPUChainTracking;
  friend class TPCClusterDecompressionCore;

 public:
#ifndef GPUCA_GPUCODE
  void InitializeProcessor();
  void RegisterMemoryAllocation();
  void SetMaxData(const GPUTrackingInOutPointers& io);

  void* SetPointersInputGPU(void* mem);
  void* SetPointersTmpNativeBuffersGPU(void* mem);
  void* SetPointersTmpNativeBuffersOutput(void* mem);
  void* SetPointersTmpNativeBuffersInput(void* mem);

#endif

 protected:
  constexpr static uint32_t NSLICES = GPUCA_NSLICES;
  o2::tpc::CompressedClusters mInputGPU;

  uint32_t mMaxNativeClustersPerBuffer;
  uint32_t* mNativeClustersIndex;
  uint32_t* mUnattachedClustersOffsets;
  uint32_t* mAttachedClustersOffsets;
  o2::tpc::ClusterNative* mTmpNativeClusters;
  o2::tpc::ClusterNative* mNativeClustersBuffer;

  template <class T>
  void SetPointersCompressedClusters(void*& mem, T& c, uint32_t nClA, uint32_t nTr, uint32_t nClU, bool reducedClA);

  int16_t mMemoryResInputGPU = -1;
  int16_t mResourceTmpIndexes = -1;
  int16_t mResourceTmpClustersOffsets = -1;
};
} // namespace GPUCA_NAMESPACE::gpu
#endif // GPUTPCDECOMPRESSION_H
