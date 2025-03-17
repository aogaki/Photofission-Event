#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>

void gen_no_timeoffset()
{
  std::ifstream settings("chSettings.json");
  if (!settings.is_open()) {
    std::cerr << "No channel settings file \"chSettings.json\" found."
              << std::endl;
    return;
  }
  nlohmann::json jSettings;
  settings >> jSettings;

  for (auto iModule = 0; iModule < jSettings.size(); iModule++) {
    for (auto iChannel = 0; iChannel < jSettings[iModule].size(); iChannel++) {
      jSettings[iModule][iChannel]["TimeOffset"] = 0.0;
      jSettings[iModule][iChannel]["IsEventTrigger"] = false;
      if (iModule == 2 && iChannel == 0) {
        jSettings[iModule][iChannel]["IsEventTrigger"] = true;
      }
    }
  }

  std::ofstream out("chSettings.json");
  out << jSettings.dump(4) << std::endl;
  out.close();
}