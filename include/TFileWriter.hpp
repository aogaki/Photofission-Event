#ifndef TFileWriter_hpp
#define TFileWriter_hpp 1

#include <TFile.h>
#include <TTree.h>

#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include "TEventData.hpp"

class TFileWriter
{
 public:
  TFileWriter(std::string fileName);
  ~TFileWriter();

  void SetData(std::unique_ptr<std::vector<TEventData>> &data);

  void Write();

 private:
  void WriteData();
  std::thread fWriteDataThread;
  std::mutex fMutex;
  bool fWritingFlag;

  std::unique_ptr<std::vector<TEventData>> fRawData;
  TFile *fOutputFile;
  TTree *fTree;

  // For tree branches
  bool fIsFissionEvent;
  uint8_t fTriggerID;
  double_t fTriggerTime;
  uint8_t fSiFrontMultiplicity;
  uint8_t fSiBackMultiplicity;
  uint8_t fSiMultiplicity;
  uint8_t fGammaMultiplicity;
  uint8_t fNeutronMultiplicity;
  std::vector<uint8_t> fModule;
  std::vector<uint8_t> fChannel;
  std::vector<double_t> fTimestamp;
  std::vector<uint16_t> fEnergy;
  std::vector<uint16_t> fEnergyShort;
};

#endif