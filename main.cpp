#include <TROOT.h>

#include <filesystem>
#include <iostream>
#include <memory>
#include <mutex>
#include <nlohmann/json.hpp>
#include <string>
#include <thread>
#include <vector>

#include "TChSettings.hpp"
#include "TEventBuilder.hpp"
#include "TFileWriter.hpp"

std::vector<std::string> GetFileList(const std::string &directory,
                                     const uint32_t runNumber,
                                     const uint32_t startVersion,
                                     const uint32_t endVersion)
{
  std::vector<std::string> fileList;
  if (!std::filesystem::exists(directory)) {
    std::cerr << "Directory not found: " << directory << std::endl;
    return fileList;
  }
  for (auto i = startVersion; i <= endVersion; i++) {
    auto searchKey =
        "run" + std::to_string(runNumber) + "_" + std::to_string(i) + "_";
    for (const auto &entry : std::filesystem::directory_iterator(directory)) {
      if (entry.path().string().find(searchKey) != std::string::npos) {
        fileList.push_back(entry.path().string());
        break;
      }
    }
  }
  return fileList;
}

int main(int argc, char *argv[])
{
  bool interactionMode = true;
  if (argc > 1) {
    for (auto i = 1; i < argc; i++) {
      if (std::string(argv[i]) == "-b") {
        interactionMode = false;
      }
    }
  }

  auto nThreads = std::thread::hardware_concurrency();
  std::cout << "Number of threads: " << nThreads << std::endl;

  auto settings = std::ifstream("settings.json");
  if (!settings.is_open()) {
    std::cerr << "No settings file \"settings.json\" found." << std::endl;
    return 1;
  }
  nlohmann::json jSettings;
  settings >> jSettings;

  std::string directory = jSettings["Directory"];
  uint32_t runNumber = jSettings["RunNumber"];
  uint32_t startVersion = jSettings["StartVersion"];
  uint32_t endVersion = jSettings["EndVersion"];
  double_t timeWindow = jSettings["TimeWindow"];

  if (interactionMode) {
    // File specification
    std::cout << "Input the directory: ";
    std::cout << "Default: " << directory << std::endl;
    std::string bufString;
    std::getline(std::cin, bufString);
    if (bufString != "") {
      directory = bufString;
    }

    std::cout << "Input the run number: ";
    std::cout << "Default: " << runNumber << std::endl;
    std::getline(std::cin, bufString);
    if (bufString != "") {
      runNumber = std::stoi(bufString);
    }

    std::cout << "Input the start version: ";
    std::cout << "Default: " << startVersion << std::endl;
    std::getline(std::cin, bufString);
    if (bufString != "") {
      startVersion = std::stoi(bufString);
    }

    std::cout << "Input the end version: ";
    std::cout << "Default: " << endVersion << std::endl;
    std::getline(std::cin, bufString);
    if (bufString != "") {
      endVersion = std::stoi(bufString);
    }

    std::cout << "Input the time window: +- ";
    std::cout << "Default: +-" << timeWindow << " ns" << std::endl;
    std::getline(std::cin, bufString);
    if (bufString != "") {
      timeWindow = std::stod(bufString);
    }
  }

  std::cout << "Directory: " << directory << std::endl;
  std::cout << "Run number: " << runNumber << std::endl;
  std::cout << "Start version: " << startVersion << std::endl;
  std::cout << "End version: " << endVersion << std::endl;
  std::cout << "Time window: +-" << timeWindow << " ns" << std::endl;

  auto fileList = GetFileList(directory, runNumber, startVersion, endVersion);
  // for (const auto &file : fileList) {
  //   std::cout << file << std::endl;
  // }
  std::cout << "Number of files: " << fileList.size() << std::endl;
  if (fileList.size() == 0) {
    std::cerr << "No files found." << std::endl;
    return 1;
  }
  if (fileList.size() < nThreads) {
    nThreads = fileList.size();
    std::cout << "Number of threads: " << nThreads << std::endl;
  }

  auto chSettingsVec = TChSettings::GetChSettings("chSettings.json");
  if (chSettingsVec.size() == 0) {
    std::cerr << "No channel settings file \"chSettings.json\" found."
              << std::endl;
    return 1;
  }

  ROOT::EnableThreadSafety();
  std::vector<std::thread> threads;
  std::mutex mutex;
  auto eveCount = 0;
  auto start = std::chrono::high_resolution_clock::now();
  for (auto i = 0; i < nThreads; i++) {
    threads.push_back(std::thread([&, threadID = i]() {
      mutex.lock();
      auto outputName = "events_t" + std::to_string(threadID) + ".root";
      auto fileWriter = std::make_unique<TFileWriter>(outputName);
      std::cout << "Output file: " << outputName << std::endl;
      mutex.unlock();

      while (true) {
        mutex.lock();
        if (fileList.size() == 0) {
          mutex.unlock();
          break;
        }
        auto fileName = fileList.at(0);
        fileList.erase(fileList.begin());
        mutex.unlock();

        TEventBuilder eventBuilder(fileName, timeWindow, chSettingsVec);
        auto nHits = eventBuilder.LoadHits();
        mutex.lock();
        std::cout << "Number of hits from " << fileName << " : " << nHits
                  << std::endl;
        mutex.unlock();

        auto nEvents = eventBuilder.EventBuild();
        auto eventData = eventBuilder.GetEventData();
        mutex.lock();
        std::cout << "Number of events from " << fileName << " : " << nEvents
                  << std::endl;
        eveCount += nEvents;
        fileWriter->SetData(eventData);
        mutex.unlock();
      }
      mutex.lock();
      std::cout << "Thread " << threadID << " finished." << std::endl;
      mutex.unlock();

      fileWriter->Write();
    }));
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }

  for (auto &thread : threads) {
    thread.join();
  }
  // fileWriter->Write();
  std::cout << "Number of events: " << eveCount << std::endl;
  auto end = std::chrono::high_resolution_clock::now();
  auto elapsed =
      std::chrono::duration_cast<std::chrono::milliseconds>(end - start)
          .count();
  std::cout << "Elapsed time: " << elapsed / 1.e3 << " s" << std::endl;

  return 0;
}