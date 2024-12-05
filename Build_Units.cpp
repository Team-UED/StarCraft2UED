#include "BasicSc2Bot.h"

using namespace sc2;

void BasicSc2Bot::ManageProduction() {
	// Train units and upgrades
	TrainMarines();
	TrainBattlecruisers();
	TrainSiegeTanks();
	UpgradeMarines();
	UpgradeMechs();
}

void BasicSc2Bot::TrainMarines() {
	// Find Barracks to train Marines
	const ObservationInterface* obs = Observation();
	Units barracks = obs->GetUnits(Unit::Alliance::Self,
		IsUnit(UNIT_TYPEID::TERRAN_BARRACKS));
	Units factories = obs->GetUnits(Unit::Alliance::Self,
		IsUnit(UNIT_TYPEID::TERRAN_FACTORY));
	Units starport = obs->GetUnits(Unit::Alliance::Self,
		IsUnit(UNIT_TYPEID::TERRAN_STARPORT));
	Units reactor = obs->GetUnits(Unit::Alliance::Self,
		IsUnit(UNIT_TYPEID::TERRAN_BARRACKSREACTOR));

	if (barracks.empty() ||
		phase == 0) { // Can't train Marines without Barracks
		return;
	}

	// factory is built...gotta swap
	if (phase == 1 && !factories.empty()) {
		if (factories.front()->build_progress > 0.4) {
			return;
		}
	}
	else if (phase == 2 && !starport.empty()) {
		if (starport.front()->build_progress > 0.5) {
			return;
		}
	}

	// Train Marines from Barracks
	if (CanBuild(50) && !barracks.empty()) {
		for (const auto& b : barracks) {
			if (b->orders.empty() && !reactor.empty() &&
				b->add_on_tag == reactor.front()->tag) {
				if (CanBuild(550)) {
					Actions()->UnitCommand(b, ABILITY_ID::TRAIN_MARINE, true);
					Actions()->UnitCommand(b, ABILITY_ID::TRAIN_MARINE, true);
				}
			}
			else if (b->orders.empty()) {
				Actions()->UnitCommand(b, ABILITY_ID::TRAIN_MARINE);
			}
		}
	}
}

void BasicSc2Bot::TrainBattlecruisers() {
	// Find Starports to build a Battlecruiser
	const ObservationInterface* obs = Observation();
	Units starports = obs->GetUnits(Unit::Alliance::Self, [](const Unit& unit) {
		return unit.unit_type == UNIT_TYPEID::TERRAN_STARPORT && unit.tag;
		});
	Units fusioncore =
		obs->GetUnits(Unit::Alliance::Self, [](const Unit& unit) {
		return unit.unit_type == UNIT_TYPEID::TERRAN_FUSIONCORE && unit.tag;
			});

	if (starports.empty() || fusioncore.empty()) {
		return;
	}
	// Train Battlecruisers from Starports
	const Unit* starport = starports.front();
	if (starport->add_on_tag != 0) {
		if (starport->orders.empty()) {
			if (CanBuild(400, 300, 6)) {
				Actions()->UnitCommand(starport,
					ABILITY_ID::TRAIN_BATTLECRUISER);
			}
		}
		else {
			for (const auto& order : starport->orders) {
				if (order.ability_id == ABILITY_ID::TRAIN_BATTLECRUISER) {
					if (order.progress >= 0.0f) {
						first_battlecruiser = true;
					}
				}
			}
		}
	}
}

void BasicSc2Bot::TrainSiegeTanks() {
	// Find Factories to train Siege Tanks
	const ObservationInterface* obs = Observation();
	Units factories = obs->GetUnits(Unit::Alliance::Self,
		IsUnit(UNIT_TYPEID::TERRAN_FACTORY));
	Units starport = obs->GetUnits(Unit::Alliance::Self,
		IsUnit(UNIT_TYPEID::TERRAN_STARPORT));
	Units fusioncores = obs->GetUnits(Unit::Alliance::Self,
		IsUnit(UNIT_TYPEID::TERRAN_FUSIONCORE));

	// Can't train Siege Tanks without Factories
	if (factories.empty()) {
		return;
	}
	const Unit* factory;
	if (CanBuild(150, 125, 3)) {
		if (phase == 2) {
			factory = factories.front();
			// Maintain 1 : 4 Ratio of Marines and Siege Tanks
			if (factory->add_on_tag != 0 && factory->orders.empty()) {
				Actions()->UnitCommand(factory, ABILITY_ID::TRAIN_SIEGETANK);
			}
		}
		else if (phase == 3) {
			if (first_battlecruiser) {
				std::vector<float> MineralGas =
					HowCloseToResourceGoal(400, 300);
				/*float avg_resources = (MineralGas[0] + MineralGas[1]) / 2;*/
				float m_close = MineralGas[0];
				float g_close = MineralGas[1];
				float how_close =
					!starport.empty()
					? HowClosetoFinishCurrentJob(starport.front())
					: 0.0f;
				factory = factories.front();
				if (num_starports && ((0.0f < how_close && how_close < 0.5f))) {
					if (factory->add_on_tag != 0 && factory->orders.empty()) {
						Actions()->UnitCommand(factory,
							ABILITY_ID::TRAIN_SIEGETANK);
					}
				}
				else if (num_starports && m_close > 0.9f && g_close > 0.9f) {

					if (factory->add_on_tag != 0 && factory->orders.empty()) {
						Actions()->UnitCommand(factory,
							ABILITY_ID::TRAIN_SIEGETANK);
					}
				}
				else if (!num_starports || !num_fusioncores) {
					if (factory->add_on_tag != 0 && factory->orders.empty()) {
						Actions()->UnitCommand(factory,
							ABILITY_ID::TRAIN_SIEGETANK);
					}
				}
			}
		}
	}
}

void BasicSc2Bot::UpgradeMarines() {
	// Find Tech Labs and Engineering Bays to upgrade Marines
	const ObservationInterface* observation = Observation();
	Units techlabs = observation->GetUnits(
		Unit::Alliance::Self, IsUnit(UNIT_TYPEID::TERRAN_BARRACKSTECHLAB));
	Units engineeringbays = observation->GetUnits(
		Unit::Alliance::Self, IsUnit(UNIT_TYPEID::TERRAN_ENGINEERINGBAY));

	// Can't upgrade Marines without Engineering Bays
	if (engineeringbays.empty()) {
		return;
	}

	// Upgrade from Engineering Bay
	for (const auto& upgrade : engineeringbay_upgrade_order) {
		// Skip if the upgrade is already completed
		if (completed_upgrades.find(upgrade) != completed_upgrades.end()) {
			continue;
		}

		// Get the ability corresponding to the upgrade
		ABILITY_ID ability_id = GetAbilityForUpgrade(upgrade);
		if (ability_id == ABILITY_ID::INVALID) {
			continue;
		}

		// Upgrade from Tech Lab(Combat Shield)
		if (!techlabs.empty()) {
			if (completed_upgrades.find(UPGRADE_ID::COMBATSHIELD) ==
				completed_upgrades.end()) {
				ABILITY_ID upgrade = ABILITY_ID::RESEARCH_COMBATSHIELD;
				// Check if the Tech Lab is busy or not
				for (const auto& techlab : techlabs) {
					if (techlab->orders.empty() && CanBuild(100, 450)) {
						Actions()->UnitCommand(techlab, upgrade);
						return;
					}
				}
			}
		}

		// Check if the Engineering Bay is busy or not
		for (const auto& engineeringbay : engineeringbays) {
			if (engineeringbay->orders.empty() && CanBuild(200, 200)) {
				Actions()->UnitCommand(engineeringbay, ability_id);
				return;
			}
		}
	}
}

void BasicSc2Bot::UpgradeMechs() {
	// Find Armories to upgrade Mechs
	const ObservationInterface* observation = Observation();
	Units armories = observation->GetUnits(Unit::Alliance::Self,
		IsUnit(UNIT_TYPEID::TERRAN_ARMORY));

	// Can't upgrade Mechs(Battlecruisers and Tanks) without Armories
	// Also, save resources for first Battlecruiser
	if (armories.empty() || !first_battlecruiser) {
		return;
	}

	// Upgrade from Armory
	for (const auto& upgrade : armory_upgrade_order) {
		// Skip if the upgrade is already completed
		if (completed_upgrades.find(upgrade) != completed_upgrades.end()) {
			continue;
		}

		// Get the ability corresponding to the upgrade
		ABILITY_ID ability_id = GetAbilityForUpgrade(upgrade);
		if (ability_id == ABILITY_ID::INVALID) {
			continue;
		}

		// Check if the Armory is busy or not
		for (const auto& armory : armories) {
			if (armory->orders.empty() && CanBuild(500, 500)) {
				Actions()->UnitCommand(armory, ability_id);
				return;
			}
		}
	}
}