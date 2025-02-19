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

/// \file TopologyDictionary.cxx

#include "ITS3Reconstruction/BuildTopologyDictionary.h"
#include "ITS3Reconstruction/LookUp.h"
#include "DataFormatsITSMFT/CompCluster.h"
#include "ITS3Base/SegmentationSuperAlpide.h"

#include "TFile.h"

ClassImp(o2::its3::BuildTopologyDictionary);

namespace o2::its3
{
void BuildTopologyDictionary::accountTopology(const itsmft::ClusterTopology& cluster, float dX, float dZ)
{
  mTotClusters++;
  bool useDf = dX < IgnoreVal / 2; // we may need to account the frequency but to not update the centroid

  // std::pair<unordered_map<unsigned long, itsmft::TopoStat>::iterator,bool> ret;
  // auto ret = mTopologyMap.insert(std::make_pair(cluster.getHash(), std::make_pair(cluster, 1)));
  auto& topoStat = mTopologyMap[cluster.getHash()];
  topoStat.countsTotal++;
  if (topoStat.countsTotal == 1) { // a new topology is inserted
    topoStat.topology = cluster;
    //___________________DEFINING_TOPOLOGY_CHARACTERISTICS__________________
    itsmft::TopologyInfo topInf;
    topInf.mPattern.setPattern(cluster.getPattern().data());
    topInf.mSizeX = cluster.getRowSpan();
    topInf.mSizeZ = cluster.getColumnSpan();
    //__________________COG_Determination_____________
    topInf.mNpixels = cluster.getClusterPattern().getCOG(topInf.mCOGx, topInf.mCOGz);
    if (useDf) {
      topInf.mXmean = dX;
      topInf.mZmean = dZ;
      topoStat.countsWithBias = 1;
    } else { // assign expected sigmas from the pixel X, Z sizes
      topInf.mXsigma2 = 1.f / 12.f / (float)std::min(10, topInf.mSizeX);
      topInf.mZsigma2 = 1.f / 12.f / (float)std::min(10, topInf.mSizeZ);
    }
    mMapInfo.emplace(cluster.getHash(), topInf);
  } else {
    if (useDf) {
      auto num = topoStat.countsWithBias++;
      auto ind = mMapInfo.find(cluster.getHash());
      float tmpxMean = ind->second.mXmean;
      float newxMean = ind->second.mXmean = ((tmpxMean)*num + dX) / (num + 1);
      float tmpxSigma2 = ind->second.mXsigma2;
      ind->second.mXsigma2 = (num * tmpxSigma2 + (dX - tmpxMean) * (dX - newxMean)) / (num + 1); // online variance algorithm
      float tmpzMean = ind->second.mZmean;
      float newzMean = ind->second.mZmean = ((tmpzMean)*num + dZ) / (num + 1);
      float tmpzSigma2 = ind->second.mZsigma2;
      ind->second.mZsigma2 = (num * tmpzSigma2 + (dZ - tmpzMean) * (dZ - newzMean)) / (num + 1); // online variance algorithm
    }
  }
}

void BuildTopologyDictionary::setThreshold(double thr)
{
  mTopologyFrequency.clear();
  for (auto&& p : mTopologyMap) { // p is pair<ulong,TopoStat>
    mTopologyFrequency.emplace_back(p.second.countsTotal, p.first);
  }
  std::sort(mTopologyFrequency.begin(), mTopologyFrequency.end(),
            [](const std::pair<unsigned long, unsigned long>& couple1,
               const std::pair<unsigned long, unsigned long>& couple2) { return (couple1.first > couple2.first); });
  mNCommonTopologies = 0;
  mDictionary.mCommonMap.clear();
  mDictionary.mGroupMap.clear();
  mFrequencyThreshold = thr;
  for (auto& q : mTopologyFrequency) {
    if (((double)q.first) / mTotClusters > thr) {
      mNCommonTopologies++;
    } else {
      break;
    }
  }
  if (mNCommonTopologies >= itsmft::CompCluster::InvalidPatternID) {
    mFrequencyThreshold = ((double)mTopologyFrequency[itsmft::CompCluster::InvalidPatternID - 1].first) / mTotClusters;
    LOGP(warning, "Redefining prob. threshould from {} to {} to be below InvalidPatternID (was {})", thr, mFrequencyThreshold, mNCommonTopologies);
    mNCommonTopologies = itsmft::CompCluster::InvalidPatternID - 1;
  }
}

void BuildTopologyDictionary::setNCommon(unsigned int nCommon)
{
  if (nCommon >= itsmft::CompCluster::InvalidPatternID) {
    LOGP(warning, "Redefining nCommon from {} to {} to be below InvalidPatternID", nCommon, itsmft::CompCluster::InvalidPatternID - 1);
    nCommon = itsmft::CompCluster::InvalidPatternID - 1;
  }
  mTopologyFrequency.clear();
  for (auto&& p : mTopologyMap) { // p os pair<ulong,TopoStat>
    mTopologyFrequency.emplace_back(p.second.countsTotal, p.first);
  }
  std::sort(mTopologyFrequency.begin(), mTopologyFrequency.end(),
            [](const std::pair<unsigned long, unsigned long>& couple1,
               const std::pair<unsigned long, unsigned long>& couple2) { return (couple1.first > couple2.first); });
  mNCommonTopologies = nCommon;
  mDictionary.mCommonMap.clear();
  mDictionary.mGroupMap.clear();
  mFrequencyThreshold = ((double)mTopologyFrequency[mNCommonTopologies - 1].first) / mTotClusters;
}

void BuildTopologyDictionary::setThresholdCumulative(double cumulative)
{
  mTopologyFrequency.clear();
  if (cumulative <= 0. || cumulative >= 1.) {
    cumulative = 0.99;
  }
  double totFreq = 0.;
  for (auto&& p : mTopologyMap) { // p os pair<ulong,TopoStat>
    mTopologyFrequency.emplace_back(p.second.countsTotal, p.first);
  }
  std::sort(mTopologyFrequency.begin(), mTopologyFrequency.end(),
            [](const std::pair<unsigned long, unsigned long>& couple1,
               const std::pair<unsigned long, unsigned long>& couple2) { return (couple1.first > couple2.first); });
  mNCommonTopologies = 0;
  mDictionary.mCommonMap.clear();
  mDictionary.mGroupMap.clear();
  for (auto& q : mTopologyFrequency) {
    totFreq += ((double)(q.first)) / mTotClusters;
    if (totFreq < cumulative) {
      mNCommonTopologies++;
      if (mNCommonTopologies >= itsmft::CompCluster::InvalidPatternID) {
        totFreq -= ((double)(q.first)) / mTotClusters;
        mNCommonTopologies--;
        LOGP(warning, "Redefining cumulative threshould from {} to {} to be below InvalidPatternID)", cumulative, totFreq);
      }
    } else {
      break;
    }
  }
  mFrequencyThreshold = ((double)(mTopologyFrequency[--mNCommonTopologies].first)) / mTotClusters;
  while (std::fabs(((double)mTopologyFrequency[mNCommonTopologies].first) / mTotClusters - mFrequencyThreshold) < 1.e-15) {
    mNCommonTopologies--;
  }
  mFrequencyThreshold = ((double)mTopologyFrequency[mNCommonTopologies++].first) / mTotClusters;
}

void BuildTopologyDictionary::groupRareTopologies()
{
  LOG(info) << "Dictionary finalisation";
  LOG(info) << "Number of clusters: " << mTotClusters;

  double totFreq = 0.;
  for (unsigned int j = 0; j < mNCommonTopologies; j++) {
    itsmft::GroupStruct gr;
    gr.mHash = mTopologyFrequency[j].second;
    gr.mFrequency = ((double)(mTopologyFrequency[j].first)) / mTotClusters;
    totFreq += gr.mFrequency;
    // rough estimation for the error considering a8 uniform distribution
    const auto& topo = mMapInfo.find(gr.mHash)->second;
    gr.mErrX = std::sqrt(topo.mXsigma2);
    gr.mErrZ = std::sqrt(topo.mZsigma2);
    gr.mErr2X = topo.mXsigma2;
    gr.mErr2Z = topo.mZsigma2;
    gr.mXCOG = -1 * topo.mCOGx;
    gr.mZCOG = topo.mCOGz;
    gr.mNpixels = topo.mNpixels;
    gr.mPattern = topo.mPattern;
    gr.mIsGroup = false;
    mDictionary.mVectorOfIDs.push_back(gr);
    if (j == int(itsmft::CompCluster::InvalidPatternID - 1)) {
      LOGP(warning, "Limiting N unique topologies to {}, threshold freq. to {}, cumulative freq. to {} to be below InvalidPatternID", j, gr.mFrequency, totFreq);
      mNCommonTopologies = j;
      mFrequencyThreshold = gr.mFrequency;
      break;
    }
  }
  // groupRareTopologies based on binning over number of rows and columns (TopologyDictionary::NumberOfRowClasses *
  // NumberOfColClasses)

  std::unordered_map<int, std::pair<itsmft::GroupStruct, unsigned long>> tmp_GroupMap; //<group ID, <Group struct, counts>>

  int grNum = 0;
  int rowBinEdge = 0;
  int colBinEdge = 0;
  for (int iRowClass = 0; iRowClass < its3::TopologyDictionary::MaxNumberOfRowClasses; iRowClass++) {
    for (int iColClass = 0; iColClass < its3::TopologyDictionary::MaxNumberOfColClasses; iColClass++) {
      rowBinEdge = (iRowClass + 1) * its3::TopologyDictionary::RowClassSpan;
      colBinEdge = (iColClass + 1) * its3::TopologyDictionary::ColClassSpan;
      grNum = its3::LookUp::groupFinder(rowBinEdge, colBinEdge);
      // Create a structure for a group of rare topologies
      itsmft::GroupStruct gr;
      gr.mHash = (((unsigned long)(grNum)) << 32) & 0xffffffff00000000;
      gr.mErrX = its3::TopologyDictionary::RowClassSpan / std::sqrt(12 * std::min(10, rowBinEdge));
      gr.mErrZ = its3::TopologyDictionary::ColClassSpan / std::sqrt(12 * std::min(10, colBinEdge));
      gr.mErr2X = gr.mErrX * gr.mErrX;
      gr.mErr2Z = gr.mErrZ * gr.mErrZ;
      gr.mXCOG = 0;
      gr.mZCOG = 0;
      gr.mNpixels = rowBinEdge * colBinEdge;
      gr.mIsGroup = true;
      gr.mFrequency = 0.;
      /// A dummy pattern with all fired pixels in the bounding box is assigned to groups of rare topologies.
      unsigned char dummyPattern[itsmft::ClusterPattern::kExtendedPatternBytes] = {0};
      dummyPattern[0] = (unsigned char)rowBinEdge;
      dummyPattern[1] = (unsigned char)colBinEdge;
      int nBits = rowBinEdge * colBinEdge;
      int nBytes = nBits / 8;
      for (int iB = 2; iB < nBytes + 2; iB++) {
        dummyPattern[iB] = (unsigned char)255;
      }
      int residualBits = nBits % 8;
      if (residualBits != 0) {
        unsigned char tempChar = 0;
        while (residualBits > 0) {
          residualBits--;
          tempChar |= 1 << (7 - residualBits);
        }
        dummyPattern[nBytes + 2] = tempChar;
      }
      gr.mPattern.setPattern(dummyPattern);
      // Filling the map for groups
      tmp_GroupMap[grNum] = std::make_pair(gr, 0);
    }
  }
  int rs{}, cs{}, index{};

  // Updating the counts for the groups of rare topologies
  for (auto j{mNCommonTopologies}; j < mTopologyFrequency.size(); j++) {
    unsigned long hash1 = mTopologyFrequency[j].second;
    rs = mTopologyMap.find(hash1)->second.topology.getRowSpan();
    cs = mTopologyMap.find(hash1)->second.topology.getColumnSpan();
    index = its3::LookUp::groupFinder(rs, cs);
    tmp_GroupMap[index].second += mTopologyFrequency[j].first;
  }

  for (auto&& p : tmp_GroupMap) {
    itsmft::GroupStruct& group = p.second.first;
    group.mFrequency = ((double)p.second.second) / mTotClusters;
    mDictionary.mVectorOfIDs.push_back(group);
  }

  // Sorting the dictionary preserving all unique topologies
  std::sort(mDictionary.mVectorOfIDs.begin(), mDictionary.mVectorOfIDs.end(), [](const itsmft::GroupStruct& a, const itsmft::GroupStruct& b) {
    return (!a.mIsGroup) && b.mIsGroup ? true : (a.mIsGroup && (!b.mIsGroup) ? false : (a.mFrequency > b.mFrequency));
  });
  if (mDictionary.mVectorOfIDs.size() >= itsmft::CompCluster::InvalidPatternID - 1) {
    LOGP(warning, "Max allowed {} patterns is reached, stopping", itsmft::CompCluster::InvalidPatternID - 1);
    mDictionary.mVectorOfIDs.resize(itsmft::CompCluster::InvalidPatternID - 1);
  }
  // Sorting the dictionary to final form
  std::sort(mDictionary.mVectorOfIDs.begin(), mDictionary.mVectorOfIDs.end(), [](const itsmft::GroupStruct& a, const itsmft::GroupStruct& b) { return a.mFrequency > b.mFrequency; });
  // Creating the map for common topologies
  for (int iKey = 0; iKey < mDictionary.getSize(); iKey++) {
    itsmft::GroupStruct& gr = mDictionary.mVectorOfIDs[iKey];
    if (!gr.mIsGroup) {
      mDictionary.mCommonMap.emplace(gr.mHash, iKey);
      if (gr.mPattern.getUsedBytes() == 1) {
        mDictionary.mSmallTopologiesLUT[(gr.mPattern.getColumnSpan() - 1) * 255 + (int)gr.mPattern.getByte(2)] = iKey;
      }
    } else {
      mDictionary.mGroupMap.emplace((int)(gr.mHash >> 32) & 0x00000000ffffffff, iKey);
    }
  }
  LOG(info) << "Dictionay finalised";
  LOG(info) << "Number of keys: " << mDictionary.getSize();
  LOG(info) << "Number of common topologies: " << mDictionary.mCommonMap.size();
  LOG(info) << "Number of groups of rare topologies: " << mDictionary.mGroupMap.size();
}

std::ostream& operator<<(std::ostream& os, const BuildTopologyDictionary& DB)
{
  for (unsigned int i = 0; i < DB.mNCommonTopologies; i++) {
    const unsigned long& hash = DB.mTopologyFrequency[i].second;
    os << "Hash: " << hash << '\n';
    os << "counts: " << DB.mTopologyMap.find(hash)->second.countsTotal;
    os << " (with bias provided: " << DB.mTopologyMap.find(hash)->second.countsWithBias << ")" << '\n';
    os << "sigmaX: " << std::sqrt(DB.mMapInfo.find(hash)->second.mXsigma2) << '\n';
    os << "sigmaZ: " << std::sqrt(DB.mMapInfo.find(hash)->second.mZsigma2) << '\n';
    os << DB.mTopologyMap.find(hash)->second.topology;
  }
  return os;
}

void BuildTopologyDictionary::printDictionary(const std::string& fname)
{
  LOG(info) << "Saving the the dictionary in binary format: ";
  std::ofstream out(fname);
  out << mDictionary;
  out.close();
  LOG(info) << " `-> done!";
}

void BuildTopologyDictionary::printDictionaryBinary(const std::string& fname)
{
  LOG(info) << "Printing the dictionary: ";
  std::ofstream out(fname);
  mDictionary.writeBinaryFile(fname);
  out.close();
  LOG(info) << " `-> done!";
}

void BuildTopologyDictionary::saveDictionaryRoot(const std::string& fname)
{
  LOG(info) << "Saving the the dictionary in a ROOT file: " << fname;
  TFile output(fname.c_str(), "recreate");
  output.WriteObjectAny(&mDictionary, mDictionary.Class(), "ccdb_object");
  output.Close();
  LOG(info) << " `-> done!";
}

} // namespace o2::its3
