#include "BasicSc2Bot.h"

// Main function to control SCVs
void BasicSc2Bot::ControlSCVs() {
    SCVScoutEnemySpawn();
    RetreatFromDanger();
    UpdateRepairingSCVs();
    RepairUnits();
    RepairStructures();
    UpdateRepairingSCVs();
    if (current_gameloop % 23 == 0) {
        SCVAttackEmergency();
    }
}

// SCVs scout the map to find enemy bases
void BasicSc2Bot::SCVScoutEnemySpawn() {
    const sc2::ObservationInterface *observation = Observation();

    // Check if we have enough SCVs
    sc2::Units scvs = observation->GetUnits(
        sc2::Unit::Alliance::Self, sc2::IsUnit(sc2::UNIT_TYPEID::TERRAN_SCV));

    if (scvs.empty() || scvs.size() < 12 || scout_complete) {
        return;
    }

    // Check if enemy start locations are available
    if (enemy_start_locations.empty()) {
        scout_complete = true;
        return;
    }

    if (is_scouting && scv_scout) {
        // Update the scouting SCV's current locationcd.
        scout_location = scv_scout->pos;

        // Check if SCV has reached the current target location
        float distance_to_target = sc2::Distance2D(
            scout_location,
            enemy_start_locations[current_scout_location_index]);
        if (distance_to_target < 5.0f) {
            // Check for enemy town halls
            sc2::Units enemy_structures = observation->GetUnits(
                sc2::Unit::Alliance::Enemy, [](const sc2::Unit &unit) {
                    return unit.unit_type ==
                               sc2::UNIT_TYPEID::TERRAN_COMMANDCENTER ||
                           unit.unit_type ==
                               sc2::UNIT_TYPEID::TERRAN_ORBITALCOMMAND ||
                           unit.unit_type ==
                               sc2::UNIT_TYPEID::TERRAN_COMMANDCENTERFLYING ||
                           unit.unit_type ==
                               sc2::UNIT_TYPEID::TERRAN_ORBITALCOMMANDFLYING ||
                           unit.unit_type ==
                               sc2::UNIT_TYPEID::TERRAN_PLANETARYFORTRESS ||
                           unit.unit_type == sc2::UNIT_TYPEID::ZERG_HATCHERY ||
                           unit.unit_type == sc2::UNIT_TYPEID::ZERG_LAIR ||
                           unit.unit_type == sc2::UNIT_TYPEID::ZERG_HIVE ||
                           unit.unit_type == sc2::UNIT_TYPEID::PROTOSS_NEXUS;
                });

            for (const auto &structure : enemy_structures) {
                if (sc2::Distance2D(scout_location, structure->pos) < 5.0f) {
                    // Set the enemy start location and stop scouting
                    enemy_start_location = structure->pos;

                    // Find the nearest corner to the enemy base
                    float min_corner_distance =
                        std::numeric_limits<float>::max();

                    for (const auto &corner : map_corners) {
                        float corner_distance =
                            DistanceSquared2D(enemy_start_location, corner);
                        if (corner_distance < min_corner_distance) {
                            min_corner_distance = corner_distance;
                            nearest_corner_enemy = corner;
                        }
                    }

                    // Find the corners adjacent to the enemy base
                    for (const auto &corner : map_corners) {
                        if (corner.x == nearest_corner_enemy.x ||
                            corner.y == nearest_corner_enemy.y) {
                            enemy_adjacent_corners.push_back(corner);
                        }
                    }

                    // Find the closest mineral patch to the start location
                    const Unit *closest_mineral = FindNearestMineralPatch();

                    // harvest mineral if a mineral patch is found
                    if (closest_mineral && scv_scout) {
                        Actions()->UnitCommand(scv_scout,
                                               ABILITY_ID::HARVEST_GATHER,
                                               closest_mineral);
                    }

                    // Mark scouting as complete
                    scv_scout = nullptr;
                    is_scouting = false;
                    scout_complete = true;
                    return;
                }
            }

            // Move to the next potential enemy location if no town hall is
            // found here
            current_scout_location_index++;
            // All locations have been checked. Mark scouting as complete
            if (current_scout_location_index >= enemy_start_locations.size()) {
                scv_scout = nullptr;
                is_scouting = false;
                scout_complete = true;
                return;
            }
            // Scout to the next location
            Actions()->UnitCommand(
                scv_scout, sc2::ABILITY_ID::MOVE_MOVE,
                enemy_start_locations[current_scout_location_index]);
        }
    } else {
        // Assign an SCV to scout when no SCVs are scouting
        for (const auto &scv : scvs) {
            Units gas_scvs = GetAllSCVsGettingGas();
            auto it = FindInVector(gas_scvs, scv);
            if (scv->orders.empty() && it == gas_scvs.end()) {
                scv_scout = scv;
                is_scouting = true;
                current_scout_location_index =
                    0; // Start from the first location

                // Set the initial position of the scouting SCV
                scout_location = scv->pos;

                // Command SCV to move to the initial possible enemy location
                Actions()->UnitCommand(
                    scv_scout, sc2::ABILITY_ID::MOVE_MOVE,
                    enemy_start_locations[current_scout_location_index]);
                break;
            }
        }
    }
}

// SCVs retreat from dangerous situations (e.g., enemy rushes)
void BasicSc2Bot::RetreatFromDanger() {
    // Iterate through all our units
    for (const auto &unit : Observation()->GetUnits(Unit::Alliance::Self)) {
        // Only consider SCVs that are not the scouting SCV
        if (unit->unit_type == UNIT_TYPEID::TERRAN_SCV && unit != scv_scout &&
            !(Distance2D(unit->pos, enemy_start_location) < 20.0f)) {

            // Skip SCVs that are actively attacking
            bool scv_is_attacking = false; // Renamed to avoid conflict
            for (const auto &order : unit->orders) {
                if (order.ability_id == ABILITY_ID::ATTACK) {
                    scv_is_attacking = true;
                    break;
                }
            }
            if (scv_is_attacking || unit->health == unit->health_max ||
                scvs_repairing.find(unit->tag) != scvs_repairing.end()) {
                continue; // Skip SCVs that are currently attacking, are full
                          // hp, or reparing
            }

            // If the SCV is in a dangerous position, make it retreat
            if (EnemyNearby(unit->pos, true, 5)) {
                /*if (current_gameloop % 24 == 0)
                    std::cout << "Retreating SCV" << std::endl;*/
                Point2D safe_position = GetNearestSafePosition(unit->pos);
                Actions()->UnitCommand(unit, ABILITY_ID::MOVE_MOVE,
                                       safe_position);
            }
        }
    }
}

// SCVs repair damaged Battlecruisers during or after engagements
void BasicSc2Bot::RepairUnits() {

    // Radius around the base considered "at base".
    const float base_radius = 20.0f;
    const Unit *target = FindDamagedUnit();

    if (target) {
        for (const auto &scv_tag : scvs_repairing) {
            const Unit *scv = Observation()->GetUnit(scv_tag);

            // Skip if the SCV is invalid
            if (!scv || !scv->is_alive) {
                continue;
            }

            // Check if SCV is already repairing
            bool is_repairing = false;
            for (const auto &order : scv->orders) {
                if (order.ability_id == ABILITY_ID::EFFECT_REPAIR ||
                    order.ability_id == ABILITY_ID::EFFECT_REPAIR_SCV) {
                    is_repairing = true;
                    break;
                }
            }

            // Repair the target if it is at the base
            bool is_at_base =
                sc2::Distance2D(target->pos, start_location) <= base_radius;
            if (is_at_base && !is_repairing) {
                Actions()->UnitCommand(scv, ABILITY_ID::EFFECT_REPAIR, target);
            }
        }
    }
}

// SCVs repair damaged structures during enemy attacks
void BasicSc2Bot::RepairStructures() {
    const Unit *target = FindDamagedStructure();

    // Repair the target if it is at the base
    if (target) {
        for (const auto &scv_tag : scvs_repairing) {
            const Unit *scv = Observation()->GetUnit(scv_tag);

            // Skip if the SCV is invalid
            if (!scv || !scv->is_alive) {
                continue;
            }

            // Check if SCV is already repairing
            bool is_repairing = false;
            for (const auto &order : scv->orders) {
                if (order.ability_id == ABILITY_ID::EFFECT_REPAIR ||
                    order.ability_id == ABILITY_ID::EFFECT_REPAIR_SCV) {
                    is_repairing = true;
                    break;
                }
            }

            // Skip SCV if it is already repairing
            if (!is_repairing) {
                Actions()->UnitCommand(scv, ABILITY_ID::EFFECT_REPAIR, target);
            }
        }
    }
}

void BasicSc2Bot::UpdateRepairingSCVs() {
    // if there are less than 6 repairing SCVs, add more to the scv_repairing
    // set
    const ObservationInterface *obs = Observation();

    Units scvs =
        obs->GetUnits(Unit::Alliance::Self, IsUnit(UNIT_TYPEID::TERRAN_SCV));
    Units scvs_gas = GetAllSCVsGettingGas();

    // Add SCVs to the repairing set
    for (const auto &scv : scvs) {
        if (scvs_repairing.size() < 6) {
            // Make sure it's not a gathering SCV
            auto it = FindInVector(scvs_gas, scv);
            if (it != scvs_gas.end()) {
                continue;
            } else {
                scvs_repairing.insert(scv->tag);
            }
        }
    }

    // Make repairing SCVs gather if there are no damaged units
    for (const auto &scv_tag : scvs_repairing) {
        const Unit *scv = obs->GetUnit(scv_tag);
        // Skip dead or invalid SCVs
        if (!scv || !scv->is_alive) {
            continue;
        }
        // Return SCVs to harvest when repair is complete
        if (!scv->orders.empty()) {
            // Check if the SCV is repairing
            const auto &order = scv->orders.front();
            if (order.ability_id == ABILITY_ID::EFFECT_REPAIR ||
                order.ability_id == ABILITY_ID::EFFECT_REPAIR_SCV) {
                // Check if the repair target is fully repaired
                const Unit *target = obs->GetUnit(order.target_unit_tag);
                if (target && target->health == target->health_max) {
                    // Otherwise, find the closest mineral patch
                    const Unit *closest_mineral = FindNearestMineralPatch();
                    // Assign the SCV to harvest minerals if a mineral patch is
                    // found
                    if (closest_mineral) {
                        /*std::cout << "Returning SCV to close mineral" <<
                         * std::endl;*/
                        Actions()->UnitCommand(scv, ABILITY_ID::HARVEST_GATHER,
                                               closest_mineral);
                    }
                }
            }
        }
    }
}

// SCVs attack in urgent situations (e.g., enemy attacking the main base)
void BasicSc2Bot::SCVAttackEmergency() {
    if (EnemyNearby(start_location, false, 25)) {

        // If there are significant enemy combat units, send SCVs to attack
        if (EnemyNearby(start_location)) {
            int scvs_sent = 0;
            const int max_scvs_to_send = 5; // Limit the number of SCVs
            const float max_distance_from_base =
                15.0f; // Maximum distance SCVs should go from the base

            const Unit *main_base = GetMainBase();
            if (!main_base) {
                return; // If we don't have a main base, exit
            }
            Units scvs = Observation()->GetUnits(
                Unit::Alliance::Self, IsUnit(UNIT_TYPEID::TERRAN_SCV));
            for (const auto &scv : scvs) {
                // Only use SCVs that have the repair tag
                if (scvs_repairing.find(scv->tag) != scvs_repairing.end()) {
                    // Ensure SCV does not move too far from the main base
                    if (Distance2D(scv->pos, main_base->pos) >
                        max_distance_from_base) {
                        continue; // Skip this SCV if it is too far from the
                    } // base

                    // Find the closest enemy unit to attack
                    const Unit *target = FindClosestEnemy(scv->pos);
                    if (target && !IsWorkerUnit(target) &&
                        Distance2D(target->pos, main_base->pos) <
                            max_distance_from_base) {
                        ++scvs_sent;
                        if (scvs_sent > max_scvs_to_send) {
                            break;
                        }
                        Actions()->UnitCommand(scv, ABILITY_ID::ATTACK, target);
                    }
                }
            }
        }
    }
}