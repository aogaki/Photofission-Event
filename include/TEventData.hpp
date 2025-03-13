#ifndef TEventData_hpp
#define TEventData_hpp 1

#include <tuple>

class THitData
{
 public:
  uint8_t Module;
  uint8_t Channel;
  double_t Timestamp;
  uint16_t Energy;
  uint16_t EnergyShort;

  THitData() {};
  THitData(uint8_t Module, uint8_t Channel, double_t Timestamp, uint16_t Energy,
           uint16_t EnergyShort)
      : Channel(Channel),
        Timestamp(Timestamp),
        Module(Module),
        Energy(Energy),
        EnergyShort(EnergyShort) {};
  virtual ~THitData() {};
};
typedef THitData HitData_t;

class TEventData
{
 public:
  TEventData() {};
  virtual ~TEventData() {};

  bool IsFissionEvent;
  uint8_t TriggerID;
  uint8_t SiFrontMultiplicity;
  uint8_t SiBackMultiplicity;
  uint8_t SiMultiplicity;
  uint8_t GammaMultiplicity;
  uint8_t NeutronMultiplicity;
  std::vector<HitData_t> fHitData;
};

#endif