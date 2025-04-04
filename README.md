# Photofission event builder
## This is a simple event builder for the photofission experiment at the E9 beamline at the ELI-NP.

### Dependencies
- ROOT
- CMake
- C++17
- git
- This code is tested with ROOT 6.34.06 Ubuntu 22.04 and macOS 15.4

### Download
```bash
git clone git@github.com:aogaki/Photofission-Event.git
cd Photofission-Event
```
This code is aiming to build event files from the raw data files in the same run. This README uses run103.
```bash
mkdir run103
cd run103
```
### Build
```bash
cmake ..
make
```
cmake will create a Makefile and the executable in the build directory also copy some files for settings and a template of data analysis.
- settings.json: settings for the event builder
- chSettings.json: settings for the channel
- reader.cpp: template for the data analysis
- time_alignment.cpp: template for the time alignment, using with gen_no_timeoffset.cpp

### Time alignment
One Gamma-ray detector is used for the reference. Other detectors are aligned with the reference detector.
```bash
root -l -q gen_no_timeoffset.cpp
```
This will change all time offsets in the chSettings.json file as 0. And one Gamma detector is set as the event trigger detector ("IsEventTrigger": true).
```json
    some lines
  "IsEventTrigger": true,
  "TimeOffset": 0.0
    some lines
```
### Run without time offset
```bash
./event-builder
```
This read settings.json and chSettings.json files and read the raw data files in the same run. The event builder will create a new file events_t*.root. * is from 0 to N, where N is usually the number of threads. To simplyfy the analysis macro running multi-threading, each thread read one file and merge results. Also TFile class of ROOT does not support multi-threading. 
This will ask you to input some information. The default value is the same as the settings.json file. You can change the value by inputting a new value. If you want to use the default value, just press Enter. But the flag of fission event only or not requires your input. This event building is for the time alignment. The time window is set to 1000 ns is safe for the time alignment. 
```bash
root -l -q time_alignment.cpp
```
This will create a new chSettings.json file with the time offsets for each detector. The time offsets are calculated by the time difference between the reference detector and the other detectors. The time offsets are saved in the chSettings.json file.
```json
    some lines
  "IsEventTrigger": false,
  "TimeOffset": -1.234567
    some lines
```
This macro file set the Si detectors as the event trigger detectors. If you want to set the Gamma detectors as the event trigger detectors, you need to change the "IsEventTrigger" of Gamma detetors to true and Si detectors to false in the chSettings.json file.
```json
    some lines
  "IsEventTrigger": false,
  "TimeOffset": -200.1234567
    some lines
```json
    some lines
  "IsEventTrigger": true,
  "TimeOffset": -1.234567
    some lines
```

### Event builder
```bash
./event-builder
```
This will read the new chSettings.json file and create a new file events_t*.root. In this time, the time window is not needed to big.

### Analysis
```bash
root -l reader.cpp
```
This macro files do a simple analysis only. You can write your own analysis macro file. 