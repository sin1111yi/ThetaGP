#pragma once

namespace ThetaGP::Drivers::Peripheral {

class PeripheralsManager {
public:
  PeripheralsManager();

  static PeripheralsManager &getInstance() {
    static PeripheralsManager instance;
    return instance;
  }

  void init();
};

} // namespace ThetaGP::Drivers::Peripheral