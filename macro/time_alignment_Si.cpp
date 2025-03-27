#include <TCanvas.h>
#include <TChain.h>
#include <TF1.h>
#include <TFile.h>
#include <TGraph.h>
#include <TGraphErrors.h>
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

constexpr uint32_t nModules = 1;
constexpr uint32_t nChannels = 16;
TH2D *histTimeADC[nModules][nChannels];
void InitHists()
{
  for (uint32_t i = 0; i < nModules; i++) {
    for (uint32_t j = 0; j < nChannels; j++) {
      histTimeADC[i][j] = new TH2D(Form("histTimeADC_%d_%d", i, j),
                                   Form("Module %d, Channel %d", i, j), 250,
                                   -1000, 1000, 500, 2000.5, 12000.5);
      histTimeADC[i][j]->GetXaxis()->SetTitle("Time (ns)");
      histTimeADC[i][j]->GetYaxis()->SetTitle("ADC");
    }
  }
}

TGraphErrors *GetTimeGraph(std::vector<TH1D *> histVec,
                           Double_t timeOffset = 0.0)
{
  auto nData = histVec.size();
  auto graph = new TGraphErrors(nData);
  for (uint32_t i = 0; i < nData; i++) {
    auto hist = histVec.at(i);
    auto maxBin = hist->GetMaximumBin();
    auto binCenter = hist->GetXaxis()->GetBinCenter(maxBin);
    auto f1 =
        new TF1(Form("f1%100d", i), "gaus", binCenter - 10, binCenter + 10);
    hist->Fit(f1, "QR");
    auto x = std::stoi(hist->GetTitle());
    auto y = f1->GetParameter(1) + timeOffset;
    auto yError = f1->GetParError(2);

    graph->SetPoint(i, x, y);
    graph->SetPointError(i, 0, yError);
  }
  return graph;
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

  bool IsFissionEvent;
  tree->SetBranchStatus("IsFissionEvent", kTRUE);
  tree->SetBranchAddress("IsFissionEvent", &IsFissionEvent);
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

    if (!IsFissionEvent) {
      continue;
    }

    for (uint32_t j = 0; j < Module->size(); j++) {
      auto timestamp = Timestamp->at(j);
      auto module = Module->at(j);
      auto channel = Channel->at(j);
      auto adc = ADC->at(j);
      if (module < nModules && channel < nChannels && timestamp != 0) {
        histTimeADC[module][channel]->Fill(timestamp, adc);
      }
    }
  }

  IsFinished.at(threadID) = true;
}

std::vector<TGraphErrors *> graphVec;
void time_alignment_Si()
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
  std::ifstream ifs(settingsFileName);
  nlohmann::json jsonFile;
  ifs >> jsonFile;
  for (uint32_t j = 0; j < nChannels; j++) {
    std::vector<TH1D *> histTimeVec;

    const auto nBins = histTimeADC[0][j]->GetNbinsY();
    const auto maxBinContent = histTimeADC[0][j]->ProjectionX()->GetMaximum();
    auto timeOffset = jsonFile.at(0).at(j).at("TimeOffset").get<double>();
    for (auto i = 1; i <= nBins; i++) {
      auto hist = histTimeADC[0][j]->ProjectionX(Form("histTime_%d", i), i, i);
      if (hist->GetEntries() > 0.01 * maxBinContent) {
        auto binCenter = histTimeADC[0][j]->GetYaxis()->GetBinCenter(i);
        hist->SetTitle(Form("%.0f", binCenter));
        histTimeVec.push_back(hist);
      }
    }
    auto graph = GetTimeGraph(histTimeVec, timeOffset);
    auto f2 = new TF1(Form("f2_%d", j), "pol1", 0, 30000);
    graph->Fit(f2, "QR");
    graph->Draw("AP");

    graphVec.push_back(graph);
  }

  auto canvas = new TCanvas("canvas", "canvas", 800, 600);
  canvas->Divide(4, 4);
  for (uint32_t i = 0; i < graphVec.size(); i++) {
    canvas->cd(i + 1);
    graphVec.at(i)->Draw("AP");
  }

  std::string outName = "Si_time_function.txt";
  std::ofstream outFile(outName);
  for (uint32_t i = 0; i < graphVec.size(); i++) {
    auto graph = graphVec.at(i);
    outFile << i << " " << graph->GetFunction(Form("f2_%d", i))->GetParameter(0)
            << " " << graph->GetFunction(Form("f2_%d", i))->GetParameter(1)
            << std::endl;
  }
  outFile.close();
}
