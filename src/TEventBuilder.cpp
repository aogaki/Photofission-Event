#include "TEventBuilder.hpp"

#include <TFile.h>
#include <TTree.h>

#include <algorithm>
#include <iostream>

TEventBuilder::TEventBuilder(
    const std::string &fileName, const double_t timeWindow,
    bool onlyFissionEvents,
    const std::vector<std::vector<TChSettings>> &settings)
    : fFileName(fileName),
      fTimeWindow(timeWindow),
      fOnlyFissionEvents(onlyFissionEvents),
      fSettings(settings)
{
}

TEventBuilder::~TEventBuilder() {}

uint32_t TEventBuilder::LoadHits()
{
  fHitData.clear();

  auto file = TFile::Open(fFileName.c_str(), "READ");
  if (!file) {
    std::cout << "File not found: " << fFileName << std::endl;
    return 0;
  }
  auto tree = dynamic_cast<TTree *>(file->Get("ELIADE_Tree"));
  if (!tree) {
    std::cout << "Tree not found: " << fFileName << std::endl;
    return 0;
  }
  tree->SetBranchStatus("*", kFALSE);

  HitData_t hit;

  tree->SetBranchStatus("Ch", kTRUE);
  tree->SetBranchAddress("Ch", &hit.Channel);

  tree->SetBranchStatus("Mod", kTRUE);
  tree->SetBranchAddress("Mod", &hit.Module);

  tree->SetBranchStatus("FineTS", kTRUE);
  tree->SetBranchAddress("FineTS", &hit.Timestamp);

  tree->SetBranchStatus("ChargeLong", kTRUE);
  tree->SetBranchAddress("ChargeLong", &hit.Energy);

  tree->SetBranchStatus("ChargeShort", kTRUE);
  tree->SetBranchAddress("ChargeShort", &hit.EnergyShort);

  const auto nEntries = tree->GetEntries();
  for (auto i = 0; i < nEntries; i++) {
    tree->GetEntry(i);
    hit.Timestamp /= 1000.0;  // ps -> ns
    hit.Timestamp += fSettings.at(hit.Module).at(hit.Channel).timeOffset;
    fHitData.push_back(hit);
  }

  CheckHitData();

  file->Close();

  return fHitData.size();
}

void TEventBuilder::CheckHitData()
{
  const double_t timeOffset = (pow(2, 47) - 1);
  const auto firstTS = fHitData.at(0).Timestamp;
  const auto lastTS = fHitData.at(fHitData.size() - 1).Timestamp;
  if (lastTS - firstTS > timeOffset) {
    std::cout << "Timestamp overflow detected: " << fFileName;
    std::cout << "\nFirst timestamp: " << firstTS;
    std::cout << "\nLast timestamp: " << lastTS << std::endl;

    for (auto i = 0; i < fHitData.size() - 1; i++) {
      auto originalTS = fHitData.at(i).Timestamp;
      if (fHitData.at(i).Module == 0 || fHitData.at(i).Module == 1) {
        fHitData.at(i).Timestamp += timeOffset * 4;
      } else {
        fHitData.at(i).Timestamp += timeOffset * 2;
      }

      if (fHitData.at(i + 1).Timestamp - originalTS > timeOffset) {
        break;
      }
    }
  }

  std::sort(fHitData.begin(), fHitData.end(),
            [](const HitData_t &a, const HitData_t &b) {
              return a.Timestamp < b.Timestamp;
            });
}

HitType_t TEventBuilder::GetHitType(uint8_t module)
{
  if (module == 0) {
    return HitType::SiFront;
  } else if (module == 1) {
    return HitType::SiBack;
  } else if (module >= 2 && module <= 4) {
    return HitType::Gamma;
  } else if (module >= 5 && module <= 9) {
    return HitType::Neutron;
  } else {
    std::cout << "Unknown module: " << static_cast<int>(module) << std::endl;
  }
  return HitType::Unknown;
}

uint32_t TEventBuilder::EventBuild()
{
  if (fHitData.size() == 0) {
    std::cout << "No hits loaded." << std::endl;
    return 0;
  }

  fEventData = std::make_unique<std::vector<TEventData>>();

  const int32_t nHits = fHitData.size();
  for (auto iHit = 0; iHit < nHits; iHit++) {
    auto hit = fHitData.at(iHit);
    if (fSettings.at(hit.Module).at(hit.Channel).isEventTrigger) {
      TEventData eventData;
      eventData.IsFissionEvent = false;
      eventData.SiFrontMultiplicity = 0;
      eventData.SiBackMultiplicity = 0;
      eventData.SiMultiplicity = 0;
      eventData.GammaMultiplicity = 0;
      eventData.NeutronMultiplicity = 0;
      uint16_t frontADC = 0;
      uint16_t backADC = 0;

      auto triggerTime = hit.Timestamp;
      hit.Timestamp -= triggerTime;
      eventData.HitData.push_back(hit);
      auto hitType = GetHitType(hit.Module);
      if (hitType == HitType::SiFront) {
        eventData.SiMultiplicity++;
        eventData.SiFrontMultiplicity++;
        frontADC = std::max(frontADC, hit.Energy);
      } else if (hitType == HitType::SiBack) {
        eventData.SiMultiplicity++;
        eventData.SiBackMultiplicity++;
        backADC = std::max(backADC, hit.Energy);
      } else if (hitType == HitType::Gamma) {
        eventData.GammaMultiplicity++;
      } else if (hitType == HitType::Neutron) {
        eventData.NeutronMultiplicity++;
      }

      eventData.TriggerID = fSettings.at(hit.Module).at(hit.Channel).detectorID;
      bool fillFlag = true;

      for (auto jHit = iHit + 1; (jHit < nHits) && fillFlag; jHit++) {
        auto nextHit = fHitData.at(jHit);
        if (fSettings.at(nextHit.Module).at(nextHit.Channel).isEventTrigger) {
          auto detectorID =
              fSettings.at(nextHit.Module).at(nextHit.Channel).detectorID;
          if (detectorID > eventData.TriggerID) {
            fillFlag = false;
            break;
          }
        }

        nextHit.Timestamp -= triggerTime;
        if (nextHit.Timestamp > fTimeWindow) {
          break;
        }

        auto hitType = GetHitType(nextHit.Module);
        if (hitType == HitType::SiFront) {
          eventData.SiMultiplicity++;
          eventData.SiFrontMultiplicity++;
          frontADC = std::max(frontADC, hit.Energy);
        } else if (hitType == HitType::SiBack) {
          eventData.SiMultiplicity++;
          eventData.SiBackMultiplicity++;
          backADC = std::max(backADC, hit.Energy);
        } else if (hitType == HitType::Gamma) {
          eventData.GammaMultiplicity++;
        } else if (hitType == HitType::Neutron) {
          eventData.NeutronMultiplicity++;
        }

        eventData.HitData.push_back(nextHit);
      }

      for (auto jHit = iHit - 1; (jHit >= 0) && fillFlag; jHit--) {
        auto prevHit = fHitData.at(jHit);
        if (fSettings.at(prevHit.Module).at(prevHit.Channel).isEventTrigger) {
          auto detectorID =
              fSettings.at(prevHit.Module).at(prevHit.Channel).detectorID;
          if (detectorID > eventData.TriggerID) {
            fillFlag = false;
            break;
          }
        }

        prevHit.Timestamp -= triggerTime;
        if (prevHit.Timestamp < -fTimeWindow) {
          break;
        }

        auto hitType = GetHitType(prevHit.Module);
        if (hitType == HitType::SiFront) {
          eventData.SiMultiplicity++;
          eventData.SiFrontMultiplicity++;
          frontADC = std::max(frontADC, hit.Energy);
        } else if (hitType == HitType::SiBack) {
          eventData.SiMultiplicity++;
          eventData.SiBackMultiplicity++;
          backADC = std::max(backADC, hit.Energy);
        } else if (hitType == HitType::Gamma) {
          eventData.GammaMultiplicity++;
        } else if (hitType == HitType::Neutron) {
          eventData.NeutronMultiplicity++;
        }

        eventData.HitData.push_back(prevHit);
      }

      if (fillFlag) {
        eventData.IsFissionEvent = (eventData.SiFrontMultiplicity > 0) &&
                                   (eventData.SiBackMultiplicity > 0) &&
                                   (frontADC > 1500.0) && (backADC > 1500.0);

        if (fOnlyFissionEvents) {
          if (eventData.IsFissionEvent) {
            fEventData->push_back(eventData);
          }
        } else {
          fEventData->push_back(eventData);
        }
      }
    }
  }

  return fEventData->size();
}