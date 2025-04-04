#ifndef TEventBuilder_hpp
#define TEventBuilder_hpp 1

#include <memory>
#include <string>
#include <vector>

#include "TChSettings.hpp"
#include "TEventData.hpp"

enum class HitType {
  SiFront = 0,
  SiBack = 1,
  Gamma = 2,
  Neutron = 3,
  Unknown = 4,
};
typedef HitType HitType_t;

class TEventBuilder
{
 public:
  TEventBuilder(const std::string &fileName, const double_t timeWindow,
                bool onlyFissionEvents,
                const std::vector<std::vector<TChSettings>> &settings);
  ~TEventBuilder();

  uint32_t LoadHits();
  uint32_t EventBuild();

  std::unique_ptr<std::vector<TEventData>> GetEventData()
  {
    return std::move(fEventData);
  }

  void SetTimeWindow(double_t timeWindow) { fTimeWindow = timeWindow; }

 private:
  std::vector<THitData> fHitData;
  void CheckHitData();

  std::unique_ptr<std::vector<TEventData>> fEventData;
  std::string fFileName;
  std::vector<std::vector<TChSettings>> fSettings;
  double_t fTimeWindow = 1000.0;  // ns
  bool fOnlyFissionEvents = false;

  HitType_t GetHitType(uint8_t module);
};

#endif