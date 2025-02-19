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

#if !defined(__CLING__) || defined(__ROOTCLING__)
#include "TGeoManager.h"
#include "TString.h"
#include "TSystem.h"

#include "DetectorsPassive/Cave.h"
#include "DetectorsPassive/Magnet.h"
#include "DetectorsPassive/Dipole.h"
#include "DetectorsPassive/Compensator.h"
#include "DetectorsPassive/Absorber.h"
#include "DetectorsPassive/Shil.h"
#include "DetectorsPassive/Hall.h"
#include "DetectorsPassive/Pipe.h"
#include <Field/MagneticField.h>
#include <MFTSimulation/Detector.h>
#include <MCHSimulation/Detector.h>
#include <MIDSimulation/Detector.h>
#include <EMCALSimulation/Detector.h>
#include <TOFSimulation/Detector.h>
#include <TRDSimulation/Detector.h>
#include <FT0Simulation/Detector.h>
#include <FV0Simulation/Detector.h>
#include <FDDSimulation/Detector.h>
#include <HMPIDSimulation/Detector.h>
#include <PHOSSimulation/Detector.h>
#include <CPVSimulation/Detector.h>
#include <ZDCSimulation/Detector.h>
#include <FOCALSimulation/Detector.h>
#include <DetectorsPassive/Cave.h>
#include <DetectorsPassive/FrameStructure.h>
#include <SimConfig/SimConfig.h>
#include <FairRunSim.h>
#include <FairRootFileSink.h>
#include <fairlogger/Logger.h>
#include <algorithm>
#include "DetectorsCommonDataFormats/UpgradesStatus.h"
#include <DetectorsBase/SimFieldUtils.h>
#include <SimConfig/SimDLLoader.h>
#endif

#ifdef ENABLE_UPGRADES
#include <FT3Simulation/Detector.h>
#include <FCTSimulation/Detector.h>
#include <IOTOFSimulation/Detector.h>
#include <RICHSimulation/Detector.h>
#include <ECalSimulation/Detector.h>
#include <MI3Simulation/Detector.h>
#include <Alice3DetectorsPassive/Pipe.h>
#include <Alice3DetectorsPassive/Absorber.h>
#include <Alice3DetectorsPassive/Magnet.h>
#endif

using Return = o2::base::Detector*;

void finalize_geometry(FairRunSim* run);

bool isActivated(std::string s)
{
  // access user configuration for list of wanted modules
  auto& modulelist = o2::conf::SimConfig::Instance().getActiveModules();
  auto active = std::find(modulelist.begin(), modulelist.end(), s) != modulelist.end();
  if (active) {
    LOG(info) << "Activating " << s << " module";
  }
  return active;
}

bool isReadout(std::string s)
{
  // access user configuration for list of wanted modules
  auto& modulelist = o2::conf::SimConfig::Instance().getReadoutDetectors();
  auto active = std::find(modulelist.begin(), modulelist.end(), s) != modulelist.end();
  if (active) {
    LOG(info) << "Reading out " << s << " detector";
  }
  return active;
}

// a "factory" like macro to instantiate the O2 geometry
void build_geometry(FairRunSim* run = nullptr)
{
  bool geomonly = (run == nullptr);

  // minimal macro to test setup of the geometry
  auto& confref = o2::conf::SimConfig::Instance();

  TString dir = getenv("VMCWORKDIR");
  TString geom_dir = dir + "/Detectors/Geometry/";
  gSystem->Setenv("GEOMPATH", geom_dir.Data());

  TString tut_configdir = dir + "/Detectors/gconfig";
  gSystem->Setenv("CONFIG_DIR", tut_configdir.Data());

  // Create simulation run if it does not exist
  if (run == nullptr) {
    run = new FairRunSim();
    run->SetSink(new FairRootFileSink("foo.root")); // Output file
    run->SetName("TGeant3");                        // Transport engine
  }
  // Create media
  run->SetMaterials("media.geo"); // Materials

  // we need a field to properly init the media
  run->SetField(o2::base::SimFieldUtils::createMagField());

  // Create geometry
  // we always need the cave
  o2::passive::Cave* cave = new o2::passive::Cave("CAVE");
  // adjust size depending on content
  cave->includeZDC(isActivated("ZDC"));
#ifdef ENABLE_UPGRADES
  cave->includeRB24(!isActivated("TRK"));
#endif
  // the experiment hall (cave)
  cave->SetGeometryFileName("cave.geo");
  run->AddModule(cave);

  // the experimental hall
  if (isActivated("HALL")) {
    auto hall = new o2::passive::Hall("HALL", "Experimental Hall");
    run->AddModule(hall);
  }

  // the magnet
  if (isActivated("MAG")) {
    // the frame structure to support other detectors
    auto magnet = new o2::passive::Magnet("MAG", "L3 Magnet");
    run->AddModule(magnet);
  }

  // the dipole
  if (isActivated("DIPO")) {
    auto dipole = new o2::passive::Dipole("DIPO", "Alice Dipole");
    run->AddModule(dipole);
  }

  // the compensator dipole
  if (isActivated("COMP")) {
    run->AddModule(new o2::passive::Compensator("COMP", "Alice Compensator Dipole"));
  }

  // beam pipe
  if (isActivated("PIPE")) {
#ifdef ENABLE_UPGRADES
    if (isActivated("IT3")) {
      run->AddModule(new o2::passive::Pipe("PIPE", "Beam pipe", 1.6f, 0.05f));
    } else {
      run->AddModule(new o2::passive::Pipe("PIPE", "Beam pipe"));
    }
#else
    run->AddModule(new o2::passive::Pipe("PIPE", "Beam pipe"));
#endif
  }

#ifdef ENABLE_UPGRADES
  // upgraded beampipe at the interaction point (IP)
  if (isActivated("A3IP")) {
    run->AddModule(new o2::passive::Alice3Pipe("A3IP", "Alice 3 beam pipe", !isActivated("TRK"), !isActivated("FT3"), 1.8f, 0.08f, 1000.f, 5.6f, 0.08f, 76.f));
  }

  // the absorber
  if (isActivated("A3ABSO")) {
    run->AddModule(new o2::passive::Alice3Absorber("A3ABSO", "ALICE3 Absorber"));
  }

  // the magnet
  if (isActivated("A3MAG")) {
    run->AddModule(new o2::passive::Alice3Magnet("A3MAG", "ALICE3 Magnet"));
  }
#endif

  // the absorber
  if (isActivated("ABSO")) {
    // the frame structure to support other detectors
    run->AddModule(new o2::passive::Absorber("ABSO", "Absorber"));
  }

  // the shil
  if (isActivated("SHIL")) {
    run->AddModule(new o2::passive::Shil("SHIL", "Small angle beam shield"));
  }

  if (isActivated("TOF") || isActivated("TRD") || isActivated("FRAME")) {
    // the frame structure to support other detectors
    run->AddModule(new o2::passive::FrameStructure("FRAME", "Frame"));
  }

  std::vector<int> detId2RunningId = std::vector<int>(o2::detectors::DetID::nDetectors, -1); // a mapping of detectorId to a dense runtime index
  // used for instance to set bits in the hit structure of MCTracks; -1 means that there is no bit associated

  auto addReadoutDetector = [&detId2RunningId, &run](o2::base::Detector* detector) {
    static int runningid = 0; // this is static for constant lambda interfaces --> use fixed type and not auto in the lambda!
    run->AddModule(detector);
    if (detector->IsActive()) {
      auto detID = detector->GetDetId();
      detId2RunningId[detID] = runningid;
      LOG(info) << " DETID " << detID << " vs " << detector->GetDetId() << " mapped to hit bit index " << runningid;
      runningid++;
    }
  };

  if (isActivated("TOF")) {
    // TOF
    addReadoutDetector(new o2::tof::Detector(isReadout("TOF")));
  }

  if (isActivated("TRD")) {
    // TRD
    addReadoutDetector(new o2::trd::Detector(isReadout("TRD")));
  }

  if (isActivated("TPC")) {
    // tpc
    addReadoutDetector(o2::conf::SimDLLoader::Instance().executeFunctionAlias<Return, bool>(
      "O2TPCSimulation", "create_detector_tpc", isReadout("TPC")));
  }
#ifdef ENABLE_UPGRADES
  if (isActivated("IT3")) {
    // IT3
    addReadoutDetector(o2::conf::SimDLLoader::Instance().executeFunctionAlias<Return, const char*, bool>(
      "O2ITSSimulation", "create_detector_its", "IT3", isReadout("IT3")));
  }

  if (isActivated("TRK")) {
    // ALICE 3 TRK
    addReadoutDetector(o2::conf::SimDLLoader::Instance().executeFunctionAlias<Return, bool>(
      "O2TRKSimulation", "create_detector_trk", isReadout("TRK")));
  }

  if (isActivated("FT3")) {
    // ALICE 3 FT3
    addReadoutDetector(new o2::ft3::Detector(isReadout("FT3")));
  }

  if (isActivated("FCT")) {
    // ALICE 3 FCT
    addReadoutDetector(new o2::fct::Detector(isReadout("FCT")));
  }

  if (isActivated("TF3")) {
    // ALICE 3 tofs
    addReadoutDetector(new o2::iotof::Detector(isReadout("TF3")));
  }

  if (isActivated("RCH")) {
    // ALICE 3 RICH
    addReadoutDetector(new o2::rich::Detector(isReadout("RCH")));
  }

  if (isActivated("ECL")) {
    // ALICE 3 ECAL
    addReadoutDetector(new o2::ecal::Detector(isReadout("ECL")));
  }

  if (isActivated("MI3")) {
    // ALICE 3 MID
    addReadoutDetector(new o2::mi3::Detector(isReadout("MI3")));
  }
#endif

  if (isActivated("ITS")) {
    // its
    addReadoutDetector(o2::conf::SimDLLoader::Instance().executeFunctionAlias<Return, const char*, bool>(
      "O2ITSSimulation", "create_detector_its", "ITS", isReadout("ITS")));
  }

  if (isActivated("MFT")) {
    // mft
    addReadoutDetector(new o2::mft::Detector(isReadout("MFT")));
  }

  if (isActivated("MCH")) {
    // mch
    addReadoutDetector(new o2::mch::Detector(isReadout("MCH")));
  }

  if (isActivated("MID")) {
    // mid
    addReadoutDetector(new o2::mid::Detector(isReadout("MID")));
  }

  if (isActivated("EMC")) {
    // emcal
    addReadoutDetector(new o2::emcal::Detector(isReadout("EMC")));
  }

  if (isActivated("PHS")) {
    // phos
    addReadoutDetector(new o2::phos::Detector(isReadout("PHS")));
  }

  if (isActivated("CPV")) {
    // cpv
    addReadoutDetector(new o2::cpv::Detector(isReadout("CPV")));
  }

  if (isActivated("FT0")) {
    // FIT-T0
    addReadoutDetector(new o2::ft0::Detector(isReadout("FT0")));
  }

  if (isActivated("FV0")) {
    // FIT-V0
    addReadoutDetector(new o2::fv0::Detector(isReadout("FV0")));
  }

  if (isActivated("FDD")) {
    // FIT-FDD
    addReadoutDetector(new o2::fdd::Detector(isReadout("FDD")));
  }

  if (isActivated("HMP")) {
    // HMP
    addReadoutDetector(new o2::hmpid::Detector(isReadout("HMP")));
  }

  if (isActivated("ZDC")) {
    // ZDC
    addReadoutDetector(new o2::zdc::Detector(isReadout("ZDC")));
  }

  if (isActivated("FOC")) {
    // FOCAL
    addReadoutDetector(new o2::focal::Detector(isReadout("FOC"), gSystem->ExpandPathName("$O2_ROOT/share/Detectors/Geometry/FOC/geometryFiles/geometry_Spaghetti.txt")));
  }

  if (geomonly) {
    run->Init();
  }

  // register the DetId2HitIndex lookup with the detector class by copying the vector
  o2::base::Detector::setDetId2HitBitIndex(detId2RunningId);
}
