#include <TCanvas.h>
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

Double_t GetCalibratedEnergy(const ChSettings_t &chSetting, const UShort_t &adc)
{
  return chSetting.p0 + chSetting.p1 * adc + chSetting.p2 * adc * adc +
         chSetting.p3 * adc * adc * adc;
}

constexpr uint32_t nModules = 9;
constexpr uint32_t nChannels = 16;
TH2D *histTime[nChannels + 1];
TH1D *histSiMultiplicty;
TH1D *histGammaMultiplicity;
TH1D *histNeutronMultiplicity;
TH1D *histADC[nModules][nChannels];
TH1D *histEnergy[nModules][nChannels];
TH2D *histADCvsTime[nModules][nChannels];
TH2D *histPSDvsTime[nModules][nChannels];
void InitHists()
{
  auto settingsFileName = "./chSettings.json";
  auto chSettingsVec = TChSettings::GetChSettings(settingsFileName);

  for (uint32_t i = 0; i < nChannels; i++) {
    histTime[i] =
        new TH2D(Form("histTime_%d", i),
                 Form("Time difference ID%02d and other detectors", i), 20000,
                 -1000, 1000, 151, -0.5, 150.5);
    histTime[i]->SetXTitle("[ns]");
    histTime[i]->SetYTitle("Detector ID");
  }
  histTime[nChannels] =
      new TH2D("histTime", "Time difference between detectors", 20000, -1000,
               1000, 151, -0.5, 150.5);

  histSiMultiplicty =
      new TH1D("histSiMultiplicity", "Si Multiplicity", 33, -0.5, 32.5);
  histSiMultiplicty->SetXTitle("Multiplicity");

  histGammaMultiplicity =
      new TH1D("histGammaMultiplicity", "Gamma Multiplicity", 48, -0.5, 47.5);
  histGammaMultiplicity->SetXTitle("Multiplicity");

  histNeutronMultiplicity = new TH1D("histNeutronMultiplicity",
                                     "Neutron Multiplicity", 48, -0.5, 47.5);
  histNeutronMultiplicity->SetXTitle("Multiplicity");

  constexpr uint32_t nBins = 32000;
  for (uint32_t i = 0; i < nModules; i++) {
    for (uint32_t j = 0; j < nChannels; j++) {
      histADC[i][j] = new TH1D(Form("histADC_%d_%d", i, j),
                               Form("Energy Module%02d Channel%02d", i, j),
                               nBins, 0.5, nBins + 0.5);
      histADC[i][j]->SetXTitle("ADC channel");
    }
  }

  for (uint32_t i = 0; i < nModules; i++) {
    for (uint32_t j = 0; j < nChannels; j++) {
      auto chSetting = chSettingsVec.at(i).at(j);
      std::array<double_t, nBins + 1> binTable;
      for (uint16_t k = 0; k < nBins + 1; k++) {
        auto currentBin = GetCalibratedEnergy(chSetting, k);
        auto nextBin = GetCalibratedEnergy(chSetting, k + 1);
        auto nextEdge = (currentBin + nextBin) / 2.;
        if (k != 0) {
          auto previousEdge = binTable.at(k - 1);
          if (nextEdge < previousEdge) {
            nextEdge = previousEdge + 0.1;
          }
        }
        binTable.at(k) = nextEdge;
      }
      histEnergy[i][j] = new TH1D(Form("histEnergy_%d_%d", i, j),
                                  Form("Energy Module%02d Channel%02d", i, j),
                                  nBins, binTable.data());
      histEnergy[i][j]->SetXTitle("Energy [keV]");
    }
  }

  for (uint32_t i = 0; i < nModules; i++) {
    for (uint32_t j = 0; j < nChannels; j++) {
      histADCvsTime[i][j] =
          new TH2D(Form("histADCvsTime_%d_%d", i, j),
                   Form("Energy vs Time Module%02d Channel%02d", i, j), 2000,
                   -1000, 1000, nBins / 10, 0.5, nBins + 0.5);
      histADCvsTime[i][j]->SetXTitle("Time [ns]");
      histADCvsTime[i][j]->SetYTitle("ADC channel");
    }
  }

  for (uint32_t i = 0; i < nModules; i++) {
    for (uint32_t j = 0; j < nChannels; j++) {
      histPSDvsTime[i][j] =
          new TH2D(Form("histPSDvsTime_%d_%d", i, j),
                   Form("PSD vs Time Module%02d Channel%02d", i, j), 2000,
                   -1000, 1000, 1000, 0, 1);
      histPSDvsTime[i][j]->SetXTitle("Time [ns]");
      histPSDvsTime[i][j]->SetYTitle("PSD");
    }
  }
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
  auto siTimeFunction = "Si_time_function.txt";
  std::vector<double_t> p0Vec;
  std::vector<double_t> p1Vec;
  std::vector<double_t> p2Vec;
  std::vector<double_t> p3Vec;
  bool isSiTimeFunction = false;
  std::ifstream ifs(siTimeFunction);
  if (!ifs) {
    std::cerr << "File not found: " << siTimeFunction << std::endl;
  } else {
    isSiTimeFunction = true;
    std::string line;
    while (std::getline(ifs, line)) {
      std::istringstream iss(line);
      int32_t ch;
      double_t p0, p1, p2, p3;
      iss >> ch >> p0 >> p1 >> p2 >> p3;
      p0Vec.push_back(p0);
      p1Vec.push_back(p1);
      p2Vec.push_back(p2);
      p3Vec.push_back(p3);
    }
  }
  ifs.close();
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
    constexpr auto nProcess = 1000;
    if (i % nProcess == 0) {
      std::lock_guard<std::mutex> lock(counterMutex);
      processedEvents += nProcess;
    }

    if (IsFissionEvent) {
      uint16_t SiEnergy = 0;
      uint16_t SiCh = 0;
      for (uint32_t j = 0; j < Module->size(); j++) {
        auto module = Module->at(j);
        auto timestamp = Timestamp->at(j);
        if (module == 0 && timestamp == 0) {
          SiCh = Channel->at(j);
          SiEnergy = Energy->at(j);
          break;
        }
      }
      double SiTime = 0;
      if (isSiTimeFunction) {
        SiTime = p0Vec.at(SiCh) + p1Vec.at(SiCh) * SiEnergy +
                 p2Vec.at(SiCh) * SiEnergy * SiEnergy +
                 p3Vec.at(SiCh) * SiEnergy * SiEnergy * SiEnergy;
      }

      histSiMultiplicty->Fill(SiMultiplicity);
      histGammaMultiplicity->Fill(GammaMultiplicity);
      histNeutronMultiplicity->Fill(NeutronMultiplicity);

      auto triggerCh = TriggerID % 16;
      for (uint32_t j = 0; j < Module->size(); j++) {
        auto module = Module->at(j);
        auto channel = Channel->at(j);
        auto timestamp = Timestamp->at(j) + SiTime;
        // auto timestamp = Timestamp->at(j);
        auto energy = Energy->at(j);
        auto energyShort = EnergyShort->at(j);
        auto chSetting = chSettingsVec.at(module).at(channel);
        auto calibratedEnergy = GetCalibratedEnergy(chSetting, energy);
        auto calibratedEnergyShort =
            GetCalibratedEnergy(chSetting, energyShort);
        auto psd = double(energy - energyShort) / double(energy);

        auto hitID = module * 16 + channel;
        // if (module != 0) {
        histTime[triggerCh]->Fill(timestamp, hitID);
        histTime[16]->Fill(timestamp, hitID);  // Sum of all
        // }

        histADC[module][channel]->Fill(energy);
        histEnergy[module][channel]->Fill(calibratedEnergy);
        histADCvsTime[module][channel]->Fill(timestamp, energy);
        histPSDvsTime[module][channel]->Fill(timestamp, psd);
      }
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
          (totalEvents - finishedEvents) * elapsed / finishedEvents / 1.e3;

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

  // Draw all histograms of ADC and Energy
  // TCanvas *cavasADC[nModules];
  // TCanvas *cavasEnergy[nModules];
  // for (uint32_t i = 0; i < nModules; i++) {
  //   cavasADC[i] =
  //       new TCanvas(Form("cavasADC_%d", i), Form("Module %02d", i), 800, 600);
  //   cavasADC[i]->Divide(4, 4);
  //   cavasEnergy[i] = new TCanvas(Form("cavasEnergy_%d", i),
  //                                Form("Module %02d", i), 800, 600);
  //   cavasEnergy[i]->Divide(4, 4);
  //   for (uint32_t j = 0; j < nChannels; j++) {
  //     cavasADC[i]->cd(j + 1);
  //     histADC[i][j]->Draw();
  //     cavasEnergy[i]->cd(j + 1);
  //     histEnergy[i][j]->Draw();
  //   }
  //   // Save as PDF
  //   cavasADC[i]->Print(Form("ADC_Module%02d.pdf", i));
  //   cavasEnergy[i]->Print(Form("Energy_Module%02d.pdf", i));
  // }

  auto canvas = new TCanvas("canvas", "canvas", 800, 600);
  histTime[16]->Draw("colz");
}
