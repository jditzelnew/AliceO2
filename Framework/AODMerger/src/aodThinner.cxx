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

#include <unordered_map>
#include <getopt.h>

#include "TSystem.h"
#include "TStopwatch.h"
#include "TString.h"
#include "TRegexp.h"
#include "TFile.h"
#include "TTree.h"
#include "TBranch.h"
#include "TList.h"
#include "TKey.h"
#include "TDirectory.h"
#include "TObjString.h"
#include "TGrid.h"
#include "TMap.h"
#include "TLeaf.h"

#include "aodMerger.h"

// AOD reduction tool
//   Designed for the 2022 pp data with specific selections:
//   - Remove all TPC only tracks, optionally keep TPC-only V0 tracks
//   - Remove all V0s which refer to any removed track
//   - Remove all cascade which refer to any removed V0
//   - Remove all ambiguous track entries which point to a track with collision
//   - Adjust all indices
int main(int argc, char* argv[])
{
  std::string inputFileName("AO2D.root");
  std::string outputFileName("AO2D_thinned.root");
  int exitCode = 0; // 0: success, !=0: failure
  bool bOverwrite = false;
  int compression = 505;

  int option_index = 1;

  const char* const short_opts = "i:o:KOh";
  static struct option long_options[] = {
    {"input", required_argument, nullptr, 'i'},
    {"output", required_argument, nullptr, 'o'},
    {"overwrite", no_argument, nullptr, 'O'},
    {"compression", required_argument, nullptr, 'c'},
    {"help", no_argument, nullptr, 'h'},
    {nullptr, 0, nullptr, 0}};

  while (true) {
    const auto opt = getopt_long(argc, argv, short_opts, long_options, &option_index);
    if (opt == -1) {
      break; // use defaults
    }
    switch (opt) {
      case 'i':
        inputFileName = optarg;
        break;
      case 'o':
        outputFileName = optarg;
        break;
      case 'O':
        bOverwrite = true;
        printf("Overwriting existing output file if existing\n");
        break;
      case 'c':
        compression = atoi(optarg);
        break;
      case 'h':
      case '?':
      default:
        printf("AO2D thinning tool. Options: \n");
        printf("  --input/-i <inputfile.root>     Contains input file path to the file to be thinned. Default: %s\n", inputFileName.c_str());
        printf("  --output/-o <outputfile.root>   Target output ROOT file. Default: %s\n", outputFileName.c_str());
        printf("  --compression/-c <compression id>   ROOT compression algorithm / level. Default: %d\n", compression);
        printf("\n");
        printf("  Optional Arguments:\n");
        printf("  --overwrite/-O                  Overwrite existing output file\n");
        return -1;
    }
  }

  printf("AOD reduction started with:\n");
  printf("  Input file: %s\n", inputFileName.c_str());
  printf("  Ouput file name: %s\n", outputFileName.c_str());

  TStopwatch clock;
  clock.Start(kTRUE);

  auto outputFile = TFile::Open(outputFileName.c_str(), (bOverwrite) ? "RECREATE" : "CREATE", "", compression);
  if (outputFile == nullptr) {
    printf("Error: File %s exists or cannot be created!\n", outputFileName.c_str());
    return 1;
  }
  TDirectory* outputDir = nullptr;

  if (inputFileName.find("alien:") == 0) {
    printf("Connecting to AliEn...");
    TGrid::Connect("alien:");
  }

  auto inputFile = TFile::Open(inputFileName.c_str());
  if (!inputFile) {
    printf("Error: Could not open input file %s.\n", inputFileName.c_str());
    return 1;
  }

  TList* keyList = inputFile->GetListOfKeys();
  keyList->Sort();

  for (auto key1 : *keyList) {
    // Keep metaData
    if (((TObjString*)key1)->GetString().EqualTo("metaData")) {
      auto metaData = (TMap*)inputFile->Get("metaData");
      outputFile->cd();
      metaData->Write("metaData", TObject::kSingleKey);
    }

    // Keep parentFiles
    if (((TObjString*)key1)->GetString().EqualTo("parentFiles")) {
      auto parentFiles = (TMap*)inputFile->Get("parentFiles");
      outputFile->cd();
      parentFiles->Write("parentFiles", TObject::kSingleKey);
    }

    // Skip everything else, except 'DF_*'
    if (!((TObjString*)key1)->GetString().BeginsWith("DF_")) {
      continue;
    }

    auto dfName = ((TObjString*)key1)->GetString().Data();

    printf("  Processing folder %s\n", dfName);
    auto folder = (TDirectoryFile*)inputFile->Get(dfName);
    auto treeList = folder->GetListOfKeys();
    treeList->Sort();

    // purging keys from duplicates
    for (auto i = 0; i < treeList->GetEntries(); ++i) {
      TKey* ki = (TKey*)treeList->At(i);
      for (int j = i + 1; j < treeList->GetEntries(); ++j) {
        TKey* kj = (TKey*)treeList->At(j);
        if (std::strcmp(ki->GetName(), kj->GetName()) == 0 && std::strcmp(ki->GetTitle(), kj->GetTitle()) == 0) {
          if (ki->GetCycle() < kj->GetCycle()) {
            printf("    *** FATAL *** we had ordered the keys, first cycle should be higher, please check");
            exitCode = 5;
          } else {
            // key is a duplicate, let's remove it
            treeList->Remove(kj);
            j--;
          }
        } else {
          // we changed key, since they are sorted, we won't have the same anymore
          break;
        }
      }
    }

    // Scan versions e.g. 001 or 002 ...
    TString v0Name{"O2v0_???"}, trkExtraName{"O2trackextra*"};
    TRegexp v0Re(v0Name, kTRUE), trkExtraRe(trkExtraName, kTRUE);
    bool hasTrackQA{false};
    TString trackQAName{"O2trackqa*"};
    TRegexp trackQARe(trackQAName, kTRUE);
    for (TObject* obj : *treeList) {
      TString st = obj->GetName();
      if (st.Index(v0Re) != kNPOS) {
        v0Name = st;
      } else if (st.Index(trkExtraRe) != kNPOS) {
        trkExtraName = st;
      } else if (st.Index(trackQARe) != kNPOS) {
        hasTrackQA = true;
        trackQAName = st;
      }
    }

    // Certain order needed in order to populate vectors of skipped entries
    auto v0Entry = (TObject*)treeList->FindObject(v0Name);
    treeList->Remove(v0Entry);
    treeList->AddFirst(v0Entry);

    // Prepare maps for track skimming
    auto trackExtraTree = (TTree*)inputFile->Get(Form("%s/%s", dfName, trkExtraName.Data()));
    if (trackExtraTree == nullptr) {
      printf("%s table not found\n", trkExtraName.Data());
      exitCode = 6;
      break;
    }
    auto track_iu = (TTree*)inputFile->Get(Form("%s/%s", dfName, "O2track_iu"));
    if (track_iu == nullptr) {
      printf("O2track_iu table not found\n");
      exitCode = 7;
      break;
    }
    auto v0s = (TTree*)inputFile->Get(Form("%s/%s", dfName, v0Name.Data()));
    if (v0s == nullptr) {
      printf("%s table not found\n", v0Name.Data());
      exitCode = 8;
      break;
    }
    // TrackQA
    TTree* trackQA;
    if (hasTrackQA && (trackQA = (TTree*)inputFile->Get(Form("%s/%s", dfName, trackQAName.Data()))) == nullptr) {
      exitCode = 20;
      break;
    }

    // We need to loop over the V0s once and flag the prong indices
    int trackIdxPos = 0, trackIdxNeg = 0;
    std::vector<bool> keepV0s(trackExtraTree->GetEntries(), false);
    v0s->SetBranchAddress("fIndexTracks_Pos", &trackIdxPos);
    v0s->SetBranchAddress("fIndexTracks_Neg", &trackIdxNeg);
    auto nV0s = v0s->GetEntriesFast();
    for (int i{0}; i < nV0s; ++i) {
      v0s->GetEntry(i);
      keepV0s[trackIdxPos] = true;
      keepV0s[trackIdxNeg] = true;
    }

    // TrackQA
    std::vector<bool> keepTrackQA;
    if (hasTrackQA) {
      keepTrackQA.assign(trackExtraTree->GetEntries(), false);
      trackQA->SetBranchAddress("fIndexTracks", &trackIdxPos);
      for (int i{0}; i < trackQA->GetEntries(); ++i) {
        trackQA->GetEntry(i);
        keepTrackQA[trackIdxPos] = true;
      }
    }

    std::vector<int> acceptedTracks(trackExtraTree->GetEntries(), -1);
    std::vector<bool> hasCollision(trackExtraTree->GetEntries(), false);

    uint8_t tpcNClsFindable = 0;
    bool bTPClsFindable = false;
    uint8_t ITSClusterMap = 0;
    UInt_t ITSClusterSizes = 0;
    bool bITSClusterMap = false;
    bool bITSClusterSizes = false;
    uint8_t TRDPattern = 0;
    bool bTRDPattern = false;
    float_t TOFChi2 = 0;
    bool bTOFChi2 = false;

    // Test if track properties exist
    TBranch* br = nullptr;
    TIter next(trackExtraTree->GetListOfBranches());
    while ((br = (TBranch*)next())) {
      TString brName = br->GetName();
      if (brName == "fTPCNClsFindable") {
        trackExtraTree->SetBranchAddress("fTPCNClsFindable", &tpcNClsFindable);
        bTPClsFindable = true;
      } else if (brName == "fITSClusterMap") {
        trackExtraTree->SetBranchAddress("fITSClusterMap", &ITSClusterMap);
        bITSClusterMap = true;
      } else if (brName == "fITSClusterSizes") {
        trackExtraTree->SetBranchAddress("fITSClusterSizes", &ITSClusterSizes);
        bITSClusterSizes = true;
      } else if (brName == "fTRDPattern") {
        trackExtraTree->SetBranchAddress("fTRDPattern", &TRDPattern);
        bTRDPattern = true;
      } else if (brName == "fTOFChi2") {
        trackExtraTree->SetBranchAddress("fTOFChi2", &TOFChi2);
        bTOFChi2 = true;
      }
    }

    // Sanity-Check
    // If any (%ITSClusterMap or %ITSClusterSizes) of these are not found, continuation is not possible, hence fataling
    if (!bTPClsFindable || !bTRDPattern || !bTOFChi2 ||
        (!bITSClusterMap && !bITSClusterSizes)) {
      printf("    *** FATAL *** Branch detection failed in %s for trackextra.[(fITSClusterMap=%d,fITSClusterSizes=%d),fTPCNClsFindable=%d,fTRDPattern=%d,fTOFChi2=%d]\n", dfName, bITSClusterMap, bITSClusterSizes, bTPClsFindable, bTRDPattern, bTOFChi2);
      exitCode = 10;
      break;
    }

    int fIndexCollisions = 0;
    track_iu->SetBranchAddress("fIndexCollisions", &fIndexCollisions);

    // loop over all tracks
    auto entries = trackExtraTree->GetEntries();
    int counter = 0;
    for (int i = 0; i < entries; i++) {
      trackExtraTree->GetEntry(i);
      track_iu->GetEntry(i);

      // Flag collisions
      hasCollision[i] = (fIndexCollisions >= 0);

      // Remove TPC only tracks, if they are not assoc. to a V0
      if (tpcNClsFindable > 0 && TRDPattern == 0 && TOFChi2 < -1. &&
          (!bITSClusterMap || ITSClusterMap == 0) &&
          (!bITSClusterSizes || ITSClusterSizes == 0) &&
          (!hasTrackQA || !keepTrackQA[i]) &&
          !keepV0s[i]) {
        counter++;
      } else {
        acceptedTracks[i] = i - counter;
      }
    }

    for (auto key2 : *treeList) {
      TString treeName = ((TObjString*)key2)->GetString().Data();

      // Connect trees but do not copy entries (using the clone function)
      // NOTE Basket size etc. are copied in CloneTree()
      if (outputDir == nullptr) {
        outputDir = outputFile->mkdir(dfName);
        printf("Writing to output folder %s\n", dfName);
      }
      outputDir->cd();

      auto inputTree = (TTree*)inputFile->Get(Form("%s/%s", dfName, treeName.Data()));
      printf("    Processing tree %s with %lld entries with total size %lld\n", treeName.Data(), inputTree->GetEntries(), inputTree->GetTotBytes());
      auto outputTree = inputTree->CloneTree(0);
      outputTree->SetAutoFlush(0);

      std::vector<int*> indexList;
      std::vector<char*> vlaPointers;
      std::vector<int*> indexPointers;
      TObjArray* branches = inputTree->GetListOfBranches();
      for (int i = 0; i < branches->GetEntriesFast(); ++i) {
        TBranch* br = (TBranch*)branches->UncheckedAt(i);
        TString branchName(br->GetName());
        TString tableName(getTableName(branchName, treeName.Data()));
        // register index of track index ONLY
        if (!tableName.EqualTo("O2track")) {
          continue;
        }
        // detect VLA
        if (((TLeaf*)br->GetListOfLeaves()->First())->GetLeafCount() != nullptr) {
          printf("  *** FATAL ***: VLA detection is not supported\n");
          exitCode = 9;
        } else if (branchName.BeginsWith("fIndexSlice")) {
          int* buffer = new int[2];
          memset(buffer, 0, 2 * sizeof(buffer[0]));
          vlaPointers.push_back(reinterpret_cast<char*>(buffer));
          inputTree->SetBranchAddress(br->GetName(), buffer);
          outputTree->SetBranchAddress(br->GetName(), buffer);

          indexList.push_back(buffer);
          indexList.push_back(buffer + 1);
        } else if (branchName.BeginsWith("fIndex") && !branchName.EndsWith("_size")) {
          int* buffer = new int;
          *buffer = 0;
          indexPointers.push_back(buffer);

          inputTree->SetBranchAddress(br->GetName(), buffer);
          outputTree->SetBranchAddress(br->GetName(), buffer);

          indexList.push_back(buffer);
        }
      }

      const bool processingTracked = treeName.BeginsWith("O2tracked");
      const bool processingTrackQA = treeName.BeginsWith("O2trackqa");
      const bool processingTracks = treeName.BeginsWith("O2track") && !processingTracked && !processingTrackQA; // matches any of the track tables and not tracked{v0s,cascase,3body} or trackqa;
      const bool processingAmbiguousTracks = treeName.BeginsWith("O2ambiguoustrack");

      auto entries = inputTree->GetEntries();
      for (int i = 0; i < entries; i++) {
        inputTree->GetEntry(i);
        bool fillThisEntry = true;
        // Special case for Tracks, TracksExtra, TracksCov
        if (processingTracks) {
          if (acceptedTracks[i] < 0) {
            fillThisEntry = false;
          }
        } else {
          // Other table than Tracks* --> reassign indices to Tracks
          for (const auto& idx : indexList) {
            int oldTrackIndex = *idx;

            // if negative, the index is unassigned.
            if (oldTrackIndex >= 0) {
              if (acceptedTracks[oldTrackIndex] < 0) {
                fillThisEntry = false;
              } else {
                *idx = acceptedTracks[oldTrackIndex];
              }
            }
          }
        }

        // Keep only tracks which have no collision, see O2-3601
        if (processingAmbiguousTracks) {
          if (hasCollision[i]) {
            fillThisEntry = false;
          }
        }

        if (fillThisEntry) {
          outputTree->Fill();
        }
      }

      if (entries != outputTree->GetEntries()) {
        printf("      Reduced from %lld to %lld entries\n", entries, outputTree->GetEntries());
        // sanity check by hardcoding the trees for which we expect a reduction
        const TString tableName{removeVersionSuffix(outputTree->GetName())};
        static const std::array<TString, 4> checkNames{"O2track", "O2trackextra", "O2trackcov", "O2ambiguoustrack"}; // O2track -> O2track_iu; O2trackcov -> O2trackcov_iu
        std::vector<bool> checks(checkNames.size(), false);
        for (size_t i{0}; i < checkNames.size(); ++i) {
          if (tableName.EqualTo(checkNames[i])) {
            checks[i] = true;
          }
        }
        if (std::none_of(checks.begin(), checks.end(), [](bool b) { return b; })) {
          exitCode = 30;
          printf("       -> Reduction is not expected for this tree!\n");
          break;
        }
      }

      delete inputTree;

      for (auto& buffer : indexPointers) {
        delete buffer;
      }
      for (auto& buffer : vlaPointers) {
        delete[] buffer;
      }

      outputDir->cd();
      outputTree->Write();
      delete outputTree;
    }
    if (exitCode > 0) {
      break;
    }

    outputDir = nullptr;
  }
  inputFile->Close();

  outputFile->Write();
  outputFile->Close();

  // in case of failure, remove the incomplete file
  if (exitCode != 0) {
    printf("Removing incomplete output file %s.\n", outputFile->GetName());
    gSystem->Unlink(outputFile->GetName());
    return exitCode; // skip output below
  }

  clock.Stop();

  // Report savings
  auto sBefore = inputFile->GetSize();
  auto sAfter = outputFile->GetSize();
  if (sBefore <= 0 || sAfter <= 0) {
    printf("Warning: Empty input or output file after thinning!\n");
    exitCode = 9;
  }
  auto spaceSaving = (1 - ((double)sAfter) / ((double)sBefore)) * 100;
  printf("Stats: After=%lld / Before=%lld Bytes ---> Saving %.1f%% diskspace!\n", sAfter, sBefore, spaceSaving);
  printf("Timing: CPU=%.2f (s);   Real=%.2f (s)\n", clock.CpuTime(), clock.RealTime());
  printf("End of AOD thinning.\n");

  return exitCode;
}
