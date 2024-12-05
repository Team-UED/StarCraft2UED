#include "BasicSc2Bot.h"

using namespace sc2;

void BasicSc2Bot::ManageEconomy() {
	TrainSCVs();
	AssignWorkers();
	TryBuildSupplyDepot();
	BuildRefineries();
	if (current_gameloop % 25 == 0) {
		IsBuilderGettingDamaged();
		IsBuildingProgress();
	}
	BuildExpansion();
	ReassignWorkers();
	UseMULE();
	UseScan();
}

void BasicSc2Bot::TrainSCVs() {
	const ObservationInterface* obs = Observation();

	// Get all bases
	Units command_centers = obs->GetUnits(Unit::Alliance::Self, IsTownHall());
	if (command_centers.empty())
		return;

	// Calculate desired SCV count based on ideal harvesters
	size_t desired_scv_count = 0;
	for (const auto& base : command_centers) {
		if (base->build_progress == 1.0f && !base->is_flying) {
			desired_scv_count += base->ideal_harvesters;
		}
	}
	desired_scv_count += 5; // Additional SCVs for building and contingency

	// If we have enough SCVs, return
	Units scvs =
		obs->GetUnits(Unit::Alliance::Self, IsUnit(UNIT_TYPEID::TERRAN_SCV));
	if (scvs.size() >= desired_scv_count)
		return;

	// Get all completed Supply Depots
	Units dps = obs->GetUnits(Unit::Alliance::Self, [](const Unit& unit) {
		return unit.unit_type == UNIT_TYPEID::TERRAN_SUPPLYDEPOT &&
			!(unit.build_progress < 0.5f);
		});

	// Ensure we have enough supply
	if (obs->GetFoodUsed() >= obs->GetFoodCap() - 1 && !dps.empty()) {
		return; // Avoid training if we're near supply cap
	}

	// Train SCVs at available Command Centers
	for (const auto& cc : command_centers) {
		if (cc->build_progress < 1.0f || cc->is_flying) {
			continue; // Skip unfinished or flying Command Centers
		}

		// Check if we are already training an SCV
		bool is_training_scv = false;
		for (const auto& order : cc->orders) {
			if (order.ability_id == ABILITY_ID::TRAIN_SCV) {
				is_training_scv = true;
				break;
			}
		}
		if (!is_training_scv && obs->GetMinerals() >= 50) {
			Actions()->UnitCommand(cc, ABILITY_ID::TRAIN_SCV);
		}
	}
}

void BasicSc2Bot::UseMULE() {
	const ObservationInterface* observation = Observation();

	// Find all Orbital Commands
	Units orbital_commands = observation->GetUnits(
		Unit::Alliance::Self, IsUnit(UNIT_TYPEID::TERRAN_ORBITALCOMMAND));

	// No Orbital Commands found
	if (orbital_commands.empty()) {
		return;
	}

	float energy_cost = 0.0f;

	if (!first_battlecruiser) {
		energy_cost = 50.0f;
	}
	else {
		energy_cost = 100.0f;
	}

	// Loop Orbital Command to check if it has enough energy
	for (const auto& orbital : orbital_commands) {
		if (orbital->energy >= energy_cost) {
			// Find the nearest mineral patch to the Orbital Command
			Units mineral_patches = observation->GetUnits(
				Unit::Alliance::Neutral, IsMineralPatch());
			const Unit* closest_mineral = nullptr;
			float min_distance = std::numeric_limits<float>::max();

			for (const auto& mineral : mineral_patches) {
				float distance = sc2::Distance2D(orbital->pos, mineral->pos);
				if (distance < min_distance) {
					min_distance = distance;
					closest_mineral = mineral;
				}
			}

			// use MULE on nearest mineral patch
			if (closest_mineral) {
				Actions()->UnitCommand(orbital, ABILITY_ID::EFFECT_CALLDOWNMULE,
					closest_mineral);
				return;
			}
		}
	}
}

void BasicSc2Bot::UseScan() {
	const ObservationInterface* observation = Observation();

	// Find all Orbital Commands
	Units orbital_commands = observation->GetUnits(
		Unit::Alliance::Self, IsUnit(UNIT_TYPEID::TERRAN_ORBITALCOMMAND));

	// No Orbital Commands found
	if (orbital_commands.empty()) {
		return;
	}

	// Find all cloacked enemies
	Units enemies = observation->GetUnits(Unit::Alliance::Enemy);
	const Unit* cloacked_enemy = nullptr;
	for (const auto& enemy : enemies) {
		if (enemy->cloak == 1) {
			cloacked_enemy = enemy;
		}
	}

	// Scan cloacked enemy
	float energy_cost = 50.0f;
	if (cloacked_enemy) {
		// Loop Orbital Command to check if it has enough energy
		for (const auto& orbital : orbital_commands) {
			if (orbital->energy >= energy_cost) {
				Actions()->UnitCommand(orbital, ABILITY_ID::EFFECT_SCAN,
					cloacked_enemy->pos);
			}
		}
	}
}

bool BasicSc2Bot::TryBuildStructure(ABILITY_ID ability_type_for_structure,
	UNIT_TYPEID unit_type) {
	const ObservationInterface* obs = Observation();
	if (!CanBuild(100)) {
		return false; // Not enough minerals to build
	}

	// Max distance from base center
	const float base_radius = 25.0f;

	// Minimum distance from mineral
	const float distance_from_minerals = 10.0f;

	// Find an SCV to build with
	Units scvs =
		obs->GetUnits(Unit::Alliance::Self, IsUnit(UNIT_TYPEID::TERRAN_SCV));
	Units scvs_gas = GetAllSCVsGettingGas();

	// Check if we have a builder
	const Unit* builder = nullptr;
	for (const auto& scv : scvs) {
		bool IsHoldingGas = scv->buffs.empty()
			? false
			: scv->buffs.front() ==
			BUFF_ID::CARRYHARVESTABLEVESPENEGEYSERGAS;
		// Check if the SCV is scv_scout
		// Check if the SCV is a gas harvester
		// Check if the SCV is already repairing
		auto it = FindInVector(scvs_gas, scv);
		if (scv == scv_scout || it != scvs_gas.end() ||
			scvs_repairing.find(scv->tag) != scvs_repairing.end() ||
			IsHoldingGas) {
			continue;
		}

		// Check if the SCV is already building
		float min_distance_to_base = std::numeric_limits<float>::max();
		float distance_to_cc;
		for (const auto& cc : bases) {
			// Check if the SCV is within the base radius
			if (ability_type_for_structure == ABILITY_ID::BUILD_SUPPLYDEPOT &&
				cc != bases[0]) {
				continue;
			}
			distance_to_cc = Distance2D(scv->pos, cc->pos);
			if (distance_to_cc < min_distance_to_base) {
				min_distance_to_base = distance_to_cc;
			}
		}
		// Check if the SCV is within the base radius
		if (min_distance_to_base > base_radius) {
			continue;
		}
		// Check if the SCV is already building
		for (const auto& order : scv->orders) {
			if (!IsBuildingOrder(order) && !builder) {
				builder = scv;
				break;
			}
		}
		if (builder) {
			break;
		}
	}

	// ------------------------------
	// Ramp Blocking logic
	// ------------------------------
	// Check if we have a builder
	if (builder) {
		if (ability_type_for_structure == ABILITY_ID::BUILD_SUPPLYDEPOT) {
			// check if ramp is blocked
			for (size_t i = 0; i < ramp_depots.size(); ++i) {

				if (ramp_depots[i]) {
					continue;
				}
				if (!EnemyNearby(mainBase_depot_points[i], false) &&
					Query()->Placement(ABILITY_ID::BUILD_SUPPLYDEPOT,
						mainBase_depot_points[i])) {
					scv_building = builder;
					Actions()->UnitCommand(builder, ability_type_for_structure,
						mainBase_depot_points[i], true);
					return true;
				}
				return false;
			}
			return depot_area_check(builder, ability_type_for_structure,
				base_location);
		}

		else if (ability_type_for_structure == ABILITY_ID::BUILD_BARRACKS) {
			Units barracks =
				obs->GetUnits(Unit::Alliance::Self, [this](const Unit& unit) {
				return unit.unit_type == UNIT_TYPEID::TERRAN_BARRACKS &&
					ALLBuildingsFilter(unit);
					});

			if (barracks.size() < 2) {
				// check if ramp is blocked
				if (phase == 0) {
					if (Query()->Placement(ABILITY_ID::BUILD_BARRACKS,
						mainBase_barrack_point)) {
						Actions()->UnitCommand(scv_building,
							ability_type_for_structure,
							mainBase_barrack_point, true);
						return true;
					}
					return false;
				}
				else {
					// ramp is blocked?
					if (!ramp_middle[0] &&
						ramp_mid_destroyed->unit_type ==
						UNIT_TYPEID::TERRAN_BARRACKS &&
						!EnemyNearby(mainBase_barrack_point, false)) {
						Actions()->UnitCommand(scv_building,
							ability_type_for_structure,
							mainBase_barrack_point, true);
						return true;
					}
					// this barrack is not a ramp building but it was destroyed
					// possibly
					else {
						// if addons are still there, build around the addons
						Units addons_t = obs->GetUnits(
							Unit::Alliance::Self, [](const Unit& unit) {
								return unit.unit_type ==
									UNIT_TYPEID::TERRAN_TECHLAB;
							});
						Units addons_r = obs->GetUnits(
							Unit::Alliance::Self, [](const Unit& unit) {
								return unit.unit_type ==
									UNIT_TYPEID::TERRAN_REACTOR;
							});
						addons_t.insert(addons_t.end(), addons_r.begin(),
							addons_r.end());

						const Unit* tl;
						if (!addons_t.empty()) {
							tl = addons_t.front();
							Point2D tl_p = Point2D(tl->pos);
							tl_p = Point2D(tl_p.x - 2.5f, tl_p.y + 0.5f);
							if (!EnemyNearby(tl_p, false) &&
								Query()->Placement(ability_type_for_structure,
									tl_p)) {
								Actions()->UnitCommand(
									builder, ability_type_for_structure, tl_p);
								return true;
							}
						}
					}
					return build33_after_check(builder,
						ability_type_for_structure,
						base_location, true);
				}
			}
		}
		else if (ability_type_for_structure == ABILITY_ID::BUILD_FACTORY) {
			Units factory =
				obs->GetUnits(Unit::Alliance::Self, [this](const Unit& unit) {
				return unit.unit_type == UNIT_TYPEID::TERRAN_FACTORY &&
					ALLBuildingsFilter(unit);
					});
			if (factory.empty()) {
				// check if ramp is blocked
				if (phase < 2) {
					return build33_after_check(builder,
						ability_type_for_structure,
						base_location, true);
				}
				else if (!ramp_middle[0] &&
					ramp_mid_destroyed->unit_type ==
					UNIT_TYPEID::TERRAN_FACTORY &&
					!EnemyNearby(mainBase_barrack_point)) {
					Actions()->UnitCommand(scv_building,
						ability_type_for_structure,
						mainBase_barrack_point, true);
					return true;
				}
				// this factory is not a ramp building but it was destroyed
				// possibly
				else {
					Units addons_t = obs->GetUnits(
						Unit::Alliance::Self, [](const Unit& unit) {
							return unit.unit_type ==
								UNIT_TYPEID::TERRAN_TECHLAB;
						});
					const Unit* tl;
					if (!addons_t.empty()) {
						tl = addons_t.front();
						Point2D tl_p = Point2D(tl->pos);
						tl_p = Point2D(tl_p.x - 2.5f, tl_p.y + 0.5f);
						if (!EnemyNearby(tl_p, false) &&
							Query()->Placement(ability_type_for_structure,
								tl_p)) {
							Actions()->UnitCommand(
								builder, ability_type_for_structure, tl_p);
							return true;
						}
					}
				}
				return build33_after_check(builder, ability_type_for_structure,
					base_location, true);
			}
		}
		else if (ability_type_for_structure == ABILITY_ID::BUILD_STARPORT) {
			Units starport =
				obs->GetUnits(Unit::Alliance::Self, [this](const Unit& unit) {
				return unit.unit_type == UNIT_TYPEID::TERRAN_STARPORT &&
					ALLBuildingsFilter(unit);
					});
			if (starport.empty()) {
				// check if ramp is blocked
				if (phase == 2) {
					return build33_after_check(builder,
						ability_type_for_structure,
						base_location, true);
				}

				// after phase 2, this means possibly starport is destroyed
				else {
					Units addons_t = obs->GetUnits(
						Unit::Alliance::Self, [](const Unit& unit) {
							return unit.unit_type ==
								UNIT_TYPEID::TERRAN_TECHLAB;
						});
					const Unit* tl;
					if (!addons_t.empty()) {
						tl = addons_t.front();
						Point2D tl_p = Point2D(tl->pos);
						tl_p = Point2D(tl_p.x - 2.5f, tl_p.y + 0.5f);
						if (!EnemyNearby(tl_p, false) &&
							Query()->Placement(ability_type_for_structure,
								tl_p)) {
							Actions()->UnitCommand(
								builder, ability_type_for_structure, tl_p);
							return true;
						}
					}
				}
				return build33_after_check(builder, ability_type_for_structure,
					base_location, true);
			}
		}
		else {
			// check far away from the ramp first around the edge of the base
			// for 3x3 buildings
			return build33_after_check(builder, ability_type_for_structure,
				base_location, false);
		}
	}
	return false;
}

bool BasicSc2Bot::TryBuildSupplyDepot() {
	// Get supply used and supply cap
	const ObservationInterface* obs = Observation();
	int32_t supply_used = obs->GetFoodUsed();
	int32_t supply_cap = obs->GetFoodCap();

	// Default surplus
	int32_t supply_surplus = 1;
	// Set surplus by phase
	if (phase == 2) {
		supply_surplus = 6;
	}
	else if (phase == 3) {
		supply_surplus = 8;
	}

	// block ramp right while building barracks
	Units barracks = obs->GetUnits(Unit::Alliance::Self,
		IsUnit(UNIT_TYPEID::TERRAN_BARRACKS));
	if (ramp_depots[0] && phase == 0 && !barracks.empty()) {
		if (!ramp_depots[1]) {
			return TryBuildStructure(ABILITY_ID::BUILD_SUPPLYDEPOT,
				UNIT_TYPEID::TERRAN_SCV);
		}
	}

	// block ramp left while building factory
	if (phase > 1) {
		for (const auto& d : ramp_depots) {
			if (!d) {
				return TryBuildStructure(ABILITY_ID::BUILD_SUPPLYDEPOT,
					UNIT_TYPEID::TERRAN_SCV);
			}
		}
	}

	// Build a supply depot when supply used reaches the threshold
	if (supply_used >= supply_cap - supply_surplus) {
		// Check if a supply depot is already under construction
		Units supply_depots_building =
			obs->GetUnits(Unit::Self, [this](const Unit& unit) {
			return unit.unit_type == UNIT_TYPEID::TERRAN_SUPPLYDEPOT &&
				(!unit.tag || unit.build_progress < 1.0f ||
					unit.display_type == 4);
				});

		if (phase != 3) {
			if (supply_depots_building.empty()) {
				// std::cout << "Building Supply Depot" << std::endl;
				return TryBuildStructure(ABILITY_ID::BUILD_SUPPLYDEPOT,
					UNIT_TYPEID::TERRAN_SCV);
			}
		}
		else if (phase == 3) {
			if (supply_depots_building.size() < 2 &&
				current_gameloop % 50 == 0) {
				return TryBuildStructure(ABILITY_ID::BUILD_SUPPLYDEPOT,
					UNIT_TYPEID::TERRAN_SCV);
			}
		}
	}
	return false;
}

void BasicSc2Bot::AssignWorkers() {
	const ObservationInterface* obs = Observation();

	// Get idle SCVs
	Units idle_scvs = obs->GetUnits(Unit::Alliance::Self, [](const Unit& unit) {
		return unit.unit_type == UNIT_TYPEID::TERRAN_SCV && unit.orders.empty();
		});

	Units scvs_not_holding =
		obs->GetUnits(Unit::Alliance::Self, [](const Unit& unit) {
		return unit.unit_type == UNIT_TYPEID::TERRAN_SCV &&
			!unit.orders.empty() &&
			unit.orders.front().ability_id ==
			ABILITY_ID::HARVEST_GATHER &&
			unit.buffs.empty();
			});

	// Get bases and refineries
	Units bases = obs->GetUnits(Unit::Alliance::Self, IsTownHall());
	Units refineries =
		obs->GetUnits(Unit::Alliance::Self, [](const Unit& unit) {
		return unit.unit_type == UNIT_TYPEID::TERRAN_REFINERY &&
			unit.build_progress == 1.0f;
			});

	// Keep track of workers per refinery
	std::map<const Unit*, int> refinery_worker_count;
	for (const auto& refinery : refineries) {
		refinery_worker_count[refinery] =
			refinery->assigned_harvesters; // Current workers on the refinery
	}

	// Map bases to nearby minerals
	std::map<const Unit*, Units> base_minerals_map;
	for (const auto& base : bases) {
		Units nearby_minerals =
			obs->GetUnits(Unit::Alliance::Neutral, [base](const Unit& unit) {
			return IsMineralPatch()(unit) &&
				Distance2D(unit.pos, base->pos) < 10.0f;
				});
		base_minerals_map[base] = nearby_minerals;
	}
	if (!idle_scvs.empty()) {
		// Assign idle SCVs to tasks
		for (const auto& scv : idle_scvs) {
			// Skip SCVs that are scouting or repairing
			if (scv == scv_scout ||
				scvs_repairing.find(scv->tag) != scvs_repairing.end()) {
				continue;
			}

			// Assign SCV to a Refinery with fewer than 3 workers
			const Unit* target_refinery = nullptr;
			for (const auto& refinery : refineries) {
				if (refinery_worker_count[refinery] < 3) {
					target_refinery = refinery;
					++refinery_worker_count[refinery]; // Increment worker count
					// for
					break;
				}
			}
			if (target_refinery) {
				Actions()->UnitCommand(scv, ABILITY_ID::HARVEST_GATHER,
					target_refinery);
				continue;
			}

			// Assign SCV to the nearest mineral patch
			const Unit* closest_mineral = FindNearestMineralPatch();
			if (closest_mineral) {
				Actions()->UnitCommand(scv, ABILITY_ID::HARVEST_GATHER,
					closest_mineral);
			}
		}
	}
	else if (!refineries.empty() && idle_scvs.empty()) {
		for (const auto& scv : scvs_not_holding) {
			// Skip SCVs that are scouting or repairing
			if (scv == scv_scout ||
				scvs_repairing.find(scv->tag) != scvs_repairing.end()) {
				continue;
			}

			// Assign SCV to a Refinery with fewer than 3 workers
			const Unit* target_refinery = nullptr;
			for (const auto& refinery : refineries) {
				if (refinery_worker_count[refinery] < 3) {
					target_refinery = refinery;
					++refinery_worker_count[refinery]; // Increment worker count
					// for
					break;
				}
			}
			if (target_refinery) {
				if (Distance2D(scv->pos, target_refinery->pos) < 15.0f) {
					Actions()->UnitCommand(scv, ABILITY_ID::HARVEST_GATHER,
						target_refinery);
				}
				continue;
			}
		}
	}
}

void BasicSc2Bot::ReassignWorkers() {
	const ObservationInterface* obs = Observation();

	Units bases = obs->GetUnits(Unit::Alliance::Self, IsTownHall());
	Units refineries = obs->GetUnits(Unit::Alliance::Self,
		IsUnit(UNIT_TYPEID::TERRAN_REFINERY));

	// Collect under-saturated and over-saturated bases
	std::vector<const Unit*> under_saturated_bases;
	std::vector<const Unit*> over_saturated_bases;

	for (const auto& base : bases) {
		if (base->build_progress < 1.0f || base->is_flying) {
			continue; // Skip unfinished or flying bases
		}

		int worker_diff = base->ideal_harvesters - base->assigned_harvesters;
		if (worker_diff > 0) {
			under_saturated_bases.emplace_back(base);
		}
		else if (worker_diff < 0) {
			over_saturated_bases.emplace_back(base);
		}
	}

	// Reassign workers from over-saturated to under-saturated bases
	for (const auto& over_base : over_saturated_bases) {
		int excess_workers =
			over_base->assigned_harvesters - over_base->ideal_harvesters;

		Units workers_at_base =
			obs->GetUnits(Unit::Alliance::Self, [over_base](const Unit& unit) {
			return unit.unit_type == UNIT_TYPEID::TERRAN_SCV &&
				!unit.orders.empty() &&
				Distance2D(unit.pos, over_base->pos) < 10.0f;
				});

		for (int i = 0; i < excess_workers && i < workers_at_base.size(); ++i) {
			const Unit* worker = workers_at_base[i];

			// Assign to the closest under-saturated base
			const Unit* closest_base = nullptr;
			float min_distance = std::numeric_limits<float>::max();

			for (const auto& under_base : under_saturated_bases) {
				float distance = Distance2D(worker->pos, under_base->pos);
				if (distance < min_distance) {
					min_distance = distance;
					closest_base = under_base;
				}
			}

			if (closest_base) {
				// Assign worker to a mineral patch near the under-saturated
				// base
				Units minerals = obs->GetUnits(
					Unit::Alliance::Neutral, [closest_base](const Unit& unit) {
						return IsMineralPatch()(unit) &&
							Distance2D(unit.pos, closest_base->pos) < 10.0f;
					});

				if (!minerals.empty()) {
					const Unit* closest_mineral = *std::min_element(
						minerals.begin(), minerals.end(),
						[worker](const Unit* a, const Unit* b) {
							return Distance2D(worker->pos, a->pos) <
								Distance2D(worker->pos, b->pos);
						});

					Actions()->UnitCommand(worker, ABILITY_ID::HARVEST_GATHER,
						closest_mineral);
				}
			}
		}
	}

	// Handle over-saturated refineries
	for (const auto& refinery : refineries) {
		int excess_workers =
			refinery->assigned_harvesters - refinery->ideal_harvesters;

		if (excess_workers > 0) {
			Units gas_workers = obs->GetUnits(
				Unit::Alliance::Self, [refinery](const Unit& unit) {
					return unit.unit_type == UNIT_TYPEID::TERRAN_SCV &&
						!unit.orders.empty() &&
						unit.orders.front().target_unit_tag == refinery->tag;
				});

			for (int i = 0; i < excess_workers && i < gas_workers.size(); ++i) {
				const Unit* worker = gas_workers[i];

				// Assign worker to the closest mineral patch
				Units minerals = obs->GetUnits(
					Unit::Alliance::Neutral, [worker](const Unit& unit) {
						return IsMineralPatch()(unit);
					});

				if (!minerals.empty()) {
					const Unit* closest_mineral = *std::min_element(
						minerals.begin(), minerals.end(),
						[worker](const Unit* a, const Unit* b) {
							return Distance2D(worker->pos, a->pos) <
								Distance2D(worker->pos, b->pos);
						});

					Actions()->UnitCommand(worker, ABILITY_ID::HARVEST_GATHER,
						closest_mineral);
				}
			}
		}
	}
}

void BasicSc2Bot::BuildRefineries() {

	const ObservationInterface* obs = Observation();
	Units cc = bases;

	// Build refineries near each base
	for (const auto& base : cc) {
		Units geysers =
			obs->GetUnits(Unit::Alliance::Neutral, [base](const Unit& unit) {
			return IsGeyser()(unit) &&
				Distance2D(unit.pos, base->pos) < 15.0f;
				});

		// Build a refinery on top of each geyser
		for (const auto& geyser : geysers) {
			Units refineries =
				obs->GetUnits(Unit::Alliance::Self, [geyser](const Unit& unit) {
				return unit.unit_type == UNIT_TYPEID::TERRAN_REFINERY &&
					Distance2D(unit.pos, geyser->pos) < 1.0f;
					});
			Units barracks =
				obs->GetUnits(Unit::Alliance::Self, [](const Unit& unit) {
				return unit.unit_type == UNIT_TYPEID::TERRAN_BARRACKS &&
					unit.build_progress < 1.0f;
					});

			// Check if a refinery is already being built
			if (refineries.empty() && obs->GetMinerals() >= 75 &&
				(!barracks.empty() || phase)) {
				Units scvs = obs->GetUnits(Unit::Alliance::Self,
					IsUnit(UNIT_TYPEID::TERRAN_SCV));
				Units scvs_gas = GetAllSCVsGettingGas();
				const Unit* builder = nullptr;

				// Find an idle SCV to build the refinery
				for (const auto& scv : scvs) {
					auto it = FindInVector(scvs_gas, scv);
					if (scvs_repairing.find(scv->tag) != scvs_repairing.end() ||
						it != scvs_gas.end()) {
						continue;
					}
					bool is_constructing = false;
					for (const auto& order : scv->orders) {
						if (order.ability_id == ABILITY_ID::BUILD_SUPPLYDEPOT ||
							order.ability_id == ABILITY_ID::BUILD_REFINERY ||
							order.ability_id == ABILITY_ID::BUILD_BARRACKS ||
							order.ability_id ==
							ABILITY_ID::BUILD_COMMANDCENTER) {
							is_constructing = true;
							break;
						}
					}

					// Check if the SCV is idle (has no orders)
					if (!is_constructing && scv != scv_scout &&
						scvs_repairing.find(scv->tag) == scvs_repairing.end()) {
						builder = scv;
						break;
					}
				}

				// Build the refinery
				if (builder) {
					if (Query()->Placement(ABILITY_ID::BUILD_REFINERY,
						geyser->pos)) {
						Actions()->UnitCommand(
							builder, ABILITY_ID::BUILD_REFINERY, geyser);
					}
				}
			}
		}
	}
}

void BasicSc2Bot::BuildExpansion() {
	const ObservationInterface* observation = Observation();

	// Check if the first battlecruiser is in production
	if (!first_battlecruiser) {
		return;
	}

	// Check if we have enough resources to expand
	if (!CanBuild(550)) {
		return;
	}

	// Check if we need to expand
	if (!NeedExpansion()) {
		return;
	}

	// Check if a Command Center is already being built
	Units command_centers_building =
		observation->GetUnits(Unit::Self, [](const Unit& unit) {
		return (unit.unit_type == UNIT_TYPEID::TERRAN_COMMANDCENTER ||
			unit.unit_type == UNIT_TYPEID::TERRAN_COMMANDCENTERFLYING ||
			unit.unit_type == UNIT_TYPEID::TERRAN_ORBITALCOMMAND ||
			unit.unit_type ==
			UNIT_TYPEID::TERRAN_ORBITALCOMMANDFLYING ||
			unit.unit_type == UNIT_TYPEID::TERRAN_PLANETARYFORTRESS) &&
			unit.build_progress < 1.0f;
			});

	if (!command_centers_building.empty()) {
		return; // An expansion is already being built
	}

	Point3D next_expansion = GetNextExpansion();
	if (next_expansion == Point3D(0.0f, 0.0f, 0.0f)) {
		return;
	}

	// Check if the expansion location is safe
	if (EnemyNearby(next_expansion)) {
		return; // Don't expand to a location that's under threat
	}

	Units scvs = observation->GetUnits(Unit::Alliance::Self,
		IsUnit(UNIT_TYPEID::TERRAN_SCV));
	Units scvs_gas = GetAllSCVsGettingGas();
	const Unit* builder = nullptr;

	// Find an idle SCV to build the expansion
	for (const auto& scv : scvs) {
		// Skip SCVs that are in the scvs_repairing set
		// Check if the SCV is a gatherer
		auto it = FindInVector(scvs_gas, scv);
		if (scvs_repairing.find(scv->tag) != scvs_repairing.end() ||
			it != scvs_gas.end()) {
			continue;
		}

		// Check if the SCV is idle (has no orders)
		if (!scv->orders.empty() &&
			scv->orders.front().ability_id == ABILITY_ID::HARVEST_GATHER) {
			builder = scv;
			break;
		}
	}
	if (builder) { // Build the expansion
		if (!EnemyNearby(next_expansion, false, 20) &&
			Query()->Placement(ABILITY_ID::BUILD_COMMANDCENTER,
				next_expansion)) {
			Actions()->UnitCommand(builder, ABILITY_ID::BUILD_COMMANDCENTER,
				next_expansion);
		}
		return;
	}
}
