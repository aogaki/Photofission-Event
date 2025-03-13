#ifndef TChSettings_hpp
#define TChSettings_hpp 1

#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

class TChSettings
{
 public:
  TChSettings() {};
  ~TChSettings() {};

  bool isEventTrigger = false;
  uint32_t coincidenceID = 0;
  int32_t detectorID = 0;
  uint32_t mod = 0;
  uint32_t ch = 0;
  double_t timeOffset = 0.;
  uint32_t thresholdADC = 0;

  bool hasAC = false;
  uint32_t ACMod = 0;
  uint32_t ACCh = 0;

  double_t phi = 0.;
  double_t theta = 0.;
  double_t distance = 0.;

  double_t x = 0.;
  double_t y = 0.;
  double_t z = 0.;

  double_t p0 = 0.;
  double_t p1 = 1.;
  double_t p2 = 0.;
  double_t p3 = 0.;

  void Print()
  {
    std::cout << "Module: " << mod << "\tChannel: " << ch << std::endl;
    std::cout << "\tTime Offset: " << timeOffset << std::endl;
    std::cout << "\tIs Event Trigger: " << isEventTrigger << std::endl;
    std::cout << "\tDetector ID: " << detectorID << std::endl;
    std::cout << "\tHas AC: " << hasAC << std::endl;
    std::cout << "\tAC Module: " << ACMod << "\tAC Channel: " << ACCh
              << std::endl;
    std::cout << "\tPhi: " << phi << "\tTheta: " << theta
              << "\tDistance: " << distance << std::endl;
    std::cout << "\tx: " << x << "\ty: " << y << "\tz: " << z << std::endl;
    std::cout << "\tp0: " << p0 << "\tp1: " << p1 << "\tp2: " << p2
              << "\tp3: " << p3 << std::endl;
    std::cout << "\tThreshold ADC: " << thresholdADC << std::endl;
    std::cout << std::endl;
  };

  static void GenerateTemplate(uint32_t nMods = 10, uint32_t nChs = 16)
  {
    nlohmann::json result;
    for (uint32_t i = 0; i < nMods; i++) {
      nlohmann::json mod;
      for (uint32_t j = 0; j < nChs; j++) {
        nlohmann::json ch;
        ch["IsEventTrigger"] = false;
        ch["DetectorID"] = 0;
        ch["Module"] = i;
        ch["Channel"] = j;
        ch["HasAC"] = false;
        ch["CoincidenceID"] = 0;
        ch["ACModule"] = 128;
        ch["ACChannel"] = 128;
        ch["Phi"] = 0.;
        ch["Theta"] = 0.;
        ch["Distance"] = 0.;
        ch["TimeOffset"] = 0.;
        ch["ThresholdADC"] = 0;
        ch["x"] = 0.;
        ch["y"] = 0.;
        ch["z"] = 0.;
        ch["p0"] = 0.;
        ch["p1"] = 1.;
        ch["p2"] = 0.;
        ch["p3"] = 0.;
        mod.push_back(ch);
      }
      result.push_back(mod);
    }

    std::ofstream ofs("chSettings.json");
    ofs << result.dump(4) << std::endl;
    ofs.close();
  };

  static std::vector<std::vector<TChSettings>> GetChSettings(
      const std::string fileName)
  {
    std::vector<std::vector<TChSettings>> chSettingsVec;

    std::ifstream ifs(fileName);
    if (!ifs) {
      std::cerr << "File not found: " << fileName << std::endl;
      return chSettingsVec;
    }

    nlohmann::json j;
    ifs >> j;

    for (const auto &mod : j) {
      std::vector<TChSettings> chSettings;
      for (const auto &ch : mod) {
        TChSettings chSetting;
        chSetting.isEventTrigger = ch["IsEventTrigger"];
        chSetting.coincidenceID = ch["CoincidenceID"];
        chSetting.detectorID = ch["DetectorID"];
        chSetting.mod = ch["Module"];
        chSetting.ch = ch["Channel"];
        chSetting.timeOffset = ch["TimeOffset"];
        chSetting.hasAC = ch["HasAC"];
        chSetting.ACMod = ch["ACModule"];
        chSetting.ACCh = ch["ACChannel"];
        chSetting.phi = ch["Phi"];
        chSetting.theta = ch["Theta"];
        chSetting.thresholdADC = ch["ThresholdADC"];
        chSetting.distance = ch["Distance"];
        chSetting.x = ch["x"];
        chSetting.y = ch["y"];
        chSetting.z = ch["z"];
        chSetting.p0 = ch["p0"];
        chSetting.p1 = ch["p1"];
        chSetting.p2 = ch["p2"];
        chSetting.p3 = ch["p3"];

        chSettings.push_back(chSetting);
      }
      chSettingsVec.push_back(chSettings);
    }

    return chSettingsVec;
  };
};

typedef TChSettings ChSettings_t;
typedef std::vector<std::vector<ChSettings_t>> ChSettingsVec_t;

#endif