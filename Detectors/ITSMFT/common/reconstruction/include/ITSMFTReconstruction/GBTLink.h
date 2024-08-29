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

/// \file GBTLink.h
/// \brief Declarations of helper classes for the ITS/MFT raw data decoding

#ifndef _ALICEO2_ITSMFT_GBTLINK_H_
#define _ALICEO2_ITSMFT_GBTLINK_H_

#define _RAW_READER_ERROR_CHECKS_ // comment this to disable error checking

#include <string>
#include <memory>
#include <gsl/gsl>
#include "ITSMFTReconstruction/PayLoadCont.h"
#include "ITSMFTReconstruction/PayLoadSG.h"
#include "ITSMFTReconstruction/GBTWord.h"
#include "ITSMFTReconstruction/RUDecodeData.h"
#include "ITSMFTReconstruction/DecodingStat.h"
#include "ITSMFTReconstruction/RUInfo.h"
#include "DataFormatsITSMFT/PhysTrigger.h"
#include "Headers/RAWDataHeader.h"
#include "DetectorsRaw/RDHUtils.h"
#include "CommonDataFormat/InteractionRecord.h"

#define GBTLINK_DECODE_ERRORCHECK(errRes, errEval)                            \
  errRes = errEval;                                                           \
  if ((errRes)&uint8_t(ErrorPrinted)) {                                       \
    ruPtr->linkHBFToDump[(uint64_t(subSpec) << 32) + hbfEntry] = irHBF.orbit; \
    errRes &= ~uint8_t(ErrorPrinted);                                         \
  }                                                                           \
  if ((errRes)&uint8_t(Abort)) {                                              \
    discardData();                                                            \
    return (status = AbortedOnError);                                         \
  }

namespace o2
{
namespace itsmft
{

struct RUDecodeData; // forward declaration to allow its linking in the GBTlink
struct GBTHeader;
struct GBTTrailer;
struct GBTTrigger;

/// support for the GBT single link data
struct GBTLink {

  enum Format : int8_t { OldFormat,
                         NewFormat,
                         NFormats };

  enum RawDataDumps : int { DUMP_NONE, // no raw data dumps on error
                            DUMP_HBF,  // dump HBF for FEEID with error
                            DUMP_TF,   // dump whole TF at error
                            DUMP_NTYPES };

  enum CollectedDataStatus : int8_t { None,
                                      AbortedOnError,
                                      StoppedOnEndOfData,
                                      DataSeen,
                                      Recovery,
                                      CachedDataExist }; // None is set before starting collectROFCableData

  enum ErrorType : uint8_t { NoError = 0x0,
                             Warning = 0x1,
                             Skip = 0x2,
                             Abort = 0x4,
                             ErrorPrinted = 0x1 << 7 };

  enum Verbosity : int8_t { Silent = -1,
                            VerboseErrors,
                            VerboseHeaders,
                            VerboseData,
                            VerboseRawDump };

  using RDH = o2::header::RDHAny;
  using RDHUtils = o2::raw::RDHUtils;
  static constexpr int CRUPageAlignment = 16; // use such alignment (in bytes) for CRU pages
  CollectedDataStatus status = None;
  CollectedDataStatus statusInTF = None; // this link was seen or not in the TF or its data were exhausted

  Verbosity verbosity = VerboseErrors;
  std::vector<PhysTrigger>* extTrigVec = nullptr;
  uint8_t idInRU = 0;     // link ID within the RU
  uint8_t idInCRU = 0;    // link ID within the CRU
  uint8_t endPointID = 0; // endpoint ID of the CRU
  bool gbtErrStatUpadated = false;
  uint16_t cruID = 0;     // CRU ID
  uint16_t feeID = 0;     // FEE ID
  uint16_t channelID = 0; // channel ID in the reader input
  uint16_t lastPageSize = 0; // size of the last CRU page
  uint32_t lanes = 0;     // lanes served by this link
  uint32_t subSpec = 0;   // link subspec
  // RS do we need this >> ? // Legacy from old data format encoder
  int nTriggers = 0;    // number of triggers loaded (the last one might be incomplete)
  // << ?

  PayLoadCont data; // data buffer used for encoding

  // transient data filled from current RDH
  int wordLength = o2::itsmft::GBTPaddedWordLength; // padded (16bytes) vs non-padded (10bytes) words
  bool alwaysParseTrigger = false;
  bool expectPadding = true;
  bool rofJumpWasSeen = false; // this link had jump in ROF IR
  uint32_t lanesActive = 0;   // lanes declared by the payload header
  uint32_t lanesStop = 0;     // lanes received stop in the payload trailer
  uint32_t lanesTimeOut = 0;  // lanes received timeout
  uint32_t lanesWithData = 0; // lanes with data transmitted
  int32_t packetCounter = -1; // current packet counter from RDH (RDH.packetCounter)
  uint32_t trigger = 0;       // trigger word
  uint32_t errorBits = 0;     // bits of the error code of last frame decoding (if any)
  uint32_t hbfEntry = 0;      // entry of the current HBF page in the rawData SG list
  const RDH* lastRDH = nullptr;
  const PayLoadSG::SGPiece* currRawPiece = nullptr;
  o2::InteractionRecord ir;       // interaction record of the ROF
  o2::InteractionRecord irHBF;    // interaction record of the HBF
  GBTLinkDecodingStat statistics; // link decoding statistics
  ChipStat chipStat;              // chip decoding statistics
  RUDecodeData* ruPtr = nullptr;  // pointer on the parent RU

  PayLoadSG rawData;         // scatter-gatter buffer for cached CRU pages, each starting with RDH
  size_t dataOffset = 0;     //
  //------------------------------------------------------------------------

  GBTLink() = default;
  GBTLink(uint16_t _cru, uint16_t _fee, uint8_t _ep, uint8_t _idInCru = 0, uint16_t _chan = 0);
  std::string describe() const;
  void clear(bool resetStat = true, bool resetTFRaw = false);

  template <class Mapping>
  CollectedDataStatus collectROFCableData(const Mapping& chmap);

  void cacheData(const void* ptr, size_t sz)
  {
    rawData.add(reinterpret_cast<const PayLoadSG::DataType*>(ptr), sz);
    if (verbosity >= VerboseRawDump) {

      LOGP(info, "Caching new RDH block for {}", describe());
      const auto* rdh = reinterpret_cast<const RDH*>(ptr);
      if (!rdh) {
        return;
      }
      RDHUtils::printRDH(rdh);
      long szd = RDHUtils::getMemorySize(*rdh);
      long offs = sizeof(RDH);
      char* ptrR = ((char*)ptr) + sizeof(RDH);
      while (offs + wordLength <= szd) {
        const o2::itsmft::GBTWord* w = reinterpret_cast<const o2::itsmft::GBTWord*>(ptrR);
        std::string com = fmt::format(" | FeeID:{:#06x} offs: {:6} ", feeID, offs);
        if (w->isData()) {
          com += "data word";
        } else if (w->isDataHeader()) {
          com += "data header";
        } else if (w->isDataTrailer()) {
          com += "data trailer";
        } else if (w->isTriggerWord()) {
          com += "trigger word";
        } else if (w->isDiagnosticWord()) {
          com += "diag word";
        } else if (w->isCalibrationWord()) {
          com += "calib word";
          com += fmt::format(" #{}", ((const GBTCalibration*)w)->calibCounter);
        } else if (w->isCableDiagnostic()) {
          com += "cable diag word";
        } else if (w->isStatus()) {
          com += "status word";
        }
        w->printX(expectPadding, com);
        offs += wordLength;
        ptrR += wordLength;
      }
    }
  }

  bool needToPrintError(uint32_t count) { return verbosity == Silent ? false : (verbosity > VerboseErrors || count == 1); }
  void accountLinkRecovery(o2::InteractionRecord ir);

 private:
  void discardData() { rawData.setDone(); }
  void printTrigger(const GBTTrigger* gbtTrg, int offs);
  void printHeader(const GBTDataHeader* gbtH, int offs);
  void printHeader(const GBTDataHeaderL* gbtH, int offs);
  void printTrailer(const GBTDataTrailer* gbtT, int offs);
  void printDiagnostic(const GBTDiagnostic* gbtD, int offs);
  void printCableDiagnostic(const GBTCableDiagnostic* gbtD);
  void printCalibrationWord(const GBTCalibration* gbtCal, int offs);
  void printCableStatus(const GBTCableStatus* gbtS);
  bool nextCRUPage();

  bool isAlignmentPadding()
  {
    if ((!expectPadding) &&                         // page alignment padding is expected only for GBT words w/o padding
        (currRawPiece->data[dataOffset] == 0xff) && //
        (dataOffset + CRUPageAlignment >= lastPageSize)) {
      return (((dataOffset + GBTWordLength) <= lastPageSize) && currRawPiece->data[dataOffset + GBTWordLength - 1] != 0xff) ? false : true;
    }
    return false;
  }

#ifndef _RAW_READER_ERROR_CHECKS_ // define dummy inline check methods, will be compiled out
  bool checkErrorsRDH(const RDH& rdh) const
  {
    return true;
  }
  uint8_t checkErrorsAlignmentPadding() const { return NoError; }
  uint8_t checkErrorsRDHStop(const RDH& rdh) const { return NoError; }
  uint8_t checkErrorsRDHStopPageEmpty(const RDH& rdh) const { return NoError; }
  uint8_t checkErrorsTriggerWord(const GBTTrigger* gbtTrg) const { return NoError; }
  uint8_t checkErrorsHeaderWord(const GBTDataHeader* gbtH) const { return NoError; }
  uint8_t checkErrorsHeaderWord(const GBTDataHeaderL* gbtH) const { return NoError; }
  uint8_t checkErrorsActiveLanes(int cables) const { return NoError; }
  uint8_t checkErrorsGBTData(int cablePos) const { return NoError; }
  uint8_t checkErrorsTrailerWord(const GBTDataTrailer* gbtT) const { return NoError; }
  uint8_t checkErrorsPacketDoneMissing(const GBTDataTrailer* gbtT, bool notEnd) const { return NoError; }
  uint8_t checkErrorsLanesStops() const { return NoError; }
  uint8_t checkErrorsDiagnosticWord(const GBTDiagnostic* gbtD) const { return NoError; }
  uint8_t checkErrorsCalibrationWord(const GBTCalibration* gbtCal) const { return NoError; }
  uint8_t checkErrorsCableID(const GBTData* gbtD, uint8_t cableSW) const { return NoError; }
  uint8_t checkErrorsIRNotExtracted() const { return NoError; }

#else
  uint8_t checkErrorsAlignmentPadding();
  uint8_t checkErrorsRDH(const RDH& rdh);
  uint8_t checkErrorsRDHStop(const RDH& rdh);
  uint8_t checkErrorsRDHStopPageEmpty(const RDH& rdh);
  uint8_t checkErrorsTriggerWord(const GBTTrigger* gbtTrg);
  uint8_t checkErrorsHeaderWord(const GBTDataHeader* gbtH);
  uint8_t checkErrorsHeaderWord(const GBTDataHeaderL* gbtH);
  uint8_t checkErrorsActiveLanes(int cables);
  uint8_t checkErrorsGBTData(int cablePos);
  uint8_t checkErrorsTrailerWord(const GBTDataTrailer* gbtT);
  uint8_t checkErrorsPacketDoneMissing(const GBTDataTrailer* gbtT, bool notEnd);
  uint8_t checkErrorsLanesStops();
  uint8_t checkErrorsDiagnosticWord(const GBTDiagnostic* gbtD);
  uint8_t checkErrorsCalibrationWord(const GBTCalibration* gbtCal);
  uint8_t checkErrorsCableID(const GBTData* gbtD, uint8_t cableSW);
  uint8_t checkErrorsIRNotExtracted();

#endif
  uint8_t checkErrorsGBTDataID(const GBTData* dbtD);

  ClassDefNV(GBTLink, 1);
};

///_________________________________________________________________
/// collect cables data for single ROF, return number of real payload words seen,
/// -1 in case of critical error
template <class Mapping>
GBTLink::CollectedDataStatus GBTLink::collectROFCableData(const Mapping& chmap)
{
  status = None;
  if (rofJumpWasSeen) { // make sure this link does not have yet unused data due to the ROF/HBF jump
    status = CachedDataExist;
    return status;
  }
  currRawPiece = rawData.currentPiece();
  uint8_t errRes = uint8_t(GBTLink::NoError);
  bool expectPacketDone = false;
  ir.clear();
  while (currRawPiece) { // we may loop over multiple CRU page
    if (dataOffset >= currRawPiece->size) {
      dataOffset = 0;                              // start of the RDH
      if (!(currRawPiece = rawData.nextPiece())) { // fetch next CRU page
        break;                                     // Data chunk (TF?) is done
      }
    }
    if (!dataOffset) { // here we always start with the RDH
      auto hbfEntrySav = hbfEntry;
      hbfEntry = 0xffffffff; // in case of problems with RDH, dump full TF
      const auto* rdh = reinterpret_cast<const RDH*>(&currRawPiece->data[dataOffset]);
      if (verbosity >= VerboseHeaders) {
        RDHUtils::printRDH(rdh);
      }
      GBTLINK_DECODE_ERRORCHECK(errRes, checkErrorsRDH(*rdh)); // make sure we are dealing with RDH
      hbfEntry = hbfEntrySav; // critical check of RDH passed
      lastRDH = rdh;
      statistics.nPackets++;
      if (RDHUtils::getPageCounter(*rdh) == 0 || irHBF.isDummy()) { // for the threshold scan data it is not guaranteed that the page0 is found)
        irHBF = RDHUtils::getHeartBeatIR(*rdh);
        hbfEntry = rawData.currentPieceID();
      }
      GBTLINK_DECODE_ERRORCHECK(errRes, checkErrorsRDHStop(*rdh)); // if new HB starts, the lastRDH must have stop
      //      GBTLINK_DECODE_ERRORCHECK(checkErrorsRDHStopPageEmpty(*rdh)); // end of HBF should be an empty page with stop
      dataOffset += sizeof(RDH);
      lastPageSize = RDHUtils::getMemorySize(*rdh);
      if (lastPageSize == sizeof(RDH)) {
        continue; // filter out empty page
      }
      if (RDHUtils::getStop(*rdh)) { // only diagnostic word can be present after the stop
        auto gbtDiag = reinterpret_cast<const GBTDiagnostic*>(&currRawPiece->data[dataOffset]);
        if (verbosity >= VerboseHeaders) {
          printDiagnostic(gbtDiag, dataOffset);
        }
        GBTLINK_DECODE_ERRORCHECK(errRes, checkErrorsDiagnosticWord(gbtDiag));
        dataOffset += RDHUtils::getOffsetToNext(*rdh) - sizeof(RDH);
        continue;
      }

      // data must start with the GBTHeader
      auto gbtH = reinterpret_cast<const GBTDataHeader*>(&currRawPiece->data[dataOffset]); // process GBT header
      if (verbosity >= VerboseHeaders) {
        printHeader(gbtH, dataOffset);
      }
      dataOffset += wordLength;
      GBTLINK_DECODE_ERRORCHECK(errRes, checkErrorsHeaderWord(gbtH));
      lanesActive = gbtH->activeLanes; // TODO do we need to update this for every page?

      GBTLINK_DECODE_ERRORCHECK(errRes, checkErrorsActiveLanes(chmap.getCablesOnRUType(ruPtr->ruInfo->ruType)));

      continue;
    }
    bool cruPageAlignmentPaddingSeen = false;

    // then we expect GBT trigger word
    {
      int ntrig = 0;
      const GBTTrigger* gbtTrg = nullptr;
      while (dataOffset < currRawPiece->size) { // we may have multiple trigger words in case there were physics triggers
        const GBTTrigger* gbtTrgTmp = reinterpret_cast<const GBTTrigger*>(&currRawPiece->data[dataOffset]);
        if (gbtTrgTmp->isTriggerWord()) {
          ntrig++;
          if (verbosity >= VerboseHeaders) {
            printTrigger(gbtTrgTmp, dataOffset);
          }
          dataOffset += wordLength;
          if (gbtTrgTmp->noData == 0 || gbtTrgTmp->internal) {
            gbtTrg = gbtTrgTmp; // this is a trigger describing the following data
          } else {
            if (extTrigVec) { // this link collects external triggers
              extTrigVec->emplace_back(PhysTrigger{o2::InteractionRecord(uint16_t(gbtTrgTmp->bc), uint32_t(gbtTrgTmp->orbit)), uint64_t(gbtTrgTmp->triggerType)});
            }
          }
          if (gbtTrgTmp->internal == 0) { // external trigger, may have others
            continue;
          }
        }
        auto gbtC = reinterpret_cast<const o2::itsmft::GBTCalibration*>(&currRawPiece->data[dataOffset]);
        if (gbtC->isCalibrationWord()) {
          if (verbosity >= VerboseHeaders) {
            printCalibrationWord(gbtC, dataOffset);
          }
          dataOffset += wordLength;
          LOGP(debug, "SetCalibData for RU:{} at bc:{}/orb:{} : [{}/{}]", ruPtr->ruSWID, gbtTrg ? gbtTrg->bc : -1, gbtTrg ? gbtTrg->orbit : -1, gbtC->calibCounter, gbtC->calibUserField);
          ruPtr->calibData = {gbtC->calibCounter, gbtC->calibUserField};
          continue;
        }
        break;
      }
      if (gbtTrg) {
        if (!gbtTrg->continuation || alwaysParseTrigger) { // this is a continuation from the previous CRU page
          statistics.nTriggers += gbtTrg->continuation == 0;
          ir.bc = gbtTrg->bc;
          ir.orbit = gbtTrg->orbit;
          trigger = gbtTrg->triggerType;
          lanesStop = 0;
          lanesWithData = 0;
        }
        if (gbtTrg->noData) {
          if (verbosity >= VerboseHeaders) {
            LOGP(info, "Offs {} Returning with status {} for {}", dataOffset, int(status), describe());
          }
          return status;
        }
      }
      if (dataOffset >= currRawPiece->size || (cruPageAlignmentPaddingSeen = isAlignmentPadding())) { // end of CRU page was reached while scanning triggers
        if (cruPageAlignmentPaddingSeen) {
          // GBTLINK_DECODE_ERRORCHECK(errRes, checkErrorsAlignmentPadding());
          dataOffset = lastPageSize;
        }
        if (verbosity >= VerboseHeaders) {
          LOGP(info, "Offs {} End of the CRU page reached while scanning triggers, continue to next page, {}", dataOffset, int(status), describe());
        }
        continue;
      }
      // trigger is supposed to be seen
      GBTLINK_DECODE_ERRORCHECK(errRes, checkErrorsIRNotExtracted());
    }

    auto gbtD = reinterpret_cast<const o2::itsmft::GBTData*>(&currRawPiece->data[dataOffset]);
    expectPacketDone = true;

    while (!gbtD->isDataTrailer() && !(cruPageAlignmentPaddingSeen = isAlignmentPadding())) { // start reading real payload
      if ((verbosity % VerboseData) == 0) {
        gbtD->printX(expectPadding);
      }
      GBTLINK_DECODE_ERRORCHECK(errRes, checkErrorsGBTDataID(gbtD));
      if (errRes != uint8_t(GBTLink::Skip)) {
        int cableHW = gbtD->getCableID(), cableSW = chmap.cableHW2SW(ruPtr->ruInfo->ruType, cableHW);
        GBTLINK_DECODE_ERRORCHECK(errRes, checkErrorsCableID(gbtD, cableSW));
        if (errRes != uint8_t(GBTLink::Skip)) {
          // GBTLINK_DECODE_ERRORCHECK(errRes, checkErrorsGBTData(chmap.cableHW2Pos(ruPtr->ruInfo->ruType, cableHW)));
          ruPtr->cableData[cableSW].add(gbtD->getW8(), 9);
          ruPtr->cableHWID[cableSW] = cableHW;
          ruPtr->cableLinkID[cableSW] = idInRU;
          ruPtr->cableLinkPtr[cableSW] = this;
        }
      }
      dataOffset += wordLength;
      gbtD = reinterpret_cast<const o2::itsmft::GBTData*>(&currRawPiece->data[dataOffset]);
    } // we are at the trailer, packet is over, check if there are more data on the next page
    if (cruPageAlignmentPaddingSeen) {
      // GBTLINK_DECODE_ERRORCHECK(errRes, checkErrorsAlignmentPadding());
      dataOffset = lastPageSize;
    } else {
      auto gbtT = reinterpret_cast<const o2::itsmft::GBTDataTrailer*>(&currRawPiece->data[dataOffset]); // process GBT trailer
      if (verbosity >= VerboseHeaders) {
        printTrailer(gbtT, dataOffset);
      }
      dataOffset += wordLength;

      GBTLINK_DECODE_ERRORCHECK(errRes, checkErrorsTrailerWord(gbtT));
      // we finished the GBT page, but there might be continuation on the next CRU page
      if (!gbtT->packetDone) {
        GBTLINK_DECODE_ERRORCHECK(errRes, checkErrorsPacketDoneMissing(gbtT, (dataOffset < currRawPiece->size && !isAlignmentPadding())));
        continue; // keep reading next CRU page
      }
      // accumulate packet states
      statistics.packetStates[gbtT->getPacketState()]++;
      if (verbosity >= VerboseHeaders) {
        LOGP(info, "Offs {} Leaving collectROFCableData for {} with DataSeen", dataOffset, describe());
      }
    }
    return (status = DataSeen);
  }

  if (expectPacketDone) { // no trailer with packet done was encountered, register error
    GBTLINK_DECODE_ERRORCHECK(errRes, checkErrorsPacketDoneMissing(nullptr, false));
    return (status = DataSeen);
  }
  return (status = StoppedOnEndOfData);
}

} // namespace itsmft
} // namespace o2

#endif // _ALICEO2_ITSMFT_GBTLINK_H_
