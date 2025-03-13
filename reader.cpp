#include <TChain.h>
#include <TF1.h>
#include <TFile.h>
#include <TH1.h>
#include <TH2.h>
#include <TROOT.h>
#include <TSystem.h>
#include <TTree.h>

#include <chrono>
#include <filesystem>
#include <iostream>
#include <mutex>
#include <nlohmann/json.hpp>
#include <string>
#include <thread>
#include <vector>

#include "TChSettings.hpp"
#include "TEventData.hpp"

std::vector<std::string> GetFileList(const std::string dirName)
{
  std::vector<std::string> fileList;

  auto searchKey = Form("events_t");
  for (const auto &entry : std::filesystem::directory_iterator(dirName)) {
    if (entry.path().string().find(searchKey) != std::string::npos) {
      fileList.push_back(entry.path().string());
    }
  }

  return fileList;
}

TH2D *histTime[17];
TH1D *histSiMultiplicty;
TH1D *histGammaMultiplicity;
TH1D *histNeutronMultiplicity;
void InitHists()
{
  for (auto i = 0; i < 17; i++) {
    histTime[i] =
        new TH2D(Form("histTime_%d", i),
                 Form("Time difference ID%02d and other detectors", i), 20000,
                 -1000, 1000, 151, -0.5, 150.5);
    histTime[i]->SetXTitle("[ns]");
    histTime[i]->SetYTitle("Detector ID");
  }

  histSiMultiplicty =
      new TH1D("histSiMultiplicity", "Si Multiplicity", 33, -0.5, 32.5);
  histSiMultiplicty->SetXTitle("Multiplicity");

  histGammaMultiplicity =
      new TH1D("histGammaMultiplicity", "Gamma Multiplicity", 48, -0.5, 47.5);
  histGammaMultiplicity->SetXTitle("Multiplicity");

  histNeutronMultiplicity = new TH1D("histNeutronMultiplicity",
                                     "Neutron Multiplicity", 48, -0.5, 47.5);
  histNeutronMultiplicity->SetXTitle("Multiplicity");
}

Double_t GetCalibratedEnergy(const ChSettings_t &chSetting, const UShort_t &adc)
{
  return chSetting.p0 + chSetting.p1 * adc + chSetting.p2 * adc * adc +
         chSetting.p3 * adc * adc * adc;
}

std::mutex counterMutex;
uint64_t totalEvents = 0;
uint64_t processedEvents = 0;
std::vector<bool> IsFinished;
void AnalysisThread(TString fileName, uint32_t threadID)
{
  ROOT::EnableThreadSafety();

  counterMutex.lock();
  auto settingsFileName = "./chSettings.json";
  auto chSettingsVec = TChSettings::GetChSettings(settingsFileName);
  counterMutex.unlock();

  auto file = TFile::Open(fileName, "READ");
  if (!file) {
    std::cerr << "File not found: events.root" << std::endl;
    return;
  }
  auto tree = dynamic_cast<TTree *>(file->Get("Event_Tree"));
  if (!tree) {
    std::cerr << "Tree not found: Event_Tree" << std::endl;
    return;
  }

  bool IsFissionEvent;
  tree->SetBranchAddress("IsFissionEvent", &IsFissionEvent);
  uint8_t TriggerID;
  tree->SetBranchAddress("TriggerID", &TriggerID);
  uint8_t SiFrontMultiplicity;
  tree->SetBranchAddress("SiFrontMultiplicity", &SiFrontMultiplicity);
  uint8_t SiBackMultiplicity;
  tree->SetBranchAddress("SiBackMultiplicity", &SiBackMultiplicity);
  uint8_t SiMultiplicity;
  tree->SetBranchAddress("SiMultiplicity", &SiMultiplicity);
  uint8_t GammaMultiplicity;
  tree->SetBranchAddress("GammaMultiplicity", &GammaMultiplicity);
  uint8_t NeutronMultiplicity;
  tree->SetBranchAddress("NeutronMultiplicity", &NeutronMultiplicity);
  std::vector<uint8_t> *Module = nullptr;
  tree->SetBranchAddress("Module", &Module);
  std::vector<uint8_t> *Channel = nullptr;
  tree->SetBranchAddress("Channel", &Channel);
  std::vector<double_t> *Timestamp = nullptr;
  tree->SetBranchAddress("Timestamp", &Timestamp);
  std::vector<uint16_t> *Energy = nullptr;
  tree->SetBranchAddress("Energy", &Energy);
  std::vector<uint16_t> *EnergyShort = nullptr;
  tree->SetBranchAddress("EnergyShort", &EnergyShort);

  const auto nEntries = tree->GetEntries();
  {
    std::lock_guard<std::mutex> lock(counterMutex);
    totalEvents += nEntries;
  }

  for (auto i = 0; i < nEntries; i++) {
    tree->GetEntry(i);

    histSiMultiplicty->Fill(SiMultiplicity);

    auto triggerCh = TriggerID % 16;
    for (uint32_t j = 0; j < Module->size(); j++) {
      auto module = Module->at(j);
      auto channel = Channel->at(j);
      auto timestamp = Timestamp->at(j);
      auto energy = Energy->at(j);
      auto energyShort = EnergyShort->at(j);
      auto chSetting = chSettingsVec.at(module).at(channel);
      auto calibratedEnergy = GetCalibratedEnergy(chSetting, energy);
      auto calibratedEnergyShort = GetCalibratedEnergy(chSetting, energyShort);

      auto hitID = module * 16 + channel;
      if (module != 0) histTime[triggerCh]->Fill(timestamp, hitID);
    }

    constexpr auto nProcess = 1000;
    if (i % nProcess == 0) {
      std::lock_guard<std::mutex> lock(counterMutex);
      processedEvents += nProcess;
    }
  }

  IsFinished.at(threadID) = true;
}

void reader()
{
  ROOT::EnableThreadSafety();

  InitHists();

  auto fileList = GetFileList("./");

  auto startTime = std::chrono::high_resolution_clock::now();
  auto lastTime = startTime;

  std::vector<std::thread> threads;
  for (uint32_t i = 0; i < fileList.size(); i++) {
    threads.emplace_back(AnalysisThread, fileList.at(i), i);
    IsFinished.push_back(false);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }

  while (true) {
    if (std::all_of(IsFinished.begin(), IsFinished.end(),
                    [](bool b) { return b; })) {
      break;
    }
    auto now = std::chrono::high_resolution_clock::now();
    auto duration =
        std::chrono::duration_cast<std::chrono::milliseconds>(now - lastTime);
    if (duration.count() > 1000) {
      counterMutex.lock();
      auto finishedEvents = processedEvents;
      counterMutex.unlock();
      auto elapsed =
          std::chrono::duration_cast<std::chrono::milliseconds>(now - startTime)
              .count();
      auto remainingTime =
          (totalEvents - processedEvents) * elapsed / finishedEvents / 1.e3;

      std::cout << "\b\r" << "Processing event " << finishedEvents << " / "
                << totalEvents << ", " << int(remainingTime) << "s  \b\b"
                << std::flush;
      lastTime = now;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }

  for (auto &thread : threads) {
    thread.join();
  }

  auto endTime = std::chrono::high_resolution_clock::now();
  auto elapsed =
      std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime)
          .count();
  std::cout << "\b\r" << "Processing event " << totalEvents << " / "
            << totalEvents << ", spent " << int(elapsed / 1.e3) << "s  \b\b"
            << std::endl;

  histTime[0]->Draw("colz");
}
