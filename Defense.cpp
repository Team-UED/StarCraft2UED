#include "BasicSc2Bot.h"

// Manages defensive structures and units.
void BasicSc2Bot::Defense() {
    BlockChokepoint();
    EarlyDefense();
    LateDefense();
}

// Blocks the chokepoint with buildings for early defense.
void BasicSc2Bot::BlockChokepoint() {
    // Check if we've already blocked the chokepoint
    if (chokepoint_blocked) {
        return;
    }

    // Get and validate the chokepoint position
    Point2D chokepoint = GetChokepointPosition();
    if (!Observation()->IsPathable(chokepoint)) {
        return;
    }

    // Check for existing supply depots and their completion
    Units existing_structures = Observation()->GetUnits(Unit::Alliance::Self);
    for (const auto& structure : existing_structures) {
        if (structure->unit_type == UNIT_TYPEID::TERRAN_SUPPLYDEPOT ||
            structure->unit_type == UNIT_TYPEID::TERRAN_SUPPLYDEPOTLOWERED) {
            for (int i = 0; i < 2; ++i) {
                Point2D expected_pos = chokepoint + Point2D(i * 3.0f, 0.0f);
                if (Distance2D(structure->pos, expected_pos) < 0.5f && 
                    structure->build_progress == 1.0f) {
                    supply_depots_built[i] = true;
                }
            }
        }
    }

    // Build Supply Depots to form a wall
    for (int i = 0; i < 2; ++i) {
        if (!supply_depots_built[i] && Observation()->GetMinerals() >= 100) {
            Point2D depot_position = chokepoint + Point2D(i * 3.0f, 0.0f);
            
            // Verify position is buildable
            if (Observation()->IsPathable(depot_position)) {
                if (TryBuildStructureAtLocation(ABILITY_ID::BUILD_SUPPLYDEPOT, 
                                              UNIT_TYPEID::TERRAN_SCV, 
                                              depot_position)) {
                    supply_depots_built[i] = true;
                    return; // Build one structure at a time
                }
            }
        }
    }

    // Build a Barracks to complete the wall
    if (supply_depots_built[0] && supply_depots_built[1] && !barracks_built && 
        Observation()->GetMinerals() >= 150) {
        Point2D barracks_position = chokepoint + Point2D(1.5f, -2.5f);
        
        if (Observation()->IsPathable(barracks_position)) {
            if (TryBuildStructureAtLocation(ABILITY_ID::BUILD_BARRACKS, 
                                          UNIT_TYPEID::TERRAN_SCV, 
                                          barracks_position)) {
                barracks_built = true;
                return;
            }
        }
    }

    // Verify all structures are completed before marking chokepoint as blocked
    if (supply_depots_built[0] && supply_depots_built[1] && barracks_built) {
        bool all_complete = true;
        for (const auto& structure : existing_structures) {
            if ((structure->unit_type == UNIT_TYPEID::TERRAN_SUPPLYDEPOT ||
                 structure->unit_type == UNIT_TYPEID::TERRAN_SUPPLYDEPOTLOWERED ||
                 structure->unit_type == UNIT_TYPEID::TERRAN_BARRACKS) &&
                structure->build_progress < 1.0f) {
                all_complete = false;
                break;
            }
        }
        if (all_complete) {
            chokepoint_blocked = true;
        }
    }
}

void BasicSc2Bot::EarlyDefense() {
    if (!IsAnyBaseUnderAttack()) {
        return;
    }

    // Get all our bases
    Units our_bases = Observation()->GetUnits(Unit::Alliance::Self, IsTownHall());

    // Collect enemy units near our bases
    Units enemy_units;
    for (const auto& base : our_bases) {
        Units enemies_near_base = Observation()->GetUnits(Unit::Alliance::Enemy, [base](const Unit& unit) {
            return Distance2D(unit.pos, base->pos) < 20.0f &&
                   unit.display_type == Unit::DisplayType::Visible;
        });
        enemy_units.insert(enemy_units.end(), enemies_near_base.begin(), enemies_near_base.end());
    }

    if (enemy_units.empty()) {
        return;
    }

    // Find the highest threat enemy unit
    const Unit* primary_target = nullptr;
    for (const auto& enemy : enemy_units) {
        if (!primary_target || enemy->weapon_cooldown < primary_target->weapon_cooldown) {
            primary_target = enemy;
        }
    }

    // Get and manage our combat units
    Units marines = Observation()->GetUnits(Unit::Alliance::Self,
                                            IsUnit(UNIT_TYPEID::TERRAN_MARINE));
    Units siege_tanks = Observation()->GetUnits(
        Unit::Alliance::Self, IsUnit(UNIT_TYPEID::TERRAN_SIEGETANK));

    // Command marines
    for (const auto &marine : marines) {
        if (marine->orders.empty() ||
            marine->orders.front().ability_id != ABILITY_ID::ATTACK) {
            Actions()->UnitCommand(marine, ABILITY_ID::ATTACK,
                                   primary_target ? primary_target->pos
                                                  : enemy_units.front()->pos);
        }
    }

    // Command siege tanks
    for (const auto &tank : siege_tanks) {
        if (tank->orders.empty()) {
            // Move siege tanks to the target area or chokepoint
            if (primary_target) {
                Actions()->UnitCommand(tank, ABILITY_ID::MOVE_MOVE,
                                       primary_target->pos);
            } else {
                // If no primary target exists, move to a defensive position or
                // chokepoint
                Point2D chokepoint = GetChokepointPosition();
                Actions()->UnitCommand(tank, ABILITY_ID::MOVE_MOVE, chokepoint);
            }
        }

        // Siege the tank if it's near the chokepoint or target position
        if (primary_target &&
            Distance2D(tank->pos, primary_target->pos) < 10.0f) {
            Actions()->UnitCommand(tank, ABILITY_ID::MORPH_SIEGEMODE);
        } else if (!primary_target) {
            Point2D chokepoint = GetChokepointPosition();
            if (Distance2D(tank->pos, chokepoint) < 5.0f) {
                Actions()->UnitCommand(tank,
                                       ABILITY_ID::MORPH_SIEGEMODE);
            }
        }
    }


    // Determine if SCV response is needed
    float enemy_strength = static_cast<float>(enemy_units.size());
    float our_strength = static_cast<float>(marines.size());

    if (enemy_strength > our_strength) {
        int scvs_sent = 0;
        const int max_scvs_to_send = 5;
        Units scvs = Observation()->GetUnits(Unit::Alliance::Self, IsUnit(UNIT_TYPEID::TERRAN_SCV));

        for (const auto& scv : scvs) {
            // Only use SCVs that are idle or not gathering resources
            bool is_gathering = false;
            for (const auto& order : scv->orders) {
                if (order.ability_id == ABILITY_ID::HARVEST_GATHER || order.ability_id == ABILITY_ID::HARVEST_RETURN) {
                    is_gathering = true;
                    break;
                }
            }
            if (is_gathering) {
                continue; // Skip SCVs that are gathering resources
            }

            // Command SCV to attack
            Actions()->UnitCommand(scv, ABILITY_ID::ATTACK, primary_target ? primary_target->pos : enemy_units.front()->pos);
            scvs_sent++;
            if (scvs_sent >= max_scvs_to_send) {
                break;
            }
        }
    }
}

// Builds additional defense structures like Missile Turrets.
void BasicSc2Bot::LateDefense() {
    // Check if we have an Engineering Bay (required for Missile Turrets)
    Units engineering_bays = Observation()->GetUnits(Unit::Alliance::Self, IsUnit(UNIT_TYPEID::TERRAN_ENGINEERINGBAY));
    if (engineering_bays.empty()) {
        return; // Can't build Missile Turrets without an Engineering Bay
    }

    // Build Missile Turrets near bases and important locations
    Units missile_turrets = Observation()->GetUnits(Unit::Alliance::Self, IsUnit(UNIT_TYPEID::TERRAN_MISSILETURRET));
    // Limit the number of Missile Turrets
    const int desired_turrets = 2 * bases.size();
    if (missile_turrets.size() >= desired_turrets) {
        return;
    }

    for (const auto& base : bases) {
        // Build a Missile Turret near the base
        Point2D turret_position = base->pos + Point2D(5.0f, 0.0f); // Adjust as needed
        if (TryBuildStructureAtLocation(ABILITY_ID::BUILD_MISSILETURRET, UNIT_TYPEID::TERRAN_SCV, turret_position)) {
            return; // Build one turret at a time
        }
    }
}