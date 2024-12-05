#include "BasicSc2Bot.h"

BasicSc2Bot::BasicSc2Bot()
    : current_build_order_index(0), num_scvs(12), num_marines(0),
      num_battlecruisers(0), num_siege_tanks(0), num_barracks(0),
      num_factories(0), num_starports(0), num_fusioncores(0), step_counter(0),
      phase(0), is_under_attack(false), is_attacking(false),
      need_expansion(false), game_time(0.0),
      enemy_strategy(EnemyStrategy::Unknown), swap_in_progress(false),
      ramp_mid_destroyed(nullptr), first_battlecruiser(false),
      first_battlecruiser_trained(false), is_scouting(false),
      scout_complete(false), current_scout_location_index(0),
      scv_scout(nullptr), nearest_corner_ally(0.0f, 0.0f),
      nearest_corner_enemy(0.0f, 0.0f), rally_barrack(0.0f, 0.0f),
      rally_factory(0.0f, 0.0f), rally_starport(0.0f, 0.0f),
      attack_target(0.0f, 0.0f) {

    build_order = {
        ABILITY_ID::BUILD_SUPPLYDEPOT, ABILITY_ID::BUILD_BARRACKS,
        ABILITY_ID::BUILD_REFINERY,    ABILITY_ID::BUILD_ENGINEERINGBAY,
        ABILITY_ID::BUILD_BUNKER,      ABILITY_ID::BUILD_FACTORY,
        ABILITY_ID::BUILD_STARPORT,    ABILITY_ID::BUILD_TECHLAB,
        ABILITY_ID::BUILD_FUSIONCORE,  ABILITY_ID::BUILD_COMMANDCENTER};
}

// =================================================================================
//
//                            DEBUGGING FUNCTIONS START
//
// =================================================================================
static uint32_t c_text_size = 11;
std::string last_action_text_;

// Draw a box around each point on the map
void BasicSc2Bot::DrawBoxesOnMap(sc2::DebugInterface *debug, uint32_t map_width,
                                 uint32_t map_height) {
    for (uint32_t x = 0; x < map_width; ++x) {
        for (uint32_t y = 0; y < map_height; ++y) {

            uint32_t z = height_at(Point2DI(x, y));
            sc2::Point3D p_min(0, 0, z);
            sc2::Point3D p_max(0.5, 0.5, z + 0.5);
            debug->DebugBoxOut(p_min, p_max, sc2::Colors::Red);
        }
    }
    debug->SendDebug();
}

// Draw a box at a specific location on the map
void BasicSc2Bot::DrawBoxAtLocation(sc2::DebugInterface *debug,
                                    const sc2::Point3D &location, float size,
                                    const sc2::Color &color) const {
    // Calculate the minimum and maximum points of the box
    sc2::Point3D p_min = location;
    p_min.x -= size / 2.0f;
    p_min.y -= size / 2.0f;

    sc2::Point3D p_max = location;
    p_max.x += size / 2.0f;
    p_max.y += size / 2.0f;

    // Keep the z-coordinate constant to make the box flat
    p_min.z = location.z;
    p_max.z = location.z + 2.0f;

    debug->DebugBoxOut(p_min, p_max, color);
}

// Get the ability text for a given ability ID
static std::string GetAbilityText(sc2::AbilityID ability_id) {
    std::string str;
    str += sc2::AbilityTypeToName(ability_id);
    str += " (";
    str += std::to_string(uint32_t(ability_id));
    str += ")";
    return str;
}

// Echo the action to the screen
void EchoAction(const sc2::RawActions &actions, sc2::DebugInterface *debug,
                const sc2::Abilities &) {
    if (actions.size() < 1) {
        debug->DebugTextOut(last_action_text_);
        return;
    }
    last_action_text_ = "";

    // For now, just show the first action.
    const sc2::ActionRaw &action = actions[0];

    // Show the index of the ability being used.
    last_action_text_ = GetAbilityText(action.ability_id);

    // Add targeting information.
    switch (action.target_type) {
    case sc2::ActionRaw::TargetUnitTag:
        last_action_text_ +=
            "\nTargeting Unit: " + std::to_string(action.target_tag);
        break;
    case sc2::ActionRaw::TargetPosition:
        last_action_text_ +=
            "\nTargeting Pos: " + std::to_string(action.target_point.x) + ", " +
            std::to_string(action.target_point.y);
        break;
    case sc2::ActionRaw::TargetNone:
    default:
        last_action_text_ += "\nTargeting self";
    }

    debug->DebugTextOut(last_action_text_);
}

// Get real time in minutes and seconds
std::vector<uint32_t> BasicSc2Bot::GetRealTime() const {
    const ObservationInterface *obs = Observation();
    uint32_t game_loop = obs->GetGameLoop();
    float real_time_seconds = game_loop / 22.4f;

    uint32_t minutes = static_cast<int>(real_time_seconds) / 60;
    uint32_t seconds = static_cast<int>(real_time_seconds) % 60;

    return {minutes, seconds};
}

// Debugging function
void BasicSc2Bot::Debugging() {
    Control()->GetObservation();

    const sc2::ObservationInterface *obs = Observation();
    sc2::QueryInterface *query = Query();
    sc2::DebugInterface *debug = Debug();

    Units scvs_building =
        obs->GetUnits(Unit::Alliance::Self, [this](const Unit &unit) {
            return unit.unit_type == UNIT_TYPEID::TERRAN_SCV &&
                   !unit.orders.empty() && IsBuildingOrder(unit.orders.front());
        });

    // Draw the build order
    for (const auto &scv : scvs_building) {
        const UnitOrder &order = scv->orders.front();
        const Unit *target = obs->GetUnit(order.target_unit_tag);
        if (target) {
            debug->DebugLineOut(scv->pos, target->pos, sc2::Colors::Yellow);
            debug->SendDebug();
        }
        DrawBoxAtLocation(debug, scv->pos, 2.0f, sc2::Colors::Red);
    }

    // Draw all the scvs that are getting gas
    Units gas_scvs = GetAllSCVsGettingGas();
    for (const auto &scv_g : gas_scvs) {
        // gas
        DrawBoxAtLocation(debug, scv_g->pos, 2.0f, sc2::Colors::Green);
    }

    // Draw all scvs that are assigned to repair
    for (const auto &scv_repairing : scvs_repairing) {
        const Unit *scv = obs->GetUnit(scv_repairing);
        if (scv) {
            // repairing
            DrawBoxAtLocation(debug, scv->pos, 2.0f, sc2::Colors::Yellow);
            if (std::find(gas_scvs.begin(), gas_scvs.end(), scv) !=
                gas_scvs.end()) {
                // this one is supposed to get gas only
                std::cout << "SCV " << scv->tag
                          << " is repairing? but should get gas" << std::endl;
            }
        }
    }

    // buildable map
    /*if (current_gameloop % 100)
    {
        for (const auto& i : build_map[0])
        {
            if (!i.second)
            {
                continue;
            }
            DrawBoxAtLocation(debug, Point3D(i.first.x + 0.5f, i.first.y + 0.5f,
    height_at_float(i.first) + 0.1f), 1.0f, sc2::Colors::Green);
        }
    }*/

    /*if (Control()->GetLastStatus() != SC2APIProtocol::Status::in_game)
        return;

    if (last_echoed_gameloop_ == obs->GetGameLoop())
        return;
    last_echoed_gameloop_ = obs->GetGameLoop();*/

    // Find a selected unit.
    // const sc2::Unit* unit = nullptr;
    // for (const auto& try_unit : obs->GetUnits()) {
    //	if (try_unit->is_selected && try_unit->alliance == sc2::Unit::Self) {
    //		unit = try_unit;
    //		break;
    //	}
    //}
    // if (!unit)
    //	return;

    //// Show the position of the selected unit.
    // std::string debug_txt = "(" + std::to_string(unit->pos.x) + ", " +
    // std::to_string(unit->pos.y) + ") | radius: " +
    // std::to_string(unit->radius) + " "; debug->DebugTextOut(debug_txt,
    // unit->pos, sc2::Colors::Green, c_text_size);
    debug->SendDebug();
}

// =================================================================================
//
//                              DEBUGGING FUNCTIONS END
//
// =================================================================================

// Start the bot
void BasicSc2Bot::on_start() {
    // Initialize start locations, expansion locations, chokepoints, etc.
    const ObservationInterface *obs = Observation();
    start_location = obs->GetStartLocation();
    enemy_start_locations = obs->GetGameInfo().enemy_start_locations;
    if (!enemy_start_locations.empty()) {
        enemy_start_location = enemy_start_locations[0];
    }
    expansion_locations = search::CalculateExpansionLocations(obs, Query());
    retreat_location = {start_location.x + 5.0f, start_location.y};

    // Get map dimensions for corner coordinates
    const GameInfo &game_info = obs->GetGameInfo();
    playable_min = game_info.playable_min;
    playable_max = game_info.playable_max;

    // Initialize the four corners of the map
    map_corners = {
        Point2D(playable_min.x, playable_min.y), // Bottom-left
        Point2D(playable_max.x, playable_min.y), // Bottom-right
        Point2D(playable_min.x, playable_max.y), // Top-left
        Point2D(playable_max.x, playable_max.y)  // Top-right
    };

    // find ramps
    base_location = GetBaseLocation();
    find_right_ramp(start_location);
    find_ramps_build_map(false);
    update_build_map(true);
    auto mineral_points = get_close_mineral_points(start_location);
    main_mineral_convexHull = convexHull(mineral_points);

    // Initialize base
    Units command_centers = obs->GetUnits(
        Unit::Alliance::Self, IsUnit(UNIT_TYPEID::TERRAN_COMMANDCENTER));
    if (!command_centers.empty()) {
        bases.emplace_back(command_centers.front());
    }

    // Initialize build tasks based on build order
    for (auto ability : build_order) {
        BuildTask task;
        task.ability_id = ability;
        task.is_complete = false;
        build_tasks.emplace_back(task);
    }

    // Initialize other game state variables
    is_under_attack = false;
    is_attacking = false;
    need_expansion = false;

    // Find the nearest corner to the starting location
    float min_corner_distance = std::numeric_limits<float>::max();
    ;

    for (const auto &corner : map_corners) {
        float corner_distance = DistanceSquared2D(start_location, corner);
        if (corner_distance < min_corner_distance) {
            min_corner_distance = corner_distance;
            nearest_corner_ally = corner;
        }
    }

    // Get map dimensions
    unsigned int width = game_info.width;
    unsigned int height = game_info.height;

    // Generate grid points across the entire map for scouting
    scout_points.clear(); // Clear previous scout points if any
    const unsigned int step_size =
        15; // Step size for grid points (adjust for thoroughness)
    for (unsigned int x = 0; x < width; x += step_size) {
        for (unsigned int y = 0; y < height; y += step_size) {
            Point2D grid_point(static_cast<float>(x), static_cast<float>(y));
            if (obs->IsPathable(grid_point)) {
                scout_points.emplace_back(grid_point);
            }
        }
    }

    // Start scouting from the beginning
    clean_up_index = 0;
}

void BasicSc2Bot::OnGameStart() {
    //
    /*Debug()->DebugIgnoreResourceCost();
    Debug()->DebugFastBuild();
    Debug()->SendDebug();*/
}

void BasicSc2Bot::OnGameEnd() {
    // Get the game info
    const ObservationInterface *observation = Observation();
    GameInfo gameInfo = observation->GetGameInfo();
    auto players = std::map<uint32_t, PlayerInfo *>();
    for (auto &playerInfo : gameInfo.player_info) {
        players[playerInfo.player_id] = &playerInfo;
    }

    // Map the player types to strings
    auto playerTypes = std::map<PlayerType, std::string>();
    playerTypes[PlayerType::Participant] = "UED";
    playerTypes[PlayerType::Computer] = "Prey";
    playerTypes[Observer] = "Observer";

    // Map the game results to strings
    auto gameResults = std::map<GameResult, std::string>();
    gameResults[Win] = " Wins";
    gameResults[Loss] = " Loses";
    gameResults[Tie] = " Tied";
    gameResults[Undecided] = " Undecided";

    // Print the results of the game
    for (auto &playerResult : observation->GetResults()) {
        std::cout
            << playerTypes[((*(players[playerResult.player_id])).player_type)]
            << gameResults[playerResult.result] << std::endl;
    }
}

// Main game loop
void BasicSc2Bot::OnStep() {
    ++step_counter;
    // Wait for 10 frames
    if (step_counter < 10) {
        return;
    }
    // On 10th frame, redo our init
    if (step_counter == 10) {
        on_start();
    }
    current_gameloop = Observation()->GetGameLoop();
    /*if (current_gameloop % 22 == 0)
        BasicSc2Bot::Debugging();*/

    if (step_counter > 10) {
        BasicSc2Bot::depot_control();
        BasicSc2Bot::ManageEconomy();
        BasicSc2Bot::ExecuteBuildOrder();
        BasicSc2Bot::ManageProduction();
        BasicSc2Bot::ControlUnits();
        BasicSc2Bot::Defense();
        BasicSc2Bot::Offense();
    }
}

void BasicSc2Bot::OnUnitIdle(const Unit *unit) {
    // Get and set the rally points for the different unit types
    switch (unit->unit_type.ToType()) {
    case UNIT_TYPEID::TERRAN_BARRACKS:
        if (rally_barrack == Point2D(0.0f, 0.0f)) {
            rally_barrack =
                towards(mainBase_barrack_point, start_location, 2.0f);
            SetRallyPoint(unit, rally_barrack);
        }
        break;
    case UNIT_TYPEID::TERRAN_FACTORY:
        if (rally_factory == Point2D(0.0f, 0.0f)) {
            Point2D p1 =
                towards(mainBase_depot_points[0], start_location, 6.0f);
            Point2D p2 =
                towards(mainBase_depot_points[1], start_location, 6.0f);
            rally_factory =
                Distance2D(p1, start_location) < Distance2D(p2, start_location)
                    ? p1
                    : p2;
            SetRallyPoint(unit, rally_factory);
        }
        break;
    case UNIT_TYPEID::TERRAN_STARPORT:
        if (rally_starport == Point2D(0.0f, 0.0f)) {
            rally_starport =
                towards(mainBase_barrack_point, start_location, 6.0f);
            SetRallyPoint(unit, rally_starport);
        }
        break;
    case UNIT_TYPEID::TERRAN_MARINE:
        if (Distance2D(unit->pos, rally_barrack) >= 3.0f &&
            Distance2D(unit->pos, enemy_start_location) >= 30.0f &&
            !unit_attacking[unit]) {
            Actions()->UnitCommand(unit, ABILITY_ID::MOVE_MOVE, rally_barrack);
        }
        break;
    case UNIT_TYPEID::TERRAN_SIEGETANK:
        if (Distance2D(unit->pos, rally_factory) >= 3.0f &&
            Distance2D(unit->pos, enemy_start_location) >= 30.0f &&
            !unit_attacking[unit]) {
            Actions()->UnitCommand(unit, ABILITY_ID::MOVE_MOVE, rally_factory);
        }
        break;
    case UNIT_TYPEID::TERRAN_BATTLECRUISER:
        if (current_gameloop % 23 == 0 &&
            !(Distance2D(unit->pos, start_location) < 25.0f)) {
            Retreat(unit);
        }
        break;
    case UNIT_TYPEID::TERRAN_SCV:
        HarvestIdleWorkers(unit);
        break;
    default:
        break;
    }
}

void BasicSc2Bot::OnUnitCreated(const Unit *unit) {

    if (!unit) {
        return;
    }

    // SCV created
    if (unit->unit_type == UNIT_TYPEID::TERRAN_SCV) {
        ++num_scvs;
        if (scvs_repairing.size() < 6) {
            scvs_repairing.insert(unit->tag);
        }
    }

    std::vector<uint32_t> minsec = GetRealTime();

    // Battlecruiser created
    if (unit->unit_type == UNIT_TYPEID::TERRAN_BATTLECRUISER) {
        if (!first_battlecruiser_trained) {
            first_battlecruiser_trained = true;
        }
        num_battlecruisers++;
        std::cout << "Battlecruiser created at " << minsec[0] << ":"
                  << minsec[1] << std::endl;
        std::cout << "Marines: " << num_marines << " Tanks: " << num_siege_tanks
                  << " Battlecruisers: " << num_battlecruisers << std::endl;
    }
    // Marine created
    if (unit->unit_type == UNIT_TYPEID::TERRAN_MARINE) {
        num_marines++;
    }
    // Siege Tank created
    if (unit->unit_type == UNIT_TYPEID::TERRAN_SIEGETANK) {
        num_siege_tanks++;
        std::cout << "Tank created at " << minsec[0] << ":" << minsec[1]
                  << std::endl;
        std::cout << "Marines: " << num_marines << " Tanks: " << num_siege_tanks
                  << " Battlecruisers: " << num_battlecruisers << std::endl;
    }
}

void BasicSc2Bot::OnBuildingConstructionComplete(const Unit *unit) {
    const ObservationInterface *obs = Observation();
    update_build_map(true);
    std::vector<uint32_t> minsec = GetRealTime();
    auto unit_type = unit->unit_type.ToType();

    if (unit_type == UNIT_TYPEID::TERRAN_BARRACKS) {
        ++num_barracks;
    } else if (unit_type == UNIT_TYPEID::TERRAN_FACTORY) {
        ++num_factories;
    } else if (unit_type == UNIT_TYPEID::TERRAN_STARPORT) {
        ++num_starports;
    } else if (unit_type == UNIT_TYPEID::TERRAN_FUSIONCORE) {
        ++num_fusioncores;
    } else if (unit_type == UNIT_TYPEID::TERRAN_COMMANDCENTER) {
        std::cout << "COMMANDCENTER created at " << minsec[0] << ":"
                  << minsec[1] << std::endl;
        std::cout << "Marines: " << num_marines << " Tanks: " << num_siege_tanks
                  << " Battlecruisers: " << num_battlecruisers << std::endl;
        bases.emplace_back(unit);
    }

    if (unit->unit_type == UNIT_TYPEID::TERRAN_REFINERY) {
        const ObservationInterface *obs = Observation();

        // Get all SCVs that are free
        Units scvs = obs->GetUnits(Unit::Alliance::Self, [](const Unit &unit) {
            return unit.unit_type == UNIT_TYPEID::TERRAN_SCV &&
                   !unit.orders.empty() &&
                   unit.orders.front().ability_id == ABILITY_ID::HARVEST_GATHER;
        });

        // Assign them to gas
        int scv_count = 0;
        for (const auto &scv : scvs) {
            if (scvs_repairing.find(scv->tag) != scvs_repairing.end()) {
                continue;
            }
            Actions()->UnitCommand(scv, ABILITY_ID::HARVEST_GATHER, unit);
            ++scv_count;
            // make sure it has 3 including the scv that is building the
            // refinery
            if (scv_count == 2)
                break;
        }
    }

    if (Point2D(unit->pos) == mainBase_barrack_point) {
        ramp_middle[0] = const_cast<sc2::Unit *>(unit);
    }

    if (phase == 0) {

        // Barracks completed
        if (unit->unit_type == UNIT_TYPEID::TERRAN_BARRACKS) {
            Units barracks = obs->GetUnits(
                Unit::Alliance::Self, IsUnit(UNIT_TYPEID::TERRAN_BARRACKS));
            /*const Unit* b = !barracks.empty() ? barracks.front() : nullptr;
            ramp_middle[0] = const_cast<sc2::Unit*>(unit);*/
            if (CanBuild(50, 25)) {
                // it is always true because we make sure that we have enough
                // resources to build the techlab
                Actions()->UnitCommand(unit,
                                       ABILITY_ID::BUILD_TECHLAB_BARRACKS);
            }

        } else if (unit->unit_type == UNIT_TYPEID::TERRAN_BARRACKSTECHLAB) {
            Units techlab =
                obs->GetUnits(Unit::Alliance::Self,
                              IsUnit(UNIT_TYPEID::TERRAN_BARRACKSTECHLAB));
            const Unit *t = !techlab.empty() ? techlab.front() : nullptr;
            ramp_middle[1] = const_cast<sc2::Unit *>(t);
            ++phase;
        }
    } else if (phase == 1) {

        if (unit->unit_type == UNIT_TYPEID::TERRAN_FACTORY) {
            Units factories = obs->GetUnits(
                Unit::Alliance::Self, IsUnit(UNIT_TYPEID::TERRAN_FACTORY));
            Units barracks = obs->GetUnits(
                Unit::Alliance::Self, IsUnit(UNIT_TYPEID::TERRAN_BARRACKS));

            if (barracks.empty())
                return;

            if (!EnemyNearby(barracks.front()->pos, true, 20) &&
                !barracks.empty() && barracks.front()->orders.empty()) {
                const Unit *b = barracks.front();
                const Unit *f = factories.front();
                ramp_middle[0] = const_cast<sc2::Unit *>(f);
                Swap(b, f, true);
            }
            ++phase;
        }
    } else if (phase == 2) {
        if (unit->unit_type == UNIT_TYPEID::TERRAN_STARPORT) {
            ++phase;
            // now it is phase 3
        }
    }
}

void BasicSc2Bot::OnUpgradeCompleted(UpgradeID upgrade_id) {
    // Add completed upgrades to the set
    completed_upgrades.insert(upgrade_id);
}

void BasicSc2Bot::OnUnitDestroyed(const Unit *unit) {
    // Update unit counts and remove destroyed units from the game state
    if (IsFriendlyStructure(*unit)) {
        update_build_map(false, unit);
        // TODO: make sure if ramp_depots[1] becomes ramp_depots[0] when
        // ramp_depots[0] is destroyed,
        if (unit == ramp_depots[0]) {
            ramp_depots[0] = nullptr;
        } else if (unit == ramp_depots[1]) {
            ramp_depots[1] = nullptr;
        }

        if (Point2D(unit->pos) == mainBase_barrack_point) {
            ramp_mid_destroyed = unit;
            ramp_middle[0] = nullptr;
        } else if (unit == ramp_middle[1]) {
            ramp_middle[1] = nullptr;
        }

        if (unit) {
            if (unit->unit_type == UNIT_TYPEID::TERRAN_BARRACKS) {
                if (num_barracks) {
                    --num_barracks;
                } else {
                    rally_barrack = Point2D(0.0f, 0.0f);
                }
            } else if (unit->unit_type == UNIT_TYPEID::TERRAN_FACTORY) {

                if (num_factories) {
                    --num_factories;
                } else {
                    rally_factory = Point2D(0.0f, 0.0f);
                }
            } else if (unit->unit_type == UNIT_TYPEID::TERRAN_STARPORT) {

                if (num_factories) {
                    --num_starports;
                } else {
                    rally_starport = Point2D(0.0f, 0.0f);
                }
            } else if (unit->unit_type == UNIT_TYPEID::TERRAN_FUSIONCORE) {
                if (num_fusioncores) {
                    --num_fusioncores;
                }
            } else if (unit->unit_type == UNIT_TYPEID::TERRAN_COMMANDCENTER ||
                       unit->unit_type == UNIT_TYPEID::TERRAN_ORBITALCOMMAND) {
                bases.erase(std::remove(bases.begin(), bases.end(), unit),
                            bases.end());
            }
        }
    }

    // Scouting scv died
    if (is_scouting && scv_scout && unit == scv_scout) {
        // find the nearest possible enemy location to its last known position
        float min_distance = std::numeric_limits<float>::max();
        sc2::Point2D nearest_location;

        for (const auto &location : enemy_start_locations) {
            float distance = sc2::Distance2D(scout_location, location);
            if (distance < min_distance) {
                min_distance = distance;
                nearest_location = location;
            }
        }

        // Set the nearest location as the confirmed enemy base location
        enemy_start_location = nearest_location;

        // Find the nearest corner to the enemy base
        float min_corner_distance = std::numeric_limits<float>::max();

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

        // Reset scouting
        scv_scout = nullptr;
        scout_complete = true;
        is_scouting = false;
    }

    // SCV died
    if (unit->unit_type == UNIT_TYPEID::TERRAN_SCV) {
        --num_scvs;
        scvs_repairing.erase(unit->tag);
        // scvs_gas.erase(unit->tag);
    } else if (unit->unit_type == UNIT_TYPEID::TERRAN_BATTLECRUISER) {
        battlecruiser_retreating.erase(unit);
        --num_battlecruisers;
    } else if (unit->unit_type == UNIT_TYPEID::TERRAN_MARINE) {
        unit_attacking.erase(unit);
        --num_marines;
    } else if (unit->unit_type == UNIT_TYPEID::TERRAN_SIEGETANK) {
        unit_attacking.erase(unit);
        --num_siege_tanks;
    }
}

void BasicSc2Bot::OnUnitEnterVision(const Unit *unit) { return; }

// Testing commands
// ./BasicSc2Bot.exe -c -a zerg -d Hard -m CactusValleyLE.SC2Map
// ./BasicSc2Bot.exe -c -a terran -d Hard -m CactusValleyLE.SC2Map
// ./BasicSc2Bot.exe -c -a protoss -d Hard -m CactusValleyLE.SC2Map
// ./BasicSc2Bot.exe -c -a zerg -d Hard -m BelShirVestigeLE.SC2Map
// ./BasicSc2Bot.exe -c -a terran -d Hard -m BelShirVestigeLE.SC2Map
// ./BasicSc2Bot.exe -c -a protoss -d Hard -m BelShirVestigeLE.SC2Map
// ./BasicSc2Bot.exe -c -a zerg -d Hard -m ProximaStationLE.SC2Map
// ./BasicSc2Bot.exe -c -a terran -d Hard -m ProximaStationLE.SC2Map
// ./BasicSc2Bot.exe -c -a protoss -d Hard -m ProximaStationLE.SC2Map