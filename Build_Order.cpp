#include "BasicSc2Bot.h"

// Replace Sleep(10) with the following code
using namespace sc2;

void BasicSc2Bot::ExecuteBuildOrder() {
    // what do I do if some buildings are destroyed?
    BuildBarracks();
    BuildFactory();
    BuildOrbitalCommand();
    BuildAddon();
    BuildStarport();
    Swap(swap_a, swap_b, false);
    BuildFusionCore();

    if (current_gameloop % 46 == 0) {
        // Building one more battlecruiser might be more helpful??
        BuildEngineeringBay();
        // don't need
        BuildArmory();
    }
}

// Build Barracks if we have a Supply Depot and enough resources
void BasicSc2Bot::BuildBarracks() {
    const ObservationInterface *obs = Observation();

    // Get Supply Depots
    Units dps = obs->GetUnits(Unit::Alliance::Self, [this](const Unit &unit) {
        return (unit.unit_type == UNIT_TYPEID::TERRAN_SUPPLYDEPOT ||
                unit.unit_type == UNIT_TYPEID::TERRAN_SUPPLYDEPOTLOWERED) &&
               ALLBuildingsFilter(unit);
    });

    // Can't built Barracks without Supply Depots
    if (dps.empty()) {
        return;
    }

    // Rebuild Barracks on the ramp if it is destroyed
    if (ramp_mid_destroyed != nullptr &&
        ramp_mid_destroyed->unit_type == UNIT_TYPEID::TERRAN_BARRACKS &&
        CanBuild(150)) {
        TryBuildStructure(ABILITY_ID::BUILD_BARRACKS, UNIT_TYPEID::TERRAN_SCV);
        ramp_mid_destroyed = nullptr;
        return;
    }

    // Build only 1 Barrack
    Units barracks =
        obs->GetUnits(Unit::Alliance::Self, [this](const Unit &unit) {
            return unit.unit_type == UNIT_TYPEID::TERRAN_BARRACKS &&
                   ALLBuildingsFilter(unit);
        });

    // Build Barracks to block ramps at both bases if we have enough resources
    if (phase < 1) {
        if (barracks.empty() && CanBuild(150)) {
            TryBuildStructure(ABILITY_ID::BUILD_BARRACKS,
                              UNIT_TYPEID::TERRAN_SCV);
        }
    } else if (phase == 3) {
        if (barracks.size() < 2 && bases.size() > 1 && CanBuild(550) &&
            current_gameloop % 46 == 0) {
            TryBuildStructure(ABILITY_ID::BUILD_BARRACKS,
                              UNIT_TYPEID::TERRAN_SCV);
        }
    }
}

// Build Orbital Command if we have a Command Center and enough resources
void BasicSc2Bot::BuildOrbitalCommand() {
    const ObservationInterface *obs = Observation();

    // Can't build Orbital Command without Barracks or Factories
    if (!num_barracks || !num_factories) {
        return;
    }

    // Find a Command Center that can be upgraded
    Units command_centers = obs->GetUnits(
        Unit::Alliance::Self, IsUnit(UNIT_TYPEID::TERRAN_COMMANDCENTER));

    if (command_centers.empty()) {
        return;
    }

    for (const auto &cc : command_centers) {
        // Check if this Command Center is already being upgraded
        bool is_upgrading = false;
        for (const auto &order : cc->orders) {
            if (order.ability_id == ABILITY_ID::MORPH_ORBITALCOMMAND) {
                is_upgrading = true;
                break;
            }
        }
        // If its not upgrading, upgrade it
        if (!is_upgrading && CanBuild(150)) {
            Actions()->UnitCommand(cc, ABILITY_ID::MORPH_ORBITALCOMMAND, true);
            return;
        }
    }
}

// Build Factory if we have a Barracks and enough resources
void BasicSc2Bot::BuildFactory() {
    const ObservationInterface *observation = Observation();

    // Can't build Factory without Barracks
    if (!num_barracks) {
        return;
    }

    if (ramp_mid_destroyed != nullptr &&
        ramp_mid_destroyed->unit_type == UNIT_TYPEID::TERRAN_FACTORY &&
        CanBuild(150, 100)) {
        TryBuildStructure(ABILITY_ID::BUILD_FACTORY, UNIT_TYPEID::TERRAN_SCV);
        ramp_mid_destroyed = nullptr;
        return;
    }

    // Build only 1 Factory
    Units factories =
        observation->GetUnits(Unit::Alliance::Self, [this](const Unit &unit) {
            return unit.unit_type == UNIT_TYPEID::TERRAN_FACTORY ||
                   unit.unit_type == UNIT_TYPEID::TERRAN_FACTORYFLYING;
        });
    if (factories.empty() && CanBuild(150, 100)) {
        TryBuildStructure(ABILITY_ID::BUILD_FACTORY, UNIT_TYPEID::TERRAN_SCV);
    }
}

// Build Starport if we have a Factory and enough resources
void BasicSc2Bot::BuildStarport() {
    const ObservationInterface *obs = Observation();

    // Can't build Starports without Factories
    if (!num_factories) {
        return;
    }

    // Build only 1 Starport
    Units starports =
        obs->GetUnits(Unit::Alliance::Self, [this](const Unit &unit) {
            return unit.unit_type == UNIT_TYPEID::TERRAN_STARPORT ||
                   unit.unit_type == UNIT_TYPEID::TERRAN_STARPORTFLYING;
        });

    if (starports.empty() && CanBuild(150, 100)) {
        TryBuildStructure(ABILITY_ID::BUILD_STARPORT, UNIT_TYPEID::TERRAN_SCV);
    }
}

// Build Tech lab if we have a Factory and enough resources
void BasicSc2Bot::BuildAddon() {
    if (!swap_in_progress) {
        const ObservationInterface *obs = Observation();
        // Get Barracks
        Units barracks =
            obs->GetUnits(Unit::Alliance::Self, [](const Unit &unit) {
                return unit.unit_type == UNIT_TYPEID::TERRAN_BARRACKS &&
                       !unit.is_flying && !unit.add_on_tag;
            });

        // Get Factories
        Units factories =
            obs->GetUnits(Unit::Alliance::Self, [](const Unit &unit) {
                return unit.unit_type == UNIT_TYPEID::TERRAN_FACTORY &&
                       !unit.is_flying && !unit.add_on_tag;
            });

        // Get Starports
        Units starports =
            obs->GetUnits(Unit::Alliance::Self, [](const Unit &unit) {
                return unit.unit_type == UNIT_TYPEID::TERRAN_STARPORT &&
                       !unit.is_flying && !unit.add_on_tag;
            });

        Units barracks_techlab =
            obs->GetUnits(Unit::Alliance::Self, [](const Unit &unit) {
                return unit.unit_type == UNIT_TYPEID::TERRAN_BARRACKSTECHLAB;
            });

        // use 3 bits to represent buildings without any addons
        char addon_bits = 0;
        addon_bits |= (!barracks.empty() ? 1 : 0);
        addon_bits |= (!factories.empty() ? 2 : 0);
        addon_bits |= (!starports.empty() ? 4 : 0);

        // any is not empty then..
        if (addon_bits != 0) {

            // enough resources
            if (CanBuild(50, 25)) {
                const Unit *building = nullptr;
                for (size_t i = 0; i < 3; ++i) {
                    if (addon_bits & 1) {
                        ABILITY_ID build_order = ABILITY_ID::BUILD_TECHLAB;
                        // Get the first building without an addon
                        switch (i) {
                        case 0:
                            building = barracks.front();
                            break;
                        case 1:
                            building = factories.front();
                            break;
                        case 2:
                            building = starports.front();
                            break;
                        }
                        // second barracks
                        if (!barracks.empty() && barracks_techlab.size()) {
                            if (CanBuild(450, 350)) {
                                build_order = ABILITY_ID::BUILD_REACTOR;
                            } else {
                                break;
                            }
                        }
                        // Build addon
                        if (!EnemyNearby(building->pos, true)) {
                            Actions()->UnitCommand(building, build_order);
                            break;
                        }
                    }
                    addon_bits >>= 1;
                }
            }
        }
    }
    return;
}

// Build Fusion Core if we have a Starport and enough resources
void BasicSc2Bot::BuildFusionCore() {
    const ObservationInterface *obs = Observation();

    // Can't build fusion core without Starports
    if (!num_starports || num_fusioncores) {
        return;
    }

    // Build only 1 Fusion core
    Units fusioncore =
        obs->GetUnits(Unit::Alliance::Self, [this](const Unit &unit) {
            return unit.unit_type == UNIT_TYPEID::TERRAN_FUSIONCORE &&
                   ALLBuildingsFilter(unit);
        });

    if (fusioncore.empty() && CanBuild(150, 125)) {
        // TODO: reduce this call?
        TryBuildStructure(ABILITY_ID::BUILD_FUSIONCORE,
                          UNIT_TYPEID::TERRAN_SCV);
    }
}

// Build Armory if we have a Fusion core and enough resources
void BasicSc2Bot::BuildArmory() {
    const ObservationInterface *obs = Observation();

    // Can't build Armory core without the First Battlecruiser
    if (!first_battlecruiser) {
        return;
    }

    // Build only 1 Armory
    Units armories =
        obs->GetUnits(Unit::Alliance::Self, [this](const Unit &unit) {
            return unit.unit_type == UNIT_TYPEID::TERRAN_ARMORY &&
                   ALLBuildingsFilter(unit);
        });

    if (armories.empty() && CanBuild(400 + 150, 300 + 50)) {
        TryBuildStructure(ABILITY_ID::BUILD_ARMORY, UNIT_TYPEID::TERRAN_SCV);
    }
}

// Build Engineering bay if we have a Barrack and enough resources
void BasicSc2Bot::BuildEngineeringBay() {
    const ObservationInterface *obs = Observation();

    // Get Barracks
    Units barracks =
        obs->GetUnits(Unit::Alliance::Self, [this](const Unit &unit) {
            return unit.unit_type == UNIT_TYPEID::TERRAN_BARRACKS &&
                   ALLBuildingsFilter(unit);
        });
    // Get Startports
    Units starports =
        obs->GetUnits(Unit::Alliance::Self, [this](const Unit &unit) {
            return unit.unit_type == UNIT_TYPEID::TERRAN_STARPORT &&
                   ALLBuildingsFilter(unit);
        });

    // Can't build Engineering bay without Barracks
    if (barracks.empty() || !first_battlecruiser) {
        return;
    }

    // Build only 1 engineering bay (After Starports)
    Units engineeringbays =
        obs->GetUnits(Unit::Alliance::Self, [this](const Unit &unit) {
            return unit.unit_type == UNIT_TYPEID::TERRAN_ENGINEERINGBAY &&
                   ALLBuildingsFilter(unit);
        });

    if (!engineeringbays.size() && !starports.empty() && bases.size() > 1 &&
        CanBuild(250)) {
        TryBuildStructure(ABILITY_ID::BUILD_ENGINEERINGBAY,
                          UNIT_TYPEID::TERRAN_SCV);
    }
}

// Swaps a Factory with a Starport
void BasicSc2Bot::Swap(const Unit *a, const Unit *b, bool lift) {

    // lift buildings
    if (lift) {
        swap_in_progress = true;
        Actions()->UnitCommand(a, ABILITY_ID::LIFT);
        Actions()->UnitCommand(b, ABILITY_ID::LIFT);
        swap_a = a;
        swap_b = b;
    } else {
        if (swap_in_progress && a->is_flying && b->is_flying) {
            const ObservationInterface *obs = Observation();
            Point2D swap_position_a = a->pos;
            Point2D swap_position_b = b->pos;
            Actions()->UnitCommand(a, ABILITY_ID::LAND, swap_position_b);
            Actions()->UnitCommand(b, ABILITY_ID::LAND, swap_position_a);
            swap_in_progress = false;
        }
    }
    return;
}
