#include "BasicSc2Bot.h"

using namespace sc2;

// ------------------ Helper Functions ------------------

// Determine whether siege tank should transform to Siege Mode, or Unsiege
bool BasicSc2Bot::SiegeTankInCombat(const Unit* unit) {
	if (!unit) {
		return false;
	}
	// Detect radius of the Siege Tank
	const float enemy_detection_radius = 13.5f;
	bool enemy_nearby = false;
	// Check for nearby enemies within the detection radius
	for (const auto& enemy_unit :
		Observation()->GetUnits(Unit::Alliance::Enemy)) {

		// Skip trivial and worker units
		if (IsTrivialUnit(enemy_unit) || IsWorkerUnit(enemy_unit)) {
			continue;
		}

		// Calculate distance to enemy
		float distance_to_enemy = Distance2D(unit->pos, enemy_unit->pos);
		if (distance_to_enemy <= enemy_detection_radius) {
			enemy_nearby = true;
			break;
		}
	}
	return enemy_nearby;
}

// ------------------ Main Functions ------------------

// Main function to control Siege Tanks
void BasicSc2Bot::ControlSiegeTanks() {
	SiegeMode();
	TargetSiegeTank();
}

// Transform Siege Tanks to Siege Mode or Unsiege
void BasicSc2Bot::SiegeMode() {
	// Get all Siege Tanks
	const Units siege_tanks = Observation()->GetUnits(
		Unit::Alliance::Self, IsUnit(UNIT_TYPEID::TERRAN_SIEGETANK));
	const Units siege_tanks_sieged = Observation()->GetUnits(
		Unit::Alliance::Self, IsUnit(UNIT_TYPEID::TERRAN_SIEGETANKSIEGED));

	if (siege_tanks.empty() && siege_tanks_sieged.empty()) { // No Siege Tanks
		return;
	}

	// Siege Tanks in combat should be in Siege Mode
	for (const auto& tank : siege_tanks) {
		if (SiegeTankInCombat(tank)) {
			Actions()->UnitCommand(tank, ABILITY_ID::MORPH_SIEGEMODE);
		}
	}
	// Siege Tanks not in combat should be Unsieged
	for (const auto& tank : siege_tanks_sieged) {
		if (!SiegeTankInCombat(tank)) {
			Actions()->UnitCommand(tank, ABILITY_ID::MORPH_UNSIEGE);
		}
	}
}

// Target mechanics for Siege Tanks
void BasicSc2Bot::TargetSiegeTank() {

	// Get all Siege Tanks
	const Units siege_tanks_sieged = Observation()->GetUnits(
		Unit::Alliance::Self, IsUnit(UNIT_TYPEID::TERRAN_SIEGETANKSIEGED));

	if (siege_tanks_sieged.empty()) {
		return;
	}

	for (const auto& siege_tank : siege_tanks_sieged) {

		// Initialize variables to find the best target
		const Unit* best_target = nullptr;
		float best_score = -1.0f;

		// Get all enemy units
		for (const auto& enemy_unit :
			Observation()->GetUnits(Unit::Alliance::Enemy)) {
			// Skip invalid or dead units

			if (!enemy_unit || !enemy_unit->is_alive) {
				continue;
			}

			// Calculate priority score for this enemy
			float score = 0.0f;

			// 1. Priority: Heavy Armor (e.g., Stalkers, Marauiders...etc)
			if (std::find(heavy_armor_units.begin(), heavy_armor_units.end(),
				enemy_unit->unit_type) != heavy_armor_units.end()) {
				score += 200.0f;
			}

			// 2. Priority: Packed Enemies (AOE Potential)
			int packed_count = 0;

			for (const auto& nearby_enemy :
				Observation()->GetUnits(Unit::Alliance::Enemy)) {
				if (nearby_enemy != enemy_unit &&
					Distance2D(enemy_unit->pos, nearby_enemy->pos) < 1.25f) {
					packed_count++;
				}
			}
			// Add 10 points for each nearby enemy
			score += packed_count * 10.0f;

			// 3. Priority: Enemies close to one-shot
			// Tank damage is 40(Light) or 70(Armored) in Siege Mode
			float health_difference = 0.0f;

			if (std::find(heavy_armor_units.begin(), heavy_armor_units.end(),
				enemy_unit->unit_type) != heavy_armor_units.end()) {
				health_difference =
					std::abs((enemy_unit->health + enemy_unit->shield) - 70.0f);
			}
			else {
				health_difference =
					std::abs((enemy_unit->health + enemy_unit->shield) - 40.0f);
			}

			score += 200.0f / (health_difference + 1.0f);

			// 4. The Rest (Prioritize closer targets)
			score +=
				1.0f / (Distance2D(siege_tank->pos, enemy_unit->pos) + 1.0f);

			// Update best target based on score
			if (score > best_score) {
				best_score = score;
				best_target = enemy_unit;
			}
		}

		// Issue attack command if a valid target is found
		if (best_target) {
			Actions()->UnitCommand(siege_tank, ABILITY_ID::ATTACK, best_target);
		}
	}
}