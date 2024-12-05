#include "BasicSc2Bot.h"

// Control all units
void BasicSc2Bot::ControlUnits() {
	ControlSCVs();
	ControlBattlecruisers();
	ControlSiegeTanks();
	ControlMarines();
}