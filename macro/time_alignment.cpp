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

constexpr uint32_t nModules = 9;
constexpr uint32_t nChannels = 16;
TH2D *histTime;
TH1D *histTriggerADC;
void InitHists()
{
  histTime = new TH2D("histTime", "Time difference between detectors", 20000,
                      -1000, 1000, 151, -0.5, 150.5);

  histTriggerADC =
      new TH1D("histTriggerADC", "Trigger ADC", 32000, 0.5, 32000.5);
}

void FitSiHist(TH1D *hist)
{
  if (hist->GetEntries() == 0) {
    return;
  }
  auto mean = hist->GetBinCenter(hist->GetMaximumBin());
  // auto sigma = hist->GetRMS();
  auto sigma = 20;
  auto f = new TF1("f", "gaus", mean - 1 * sigma, mean + 1 * sigma);
  f->SetParameters(hist->GetMaximum(), mean, sigma);
  hist->Fit(f, "QR", "", mean - 1 * sigma, mean + 1 * sigma);

  f->SetRange(mean - 1 * sigma, mean + 1 * sigma);
  hist->Fit(f, "QR", "", mean - 1 * sigma, mean + 1 * sigma);

  hist->GetXaxis()->SetRangeUser(mean - 5 * sigma, mean + 5 * sigma);
}

void FitHist(TH1D *hist)
{
  if (hist->GetEntries() == 0) {
    return;
  }
  auto mean = hist->GetBinCenter(hist->GetMaximumBin());
  // auto sigma = hist->GetRMS();
  auto sigma = 2;
  auto f = new TF1("f", "gaus", mean - 1 * sigma, mean + 1 * sigma);
  f->SetParameters(hist->GetMaximum(), mean, sigma);
  hist->Fit(f, "QR", "", mean - 1 * sigma, mean + 1 * sigma);

  f->SetRange(mean - 1 * sigma, mean + 1 * sigma);
  hist->Fit(f, "QR", "", mean - 1 * sigma, mean + 1 * sigma);

  hist->GetXaxis()->SetRangeUser(mean - 5 * sigma, mean + 5 * sigma);
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
  tree->SetBranchStatus("*", kFALSE);

  std::vector<uint8_t> *Module = nullptr;
  tree->SetBranchStatus("Module", kTRUE);
  tree->SetBranchAddress("Module", &Module);
  std::vector<uint8_t> *Channel = nullptr;
  tree->SetBranchStatus("Channel", kTRUE);
  tree->SetBranchAddress("Channel", &Channel);
  std::vector<double_t> *Timestamp = nullptr;
  tree->SetBranchStatus("Timestamp", kTRUE);
  tree->SetBranchAddress("Timestamp", &Timestamp);
  std::vector<uint16_t> *ADC = nullptr;
  tree->SetBranchStatus("Energy", kTRUE);
  tree->SetBranchAddress("Energy", &ADC);

  const auto nEntries = tree->GetEntries();
  {
    std::lock_guard<std::mutex> lock(counterMutex);
    totalEvents += nEntries;
  }

  for (auto i = 0; i < nEntries; i++) {
    constexpr auto nProcess = 1000;
    if (i % nProcess == 0) {
      std::lock_guard<std::mutex> lock(counterMutex);
      processedEvents += nProcess;
    }
    tree->GetEntry(i);

    bool isCo = false;
    for (uint32_t i = 0; i < Module->size(); i++) {
      if (Timestamp->at(i) == 0) {
        if (1225 < ADC->at(i) && ADC->at(i) < 1520) {  // For 103
          // if (1200 < ADC->at(i) && ADC->at(i) < 1485) {  // For 115
          isCo = true;
          break;
        }
      }
    }

    for (uint32_t j = 0; j < Module->size(); j++) {
      auto timestamp = Timestamp->at(j);
      if (timestamp == 0) {
        histTriggerADC->Fill(ADC->at(j));
      } else {
        auto module = Module->at(j);
        auto channel = Channel->at(j);
        auto hitID = module * 16 + channel;
        if (isCo || module == 0 || module == 1)
          histTime->Fill(timestamp, hitID);
      }
    }
  }

  IsFinished.at(threadID) = true;
}

void time_alignment()
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

  auto settingsFileName = "./chSettings.json";
  auto chSettingsVec = TChSettings::GetChSettings(settingsFileName);

  constexpr double_t lightSpeed = 29.9792458;  // cm/ns
  std::vector<std::vector<double_t>> tof(nModules,
                                         std::vector<double_t>(nChannels));
  for (uint32_t i = 0; i < nModules; i++) {
    for (uint32_t j = 0; j < nChannels; j++) {
      tof.at(i).at(j) = chSettingsVec.at(i).at(j).distance / lightSpeed;
    }
  }

  std::vector<std::vector<TH1D *>> histTof(nModules,
                                           std::vector<TH1D *>(nChannels));
  for (uint32_t i = 0; i < nModules; i++) {
    for (uint32_t j = 0; j < nChannels; j++) {
      histTof.at(i).at(j) = histTime->ProjectionX(
          Form("histTof_%d_%d", i, j), i * 16 + j + 1, i * 16 + j + 1);
      histTof.at(i).at(j)->SetTitle(
          Form("Time of flight for Module %d, Channel %d", i, j));
      histTof.at(i).at(j)->GetXaxis()->SetTitle("Time of flight (ns)");
    }
  }

  for (uint32_t i = 0; i < nModules; i++) {
    for (uint32_t j = 0; j < nChannels; j++) {
      if (i < 2)
        FitSiHist(histTof.at(i).at(j));
      else
        FitHist(histTof.at(i).at(j));
    }
  }

  auto canvas = new TCanvas("canvas", "canvas", 800, 600);
  canvas->Divide(4, 4);
  for (uint32_t i = 0; i < nModules; i++) {
    for (uint32_t j = 0; j < nChannels; j++) {
      canvas->cd(j + 1);
      histTof.at(i).at(j)->Draw();
    }
    canvas->SaveAs(Form("histTof_%d.pdf", i), "pdf");
  }
  delete canvas;

  for (uint32_t i = 0; i < nModules; i++) {
    for (uint32_t j = 0; j < nChannels; j++) {
      auto fitFunc = histTof.at(i).at(j)->GetFunction("f");
      if (fitFunc) {
        auto mean = fitFunc->GetParameter(1);
        if (i < 2) mean = histTof.at(i).at(j)->GetMean();
        if (i == 2 && j == 0) mean = 0;  // Event trigger detector
        chSettingsVec.at(i).at(j).timeOffset = tof.at(i).at(j) - mean;
        std::cout << "Module " << i << ", Channel " << j
                  << ", time offset: " << chSettingsVec.at(i).at(j).timeOffset
                  << std::endl;
      }
    }
  }

  std::ifstream ifs(settingsFileName);
  nlohmann::json jsonFile;
  ifs >> jsonFile;
  for (uint32_t i = 0; i < nModules; i++) {
    for (uint32_t j = 0; j < nChannels; j++) {
      jsonFile.at(i).at(j).at("TimeOffset") =
          chSettingsVec.at(i).at(j).timeOffset;
      if (i == 0)
        jsonFile.at(i).at(j).at("IsEventTrigger") = true;
      else
        jsonFile.at(i).at(j).at("IsEventTrigger") = false;
    }
  }
  ifs.close();
  auto outputFileName = "./chSettings.json";
  std::ofstream ofs(outputFileName);
  ofs << jsonFile.dump(4);
  ofs.close();
}
