#include "BasicSc2Bot.h"

using namespace sc2;

// ------------------ Helper Functions ------------------

// Check if the ramp structure is intact
bool BasicSc2Bot::IsRampIntact() {
	// Check if the ramp is intact
	bool ramp_intact = true;
	for (const auto& building : ramp_depots) {
		if (!building) {
			ramp_intact = false;
			break;
		}
	}
	for (const auto& building : ramp_middle) {
		if (!building) {
			ramp_intact = false;
			break;
		}
	}

	return ramp_intact;
}

// Check if the unit is near the ramp
bool BasicSc2Bot::IsNearRamp(const Unit* unit) {
	if (!unit) { // Null check
		return false;
	}

	bool near_ramp = false;
	const float ramp_check_distance = 5.0f; // Distance to check for ramp proximity

	// Check if the unit is near the ramp
	for (const auto& building : ramp_depots) {
		if (building &&
			Distance2D(unit->pos, building->pos) < ramp_check_distance) {
			near_ramp = true;
			break;
		}
	}
	for (const auto& building : ramp_middle) {
		if (building &&
			Distance2D(unit->pos, building->pos) < ramp_check_distance) {
			near_ramp = true;
			break;
		}
	}
	return near_ramp;
}

// Get closest target to the unit
const Unit* BasicSc2Bot::GetClosestTarget(const Unit* unit) {
	if (!unit) { // Null check
		return nullptr;
	}

	const Unit* target = nullptr;
	float min_distance = std::numeric_limits<float>::max();

	// Find the closest enemy
	for (const auto& enemy_unit :
		Observation()->GetUnits(Unit::Alliance::Enemy)) {
		// Skip invalid or dead units
		if (!enemy_unit || !enemy_unit->is_alive) {
			continue;
		}

		// Calculate distance to the enemy unit
		float distance_to_enemy = Distance2D(unit->pos, enemy_unit->pos);

		// Find the closest unit
		if (distance_to_enemy < min_distance && distance_to_enemy < 13.0f) {
			min_distance = distance_to_enemy;
			target = enemy_unit;
		}
	}

	return target;
}

// Move Marine to a new position to perform kite
void BasicSc2Bot::KiteMarine(const Unit* marine, const Unit* target,
	bool advance, float distance) {
	Point2D direction =
		advance ? (target->pos - marine->pos) : (marine->pos - target->pos);
	float length =
		std::sqrt(direction.x * direction.x + direction.y * direction.y);

	// Normalize the vector
	if (length > 0) {
		direction /= length;
	}

	// Move the Marine to the new position
	Point2D new_position = marine->pos + direction * distance;
	Actions()->UnitCommand(marine, ABILITY_ID::MOVE_MOVE, new_position);
}

// ------------------ Main Functions ------------------

// Main function to control Marines
void BasicSc2Bot::ControlMarines() {
	KillScouts();
	TargetMarines();
}

// Target agressive scouts(Reapers) with Marines
void BasicSc2Bot::KillScouts() {

	// Get all Marines
	Units marines = Observation()->GetUnits(Unit::Alliance::Self,
		IsUnit(UNIT_TYPEID::TERRAN_MARINE));
	// Get all enemy units
	Units enemies = Observation()->GetUnits(Unit::Alliance::Enemy);

	// Check if there are no Marines or enemies
	if (marines.empty() || enemies.empty()) {
		return;
	}

	// Attack the scouts
	for (const auto& enemy_unit : enemies) {
		if (enemy_unit->unit_type == UNIT_TYPEID::TERRAN_REAPER &&
			Distance2D(enemy_unit->pos, start_location) <= 15.0f) {
			for (const auto& marine : marines) {
				if (marine->orders.empty()) {
					Actions()->UnitCommand(marine, ABILITY_ID::ATTACK_ATTACK,
						enemy_unit);
				}
			}
		}
	}
}

// Target mechanics for Marines
void BasicSc2Bot::TargetMarines() {

	// Get all Marines
	Units marines = Observation()->GetUnits(Unit::Alliance::Self,
		IsUnit(UNIT_TYPEID::TERRAN_MARINE));
	if (marines.empty()) { // Check if there are no Marines
		return;
	}

	// Marine parameters
	float marine_vision = 0.0f; // Marine's vision(default)
	const float fallback_distance =
		1.0f; // Distance to kite away for melee units
	const float advance_distance = 0.5f; // Distance to close for ranged units

	// For each Marine
	for (const auto& marine : marines) {
		const Unit* target = GetClosestTarget(marine);

		if (target) {
			// Check if the target is a melee unit
			bool is_melee =
				melee_units.find(target->unit_type) != melee_units.end();

			if (IsNearRamp(marine)) {
				marine_vision = 8.0f;
			}
			else {
				marine_vision = 13.0f;
			}

			// Check if the target is within the Marine's vision
			if (Distance2D(marine->pos, target->pos) > marine_vision) {
				continue;
			}

			// Attack whenever possible
			if (marine->weapon_cooldown == 0.0f) {
				Actions()->UnitCommand(marine, ABILITY_ID::ATTACK_ATTACK,
					target);
			}
			// Do not Kite if the ramp is intact and the Marine is near the ramp
			else if (IsRampIntact() && IsNearRamp(marine)) {
				continue;
			}
			else {
				if (is_melee && Distance2D(marine->pos, target->pos) <= 4.5f) {
					// Fall back if the target is melee
					KiteMarine(marine, target, false, fallback_distance);
				}
				else {
					// Check if the target is ranged but not a structure
					const UnitTypeData& target_type_data =
						Observation()->GetUnitTypeData().at(target->unit_type);
					bool is_structure = false;

					for (const auto& attribute : target_type_data.attributes) {
						if (attribute == Attribute::Structure) {
							is_structure = true;
							break;
						}
					}

					// Advance if the target is ranged and not a structure
					if (!is_structure &&
						Distance2D(marine->pos, target->pos) > 4.5f) {
						KiteMarine(marine, target, true, advance_distance);
					}
				}
			}
		}
	}
}
