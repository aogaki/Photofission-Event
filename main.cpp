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
  std::string settingsFileName = "settings.json";
  if (argc > 1) {
    for (auto i = 1; i < argc; i++) {
      if (std::string(argv[i]) == "-b") {
        interactionMode = false;
        settingsFileName = argv[i + 1];
      }
    }
  }

  // auto nThreads = std::thread::hardware_concurrency();
  // std::cout << "Number of threads: " << nThreads << std::endl;

  auto settings = std::ifstream(settingsFileName);
  if (!settings.is_open()) {
    std::cerr << "No settings file \"settings.json\" found." << std::endl;
    return 1;
  }
  nlohmann::json jSettings;
  settings >> jSettings;

  // std::string directory = jSettings["Directory"];
  // std::string chSettingFileName = jSettings["ChannelSettings"];
  // uint32_t runNumber = jSettings["RunNumber"];
  // uint32_t startVersion = jSettings["StartVersion"];
  // uint32_t endVersion = jSettings["EndVersion"];
  // double_t timeWindow = jSettings["TimeWindow"];

  std::string directory = "";
  //check if directory is empty
  if (jSettings["Directory"].is_string()) {
    directory = jSettings["Directory"];
  } else {
    std::cerr << "No directory found in settings file." << std::endl;
    std::cerr << "Key \"Directory\" is not a string." << std::endl;
    return 1;
  }

  std::string chSettingFileName = "";
  if (jSettings["ChannelSettings"].is_string()) {
    chSettingFileName = jSettings["ChannelSettings"];
  } else {
    std::cerr << "No channel settings file found in settings file."
              << std::endl;
    std::cerr << "Key \"ChannelSettings\" is not a string." << std::endl;
    return 1;
  }

  bool onlyFissionEvents = false;
  if (jSettings["OnlyFissionEvent"].is_boolean()) {
    onlyFissionEvents = jSettings["OnlyFissionEvent"];
  } else {
    std::cerr << "No only fission event flag found in settings file."
              << std::endl;
    std::cerr << "Key \"OnlyFissionEvent\" is not a boolean." << std::endl;
    return 1;
  }

  uint32_t nThreads = 0;
  if (jSettings["NumberOfThreads"].is_number_integer()) {
    nThreads = jSettings["NumberOfThreads"];
  } else {
    std::cerr << "No number of threads found in settings file." << std::endl;
    std::cerr << "Key \"NumberOfThreads\" is not a number." << std::endl;
    return 1;
  }
  if (nThreads == 0) {
    nThreads = std::thread::hardware_concurrency();
  }

  uint32_t runNumber = 0;
  if (jSettings["RunNumber"].is_number_integer()) {
    runNumber = jSettings["RunNumber"];
  } else {
    std::cerr << "No run number found in settings file." << std::endl;
    std::cerr << "Key \"RunNumber\" is not a number." << std::endl;
    return 1;
  }

  uint32_t startVersion = 0;
  if (jSettings["StartVersion"].is_number_integer()) {
    startVersion = jSettings["StartVersion"];
  } else {
    std::cerr << "No start version found in settings file." << std::endl;
    std::cerr << "Key \"StartVersion\" is not a number." << std::endl;
    return 1;
  }

  uint32_t endVersion = 0;
  if (jSettings["EndVersion"].is_number_integer()) {
    endVersion = jSettings["EndVersion"];
  } else {
    std::cerr << "No end version found in settings file." << std::endl;
    std::cerr << "Key \"EndVersion\" is not a number." << std::endl;
    return 1;
  }

  uint32_t timeWindow = 0;
  if (jSettings["TimeWindow"].is_number_integer()) {
    timeWindow = jSettings["TimeWindow"];
  } else {
    std::cerr << "No time window found in settings file." << std::endl;
    std::cerr << "Key \"TimeWindow\" is not a number." << std::endl;
    return 1;
  }

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

    while (true) {
      std::cout << "Input only fission event flag: [y/n]";
      std::string onlyFissionEventString;
      if (onlyFissionEvents) {
        onlyFissionEventString = "y";
      } else {
        onlyFissionEventString = "n";
      }
      std::getline(std::cin, bufString);
      if (bufString == "y") {
        onlyFissionEvents = true;
        break;
      } else if (bufString == "n") {
        onlyFissionEvents = false;
        break;
      } else {
        std::cerr << "Invalid input. Only \"y\" or \"n\" are accepted."
                  << std::endl;
      }
    }

    std::cout << "Input the number of threads: ";
    std::cout << "Default: " << nThreads << std::endl;
    std::getline(std::cin, bufString);
    if (bufString != "") {
      nThreads = std::stoi(bufString);
    }
    if (nThreads == 0) {
      nThreads = std::thread::hardware_concurrency();
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

  auto chSettingsVec = TChSettings::GetChSettings(chSettingFileName);
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

        TEventBuilder eventBuilder(fileName, timeWindow, onlyFissionEvents,
                                   chSettingsVec);
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