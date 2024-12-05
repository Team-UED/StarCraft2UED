#include "BasicSc2Bot.h"

// Returns the starting base location
const Unit* BasicSc2Bot::GetMainBase() const {
	// Get the main Command Center, Orbital Command, or Planetary Fortress
	Units command_centers = Observation()->GetUnits(Unit::Self, IsTownHall());
	if (!command_centers.empty()) {
		return command_centers.front();
	}
	return nullptr;
}

// If the mineral and food resources are available, return true
bool BasicSc2Bot::CanBuild(const int32_t mineral, const int32_t gas,
	const int32_t food) const {
	const ObservationInterface* obs = Observation();
	return obs->GetMinerals() >= mineral && obs->GetVespene() >= gas &&
		obs->GetFoodCap() - obs->GetFoodUsed() >= food;
}

// Check if the enemy is nearby
bool BasicSc2Bot::EnemyNearby(const Point2D& pos, const bool worker,
	const int32_t distance) {
	// if enemy units are within a certain radius (run!!!!)
	const ObservationInterface* obs = Observation();
	Units enemy_units = obs->GetUnits(Unit::Alliance::Enemy);
	bool enemy_nearby = false;
	for (const auto& e : enemy_units) {
		if (IsTrivialUnit(e) || (worker * IsWorkerUnit(e)))
			continue;
		if (Distance2D(e->pos, pos) < 15) {
			enemy_nearby = true;
			break;
		}
	}
	return enemy_nearby;
}

// Returns how close the current resources are to the resource goal
std::vector<float> BasicSc2Bot::HowCloseToResourceGoal(const int32_t& m,
	const int32_t& g) const {
	const ObservationInterface* obs = Observation();
	return { obs->GetMinerals() / static_cast<float>(m),
			obs->GetVespene() / static_cast<float>(g) };
}

// How much of the current job is finished
float BasicSc2Bot::HowClosetoFinishCurrentJob(const Unit* b) const {
	return b->orders.empty() ? -1.0f : b->orders.front().progress;
}

// Check if the building is still under construction
void BasicSc2Bot::IsBuildingProgress() {
	const ObservationInterface* obs = Observation();
	Units buildings =
		obs->GetUnits(Unit::Alliance::Self, [this](const Unit& b) {
		return BuildingsBeingBuiltFilter(b);
			});

	// Cancel the building if the builder is getting damaged
	for (const auto& b : buildings) {
		auto it = buildings_in_progress.find(b->tag);
		if (it != buildings_in_progress.end()) {

			if (!b->is_alive) {
				buildings_in_progress.erase(it);
			}
			else {
				if (b->build_progress != 1.0f &&
					b->build_progress == it->second) {
					Actions()->UnitCommand(b,
						ABILITY_ID::CANCEL_BUILDINPROGRESS);
				}
				else {
					it->second = b->build_progress;
				}
			}
		}
		else {
			buildings_in_progress[b->tag] = b->build_progress;
		}
	}
	return;
}

// Check if the builder is getting damaged
void BasicSc2Bot::IsBuilderGettingDamaged() {
	const ObservationInterface* obs = Observation();
	Units scvs = obs->GetUnits(Unit::Alliance::Self, [this](const Unit& u) {
		return !u.orders.empty() && IsBuildingOrder(u.orders.front());
		});
	for (const auto& scv : scvs) {
		auto it = builders_container.find(scv->tag);
		if (it != builders_container.end()) {
			if (!scv->is_alive) {
				builders_container.erase(it);
			}
			else if (scv->health < it->second) {
				it->second = scv->health;
				Actions()->UnitCommand(scv, ABILITY_ID::HALT);
			}
		}
		else {
			builders_container[scv->tag] = scv->health;
		}
	}
	return;
}

// Main base is complete and need expanding
bool BasicSc2Bot::NeedExpansion() const {
	const ObservationInterface* observation = Observation();

	// Check if we have any bases
	Units all_bases = observation->GetUnits(Unit::Self, IsTownHall());
	Units bases;

	// Filter out completed bases
	for (const auto& base : all_bases) {
		if (base->build_progress == 1.0f) {
			bases.emplace_back(base);
		}
	}

	if (bases.empty()) {
		return true; // Need to rebuild if all bases are lost
	}

	const size_t max_bases = 3;
	if (bases.size() >= max_bases) {
		return false; // Do not expand if we've reached the maximum number of
		// bases
	}

	// Calculate total ideal workers
	size_t total_ideal_workers = 0;
	for (const auto& base : bases) {
		total_ideal_workers += base->ideal_harvesters;
	}

	// Get current number of SCVs
	size_t num_scvs =
		observation
		->GetUnits(Unit::Alliance::Self, IsUnit(UNIT_TYPEID::TERRAN_SCV))
		.size();

	// Expand when we have enough SCVs to saturate our current bases
	return num_scvs >= 0.95f * total_ideal_workers;
}

Point3D BasicSc2Bot::GetNextExpansion() const {
	const ObservationInterface* observation = Observation();

	// Check if we have enough resources to expand
	if (observation->GetMinerals() < 400) {
		return Point3D(0.0f, 0.0f, 0.0f);
	}

	const std::vector<Point3D>& expansions = expansion_locations;
	Units townhalls = observation->GetUnits(Unit::Alliance::Self, IsTownHall());

	if (townhalls.empty() || GetMainBase() == nullptr) {
		return Point3D(0.0f, 0.0f, 0.0f);
	}

	// Find the closest unoccupied expansion location
	Point3D main_base_location = GetMainBase()->pos;
	float closest_distance = std::numeric_limits<float>::max();
	Point3D next_expansion = Point3D(0.0f, 0.0f, 0.0f);

	// Find the closest expansion that is not already occupied
	for (const auto& expansion_pos : expansions) {
		bool occupied = false;
		for (const auto& townhall : townhalls) {
			if (Distance2D(expansion_pos, townhall->pos) < 5.0f) {
				occupied = true;
				break;
			}
		}

		// Skip this expansion if it is already occupied
		if (occupied) {
			continue;
		}

		// Find the closest expansion to the main base
		float distance = DistanceSquared3D(expansion_pos, main_base_location);

		// Update the closest expansion if the current one is closer
		if (distance < closest_distance) {
			closest_distance = distance;
			next_expansion = expansion_pos;
		}
	}
	return next_expansion;
}

// Get a safe position for the main base
Point2D BasicSc2Bot::GetSafePosition() {
	const Unit* main_base = GetMainBase();
	return main_base ? main_base->pos : Point2D(0, 0);
}

// Find the closest damaged unit for repair
const Unit* BasicSc2Bot::FindDamagedUnit() {
	Units damaged_units =
		Observation()->GetUnits(Unit::Alliance::Self, [](const Unit& unit) {
		return unit.health < unit.health_max &&
			(unit.unit_type == UNIT_TYPEID::TERRAN_BATTLECRUISER ||
				unit.unit_type == UNIT_TYPEID::TERRAN_SIEGETANK ||
				unit.unit_type == UNIT_TYPEID::TERRAN_SIEGETANKSIEGED);
			});
	return !damaged_units.empty() ? damaged_units.front() : nullptr;
}

// Find the closest damaged structure for repair
const Unit* BasicSc2Bot::FindDamagedStructure() {
	const auto& units = Observation()->GetUnits(Unit::Alliance::Self);

	const Unit* highest_priority_target = nullptr;
	int highest_priority = std::numeric_limits<int>::max();
	float lowest_health = std::numeric_limits<float>::max();

	// Find the closest damaged structure for repair
	for (const auto& unit : units) {
		if ((unit->health < (unit->health_max * 0.9)) && unit->is_alive) {
			int priority = std::numeric_limits<int>::max();

			// Assign priorities (lower number = higher priority)
			if (unit->unit_type == UNIT_TYPEID::TERRAN_SUPPLYDEPOT ||
				unit->unit_type == UNIT_TYPEID::TERRAN_SUPPLYDEPOTLOWERED ||
				unit->unit_type == UNIT_TYPEID::TERRAN_BARRACKS ||
				unit->unit_type == UNIT_TYPEID::TERRAN_FACTORY ||
				unit->unit_type == UNIT_TYPEID::TERRAN_BARRACKSTECHLAB ||
				unit->unit_type == UNIT_TYPEID::TERRAN_FACTORYTECHLAB) {
				priority = 1;
			}
			else if (unit->unit_type == UNIT_TYPEID::TERRAN_COMMANDCENTER ||
				unit->unit_type == UNIT_TYPEID::TERRAN_ORBITALCOMMAND ||
				unit->unit_type == UNIT_TYPEID::TERRAN_STARPORT ||
				unit->unit_type == UNIT_TYPEID::TERRAN_STARPORTTECHLAB ||
				unit->unit_type == UNIT_TYPEID::TERRAN_ENGINEERINGBAY ||
				unit->unit_type == UNIT_TYPEID::TERRAN_ARMORY ||
				unit->unit_type == UNIT_TYPEID::TERRAN_FUSIONCORE ||
				unit->unit_type == UNIT_TYPEID::TERRAN_MISSILETURRET ||
				unit->unit_type == UNIT_TYPEID::TERRAN_REFINERY) {
				priority = 2;
			}

			// Update the target if the current unit has a higher priority
			if (priority < highest_priority || (priority == highest_priority &&
				unit->health < lowest_health)) {
				// Skip if the priority is too high (Units)
				if (priority > 2) {
					continue;
				}
				highest_priority_target = unit;
				highest_priority = priority;
				lowest_health = unit->health;
			}
		}
	}

	return highest_priority_target;
}

// Check if the main base is under attack
bool BasicSc2Bot::IsWorkerUnit(const Unit* unit) {
	return unit->unit_type == UNIT_TYPEID::TERRAN_SCV ||
		unit->unit_type == UNIT_TYPEID::PROTOSS_PROBE ||
		unit->unit_type == UNIT_TYPEID::ZERG_DRONE ||
		unit->unit_type == UNIT_TYPEID::TERRAN_MULE;
}

bool BasicSc2Bot::IsTrivialUnit(const Unit* unit) const {
	return unit->unit_type == UNIT_TYPEID::ZERG_OVERLORD ||
		unit->unit_type == UNIT_TYPEID::ZERG_OVERSEER ||
		unit->unit_type == UNIT_TYPEID::ZERG_OVERSEERSIEGEMODE ||
		unit->unit_type == UNIT_TYPEID::ZERG_CHANGELING ||
		unit->unit_type == UNIT_TYPEID::ZERG_CHANGELINGMARINE ||
		unit->unit_type == UNIT_TYPEID::ZERG_CHANGELINGMARINESHIELD ||
		unit->unit_type == UNIT_TYPEID::PROTOSS_OBSERVER ||
		unit->unit_type == UNIT_TYPEID::PROTOSS_OBSERVERSIEGEMODE;
}

bool BasicSc2Bot::IsBuildingOrder(const UnitOrder& order) const {
	return order.ability_id == ABILITY_ID::BUILD_ARMORY ||
		order.ability_id == ABILITY_ID::BUILD_BARRACKS ||
		order.ability_id == ABILITY_ID::BUILD_BUNKER ||
		order.ability_id == ABILITY_ID::BUILD_COMMANDCENTER ||
		order.ability_id == ABILITY_ID::BUILD_ENGINEERINGBAY ||
		order.ability_id == ABILITY_ID::BUILD_FACTORY ||
		order.ability_id == ABILITY_ID::BUILD_FUSIONCORE ||
		order.ability_id == ABILITY_ID::BUILD_GHOSTACADEMY ||
		order.ability_id == ABILITY_ID::BUILD_MISSILETURRET ||
		order.ability_id == ABILITY_ID::BUILD_REFINERY ||
		order.ability_id == ABILITY_ID::BUILD_STARPORT ||
		order.ability_id == ABILITY_ID::BUILD_SUPPLYDEPOT;
}

bool BasicSc2Bot::ALLBuildingsFilter(const Unit& unit) const {
	return (!unit.tag || unit.tag || unit.build_progress < 1.0f ||
		unit.display_type == 4);
}

bool BasicSc2Bot::BuildingsBeingBuiltFilter(const Unit& unit) const {
	return (unit.tag && unit.build_progress < 1.0f);
}

// Modify IsMainBaseUnderAttack() to consider only combat units
bool BasicSc2Bot::IsMainBaseUnderAttack() {
	const Unit* main_base = GetMainBase();
	if (!main_base) {
		return false;
	}

	// Check if there are enemy combat units near our main base
	Units enemy_units_near_base = Observation()->GetUnits(
		Unit::Alliance::Enemy, [this, main_base](const Unit& unit) {
			float distance = Distance2D(unit.pos, main_base->pos);
			if (distance < 25.0f && !IsWorkerUnit(&unit) &&
				!IsTrivialUnit(&unit)) {
				return true;
			}
			return false;
		});

	return !enemy_units_near_base.empty();
}

// Find the closest enemy unit to a given position
const Unit* BasicSc2Bot::FindClosestEnemy(const Point2D& pos) {
	const Unit* closest_enemy = nullptr;
	float closest_distance = std::numeric_limits<float>::max();

	for (const auto& unit : Observation()->GetUnits(Unit::Alliance::Enemy)) {
		float distance = Distance2D(pos, unit->pos);
		if (distance < closest_distance) {
			closest_distance = distance;
			closest_enemy = unit;
		}
	}
	return closest_enemy;
}

// Check if the unit has a specific ability
const Unit* BasicSc2Bot::FindUnit(sc2::UnitTypeID unit_type) const {
	const ObservationInterface* observation = Observation();
	Units units =
		observation->GetUnits(Unit::Alliance::Self, IsUnit(unit_type));
	for (const auto& unit : units) {
		// Exclude SCVs that are currently constructing
		if (unit->orders.empty() ||
			unit->orders.front().ability_id == ABILITY_ID::HARVEST_GATHER) {
			return unit;
		}
	}
	return nullptr;
}

// Returns the count of units of a given type
bool BasicSc2Bot::TryBuildStructureAtLocation(
	ABILITY_ID ability_type_for_structure, UNIT_TYPEID unit_type,
	const Point2D& location) {
	const Unit* builder = FindUnit(unit_type);
	Units gas_scvs = GetAllSCVsGettingGas();
	auto it = FindInVector(gas_scvs, builder);

	if (builder && it == gas_scvs.end() &&
		scvs_repairing.find(builder->tag) == scvs_repairing.end()) {
		if (Query()->Placement(ability_type_for_structure, location, builder)) {
			Actions()->UnitCommand(builder, ability_type_for_structure,
				location);
			return true;
		}
	}
	return false;
}

// Returns the count of units of a given type
Point2D BasicSc2Bot::GetRallyPoint() {
	const Unit* main_base = GetMainBase();
	if (main_base) {
		// Adjust the rally point as needed
		return Point2D(main_base->pos.x + 15.0f, main_base->pos.y);
	}
	// Fallback position
	return Point2D(0.0f, 0.0f);
}

void BasicSc2Bot::SetRallyPoint(const Unit* b, const Point2D& p) {
	Actions()->UnitCommand(b, ABILITY_ID::RALLY_BUILDING, p);
	return;
}

const Unit* BasicSc2Bot::GetLeastSaturatedBase() const {
	const ObservationInterface* observation = Observation();
	Units bases = observation->GetUnits(Unit::Alliance::Self, IsTownHall());
	const Unit* least_saturated_base = nullptr;
	int max_worker_need = 0;

	// Find the least saturated base
	for (const auto& base : bases) {
		if (base->build_progress < 1.0f || base->is_flying) {
			continue; // Skip bases that are under construction or flying
		}

		int worker_need = base->ideal_harvesters - base->assigned_harvesters;
		if (worker_need > max_worker_need) {
			max_worker_need = worker_need;
			least_saturated_base = base;
		}
	}

	return least_saturated_base;
}

Point2D BasicSc2Bot::GetNearestSafePosition(const Point2D& pos) {
	const float safe_radius =
		15.0f; // Radius within which enemies make a position unsafe
	const float search_radius = 50.0f; // Search range to find safe positions
	const int grid_steps = 5;          // Granularity of the search grid

	// Get all enemy units
	Units enemy_units = Observation()->GetUnits(Unit::Alliance::Enemy);
	if (enemy_units.empty()) {
		return pos; // Return the original position if there are no enemies
	}

	// Check if a position is safe
	auto is_safe = [&enemy_units, safe_radius](const Point2D& candidate) {
		for (const auto& enemy : enemy_units) {
			if (enemy && Distance2D(enemy->pos, candidate) <
				safe_radius) { // Ensure enemy is not null
				return false; // Unsafe if an enemy is within the safe_radius
			}
		}
		return true; // Safe if no enemies are within the safe_radius
		};

	// Start searching for the nearest safe position
	Point2D nearest_safe_position = pos;
	float min_distance = std::numeric_limits<float>::max();

	// Search within a grid around the position
	for (float dx = -search_radius; dx <= search_radius; dx += grid_steps) {
		for (float dy = -search_radius; dy <= search_radius; dy += grid_steps) {
			Point2D candidate = pos + Point2D(dx, dy);
			if (is_safe(candidate)) {
				float distance = Distance2D(pos, candidate);
				if (distance < min_distance) {
					min_distance = distance;
					nearest_safe_position = candidate;
				}
			}
		}
	}

	// Return the nearest safe position (defaults to the original position if no
	// safe position is found)
	return nearest_safe_position;
}

// Returns true if any base is not full hp
bool BasicSc2Bot::IsAnyBaseUnderAttack() {
	const ObservationInterface* observation = Observation();
	Units bases = observation->GetUnits(Unit::Alliance::Self, IsTownHall());
	for (const auto& base : bases) {
		if (base->health < base->health_max) {
			return true;
		}
	}
	return false;
}

// Move our units to the enemy
void BasicSc2Bot::MoveToEnemy(const Units& marines, const Units& siege_tanks) {
	Units enemy_units = Observation()->GetUnits(Unit::Alliance::Enemy);

	// Find the closest enemy unit to the first marine
	if (marines.empty()) {
		return;
	}
	const Unit* first_marine = marines.front();
	const Unit* closest_enemy = nullptr;
	float closest_distance = std::numeric_limits<float>::max();
	for (const auto& enemy : enemy_units) {
		float distance = Distance2D(first_marine->pos, enemy->pos);
		if (distance < closest_distance) {
			closest_distance = distance;
			closest_enemy = enemy;
		}
	}

	// If no closest enemy is found, do nothing
	if (!closest_enemy) {
		return;
	}

	// Move all units to the closest enemy
	for (const auto& marine : marines) {
		Actions()->UnitCommand(marine, ABILITY_ID::MOVE_MOVE,
			closest_enemy->pos);
	}
	for (const auto& tank : siege_tanks) {
		Actions()->UnitCommand(tank, ABILITY_ID::MOVE_MOVE, closest_enemy->pos);
	}
}

// If the enemy researched an ability for their unit
bool BasicSc2Bot::HasAbility(const Unit* unit, AbilityID ability_id) {
	if (!unit) { // Null check
		return false;
	}

	// If the enemy has ability ready
	const auto& abilities = Query()->GetAbilitiesForUnit(unit);
	for (const auto& ability : abilities.abilities) {
		if (ability.ability_id == ability_id) {
			return true;
		}
	}
	return false;
}

// How many units of a given type are in combat
int BasicSc2Bot::UnitsInCombat(UNIT_TYPEID unit_type) {
	int num_unit = 0;

	// Get all units of the specified type
	for (const auto& unit :
		Observation()->GetUnits(Unit::Alliance::Self, IsUnit(unit_type))) {

		bool is_near_enemy = false;

		// Check proximity to enemy units
		for (const auto& enemy_unit :
			Observation()->GetUnits(Unit::Alliance::Enemy)) {

			// Do not count trivial units
			if (IsTrivialUnit(enemy_unit)) {
				continue;
			}

			// Check if the unit is near an enemy
			float distance_to_enemy = Distance2D(unit->pos, enemy_unit->pos);
			if (distance_to_enemy <= 15.0f) {
				is_near_enemy = true;
				break;
			}
		}

		// Count unit if it is near at least one enemy
		if (is_near_enemy) {
			num_unit++;
		}
	}

	return num_unit;
}

const Unit* BasicSc2Bot::FindNearestMineralPatch() {
	// Find closest mineral patches
	Units mineral_patches = Observation()->GetUnits(
		Unit::Alliance::Neutral, IsUnit(UNIT_TYPEID::NEUTRAL_MINERALFIELD));
	const Unit* closest_mineral = nullptr;
	float min_distance = std::numeric_limits<float>::max();

	for (const auto& mineral : mineral_patches) {
		float distance = sc2::Distance2D(start_location, mineral->pos);
		if (distance < min_distance) {
			min_distance = distance;
			closest_mineral = mineral;
		}
	}
	if (closest_mineral != nullptr) {
		Point3D p = closest_mineral->pos + Point3D(1.0f, 1.0f, 1.0f);
		Debug()->DebugBoxOut(closest_mineral->pos, p);
	}
	return closest_mineral;
};

const Unit* BasicSc2Bot::FindRefinery() {
	// Find all refineries
	Units refineries = Observation()->GetUnits(
		Unit::Alliance::Self, IsUnit(UNIT_TYPEID::TERRAN_REFINERY));

	// Find a refinery with fewer than 3 workers
	const Unit* target_refinery = nullptr;

	if (!refineries.empty()) {
		for (const auto& refinery : refineries) {
			if (refinery->assigned_harvesters < refinery->ideal_harvesters) {
				target_refinery = refinery;
				break;
			}
		}
	}
	return target_refinery;
};

void BasicSc2Bot::HarvestIdleWorkers(const Unit* unit) {

	auto scv_repair = scvs_repairing.find(unit->tag);

	if (!unit || unit == scv_scout || unit == scv_building) {
		return;
	}

	// Find a refinery with fewer than 3 workers
	const Unit* target_refinery = FindRefinery();

	// Assign the SCV to the refinery if found
	if (target_refinery) {
		Actions()->UnitCommand(unit, ABILITY_ID::HARVEST_GATHER,
			target_refinery);
		return;
	}

	// Otherwise, find the closest mineral patch
	const Unit* closest_mineral = FindNearestMineralPatch();
	// Assign the SCV to harvest minerals if a mineral patch is found
	if (closest_mineral) {
		Actions()->UnitCommand(unit, ABILITY_ID::HARVEST_GATHER,
			closest_mineral);
		return;
	}
}

// Returns all SCVs that are currently gathering gas
Units BasicSc2Bot::GetAllSCVsGettingGas() const {
	const ObservationInterface* obs = Observation();
	Units gas_buildings = obs->GetUnits(Unit::Alliance::Self,
		IsUnit(UNIT_TYPEID::TERRAN_REFINERY));
	Units scvs =
		obs->GetUnits(Unit::Alliance::Self, IsUnit(UNIT_TYPEID::TERRAN_SCV));
	;
	// Find all SCVs that are currently gathering gas
	Units gas_scvs;
	for (const auto& scv : scvs) {
		for (const auto& gas_b : gas_buildings) {
			if (scv->orders.size() >= 1 &&
				scv->orders.front().ability_id == ABILITY_ID::HARVEST_GATHER &&
				scv->orders.front().target_unit_tag == gas_b->tag) {
				gas_scvs.emplace_back(scv);
			}
		}
	}
	return gas_scvs;
}
