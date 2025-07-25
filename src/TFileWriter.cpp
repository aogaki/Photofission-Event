#include "TFileWriter.hpp"

#include <iostream>

TFileWriter::TFileWriter(std::string fileName)
{
  fOutputFile = new TFile(fileName.c_str(), "RECREATE");
  fTree = new TTree("Event_Tree", "Data tree");
  fTree->Branch("IsFissionEvent", &fIsFissionEvent);
  fTree->Branch("TriggerID", &fTriggerID);
  fTree->Branch("TriggerTime", &fTriggerTime);
  fTree->Branch("SiFrontMultiplicity", &fSiFrontMultiplicity);
  fTree->Branch("SiBackMultiplicity", &fSiBackMultiplicity);
  fTree->Branch("SiMultiplicity", &fSiMultiplicity);
  fTree->Branch("GammaMultiplicity", &fGammaMultiplicity);
  fTree->Branch("NeutronMultiplicity", &fNeutronMultiplicity);
  fTree->Branch("Module", &fModule);
  fTree->Branch("Channel", &fChannel);
  fTree->Branch("Timestamp", &fTimestamp);
  fTree->Branch("Energy", &fEnergy);
  fTree->Branch("EnergyShort", &fEnergyShort);
  fTree->SetDirectory(fOutputFile);

  fRawData = std::make_unique<std::vector<TEventData>>();

  fWriteDataThread = std::thread(&TFileWriter::WriteData, this);
}

TFileWriter::~TFileWriter()
{
  // delete fOutputFile;
}

void TFileWriter::SetData(std::unique_ptr<std::vector<TEventData>> &data)
{
  fMutex.lock();
  fRawData->insert(fRawData->end(), data->begin(), data->end());
  fMutex.unlock();
}

void TFileWriter::Write()
{
  while (true) {
    fMutex.lock();
    if (fRawData->size() == 0) {
      fWritingFlag = false;
      fMutex.unlock();
      break;
    }
    fMutex.unlock();
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  fWriteDataThread.join();

  fMutex.lock();
  fOutputFile->cd();
  fTree->Write();
  fOutputFile->Write();
  fOutputFile->Close();
  fMutex.unlock();
}

void TFileWriter::WriteData()
{
  fWritingFlag = true;

  while (fWritingFlag) {
    fMutex.lock();
    auto size = fRawData->size();
    fMutex.unlock();

    if (size > 0) {
      auto localData = std::make_unique<std::vector<TEventData>>();
      fMutex.lock();
      localData->insert(localData->end(), fRawData->begin(), fRawData->end());
      fRawData->clear();
      fMutex.unlock();

      for (auto &event : *localData) {
        fIsFissionEvent = event.IsFissionEvent;
        fTriggerID = event.TriggerID;
        fTriggerTime = event.TriggerTime;
        fSiFrontMultiplicity = event.SiFrontMultiplicity;
        fSiBackMultiplicity = event.SiBackMultiplicity;
        fSiMultiplicity = event.SiMultiplicity;
        fGammaMultiplicity = event.GammaMultiplicity;
        fNeutronMultiplicity = event.NeutronMultiplicity;
        fModule.clear();
        fChannel.clear();
        fTimestamp.clear();
        fEnergy.clear();
        fEnergyShort.clear();
        for (auto &hit : event.HitData) {
          fModule.push_back(hit.Module);
          fChannel.push_back(hit.Channel);
          fTimestamp.push_back(hit.Timestamp);
          fEnergy.push_back(hit.Energy);
          fEnergyShort.push_back(hit.EnergyShort);
        }
        fTree->Fill();
      }
    } else {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  }
}