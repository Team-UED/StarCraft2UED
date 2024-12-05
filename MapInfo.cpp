#include "BasicSc2Bot.h"

// return (A.x - O.x) * (B.y - O.y) - (A.y - O.y) * (B.x - O.x)
float BasicSc2Bot::cross_product(const Point2D& O, const Point2D& A,
	const Point2D& B) const {
	return (A.x - O.x) * (B.y - O.y) - (A.y - O.y) * (B.x - O.x);
}

// find the set of Point2D points that are the convex hull of the given set of Point2D points
// It helps to tell the boundary of a set of points
std::vector<Point2D>
BasicSc2Bot::convexHull(std::vector<Point2D>& points) const {
	// Sort points
	std::sort(points.begin(), points.end(),
		[this](const Point2D& a, const Point2D& b) { return a.x > b.x; });

	// Build the hull
	std::vector<Point2D> hull;

	// Lower hull
	for (const auto& p : points) {
		while (hull.size() >= 2 &&
			cross_product(hull[hull.size() - 2], hull.back(), p) <= 0) {
			hull.pop_back();
		}
		hull.emplace_back(p);
	}

	// Upper hull
	size_t lowerHullSize = hull.size();
	for (auto it = points.rbegin(); it != points.rend(); ++it) {
		while (hull.size() > lowerHullSize &&
			cross_product(hull[hull.size() - 2], hull.back(), *it) <= 0) {
			hull.pop_back();
		}
		hull.emplace_back(*it);
	}

	// Remove the last point because it's the same as the first
	hull.pop_back();

	switch (base_location) {
	case BaseLocation::lefttop:
		std::sort(hull.begin(), hull.end(),
			[this](const Point2D& a, const Point2D& b) {
				return a.y < b.y || (a.x == b.x && a.y < b.y);
			});
		break;
	case BaseLocation::righttop:
		std::sort(hull.begin(), hull.end(),
			[this](const Point2D& a, const Point2D& b) {
				return a.y > b.y || (a.y == b.y && a.x < b.x);
			});
		break;
	case BaseLocation::leftbottom:
		std::sort(hull.begin(), hull.end(),
			[this](const Point2D& a, const Point2D& b) {
				return a.y > b.y || (a.x == b.x && a.y > b.y);
			});
		break;
	case BaseLocation::rightbottom:
		std::sort(hull.begin(), hull.end(),
			[this](const Point2D& a, const Point2D& b) {
				return a.y < b.y || (a.y == b.y && a.x < b.x);
			});
		break;
	}
	return hull;
}

// Return mean of vector of points
Point2D BasicSc2Bot::Point2D_mean(const std::vector<Point2D>& points) const {
	Point2D mean;

	for (const auto& p : points) {
		mean += p;
	}
	mean /= points.size();

	return mean;
}

// Return mean of map of points
Point2D BasicSc2Bot::Point2D_mean(
	const std::map<Point2D, bool, Point2DComparator>& map_points) const {
	Point2D mean;
	for (const auto& p : map_points) {
		mean += p.first;
	}
	mean /= map_points.size();

	return mean;
}

// Find the intersection points of two circles
std::vector<Point2D> BasicSc2Bot::circle_intersection(const Point2D& p1,
	const Point2D& p2,
	float r) const {
	assert(p1 != p2);
	float distanceBetweenPoints = Distance2D(p1, p2);
	assert(r > distanceBetweenPoints / 2);

	// remaining distance from center towards the intersection, using pythagoras
	float remainingDistanceFromCenter = std::sqrt(
		(r * r) - ((distanceBetweenPoints / 2) * (distanceBetweenPoints / 2)));

	// center of both points
	Point2D offsetToCenter((p2.x - p1.x) / 2, (p2.y - p1.y) / 2);
	Point2D center = p1 + offsetToCenter;

	// stretch offset vector in the ratio of remaining distance from center to
	// intersection
	float vectorStretchFactor =
		remainingDistanceFromCenter / (distanceBetweenPoints / 2);
	Point2D offsetToCenterStretched(offsetToCenter.x * vectorStretchFactor,
		offsetToCenter.y * vectorStretchFactor);

	// rotate the vector by 90 degrees
	Point2D vectorRotated1(offsetToCenterStretched.y,
		-offsetToCenterStretched.x);
	Point2D vectorRotated2(-offsetToCenterStretched.y,
		offsetToCenterStretched.x);
	Point2D intersect1 = center + vectorRotated1;
	Point2D intersect2 = center + vectorRotated2;

	return { intersect1, intersect2 };
}

// Return the point that is distance away from p1 towards p2
Point2D BasicSc2Bot::towards(const Point2D& p1, const Point2D& p2,
	float distance) const {
	if (p1 == p2) {
		return p1;
	}

	Point2D p;
	float d = Distance2D(p1, p2);
	p.x = p1.x + (p2.x - p1.x) / d * distance;
	p.y = p1.y + (p2.y - p1.y) / d * distance;

	return p;
}

// update buildable map (if it is built or not?) 
// destoryed_building is optional for the case of building destroyed
void BasicSc2Bot::update_build_map(const bool built,
	const Unit* destroyed_building) {

	const Point2D offset(0.5, 0.5);
	Point2D building_point;

	// 0: main_base_build_map
	auto& base_build_map = build_map[0];

	// building footprint radius
	const auto b_bool = built ? false : true;

	const ObservationInterface* obs = Observation();
	Units buildings =
		destroyed_building
		? Units{ destroyed_building }
		: obs->GetUnits(Unit::Alliance::Self, [this](const Unit& b) {
		return IsFriendlyStructure(b);
			});

	for (auto& b : buildings) {
		auto b_type = b->unit_type.ToType();
		building_point = Point2D(b->pos.x, b->pos.y);

		// 2x2
		if (b_type == UNIT_TYPEID::TERRAN_SUPPLYDEPOT ||
			b_type == UNIT_TYPEID::TERRAN_SUPPLYDEPOTLOWERED ||
			b_type == UNIT_TYPEID::TERRAN_MISSILETURRET) {
			// b_radius = 1.0f;

			// I need to check 0,0, 0,-1, -1,0, -1,-1
			for (int dx = -1; dx <= 0; ++dx) {
				for (int dy = -1; dy <= 0; ++dy) {
					base_build_map[building_point + Point2D(dx, dy)] = b_bool;
				}
			}
		}
		// 3x3 + 2x2 (3.5) add_on ->
		else if (b_type == UNIT_TYPEID::TERRAN_BARRACKS ||
			b_type == UNIT_TYPEID::TERRAN_FACTORY ||
			b_type == UNIT_TYPEID::TERRAN_STARPORT) {

			// b_radius = 1.5f;

			Point2D center_point = building_point - offset;
			for (int dx = -1; dx <= 1; ++dx) {
				for (int dy = -1; dy <= 1; ++dy) {
					base_build_map[center_point + Point2D(dx, dy)] = b_bool;
				}
			}
		}
		else if (b_type == UNIT_TYPEID::TERRAN_BARRACKSTECHLAB ||
			b_type == UNIT_TYPEID::TERRAN_BARRACKSREACTOR ||
			b_type == UNIT_TYPEID::TERRAN_FACTORYTECHLAB ||
			b_type == UNIT_TYPEID::TERRAN_FACTORYREACTOR ||
			b_type == UNIT_TYPEID::TERRAN_STARPORTTECHLAB ||
			b_type == UNIT_TYPEID::TERRAN_STARPORTREACTOR) {

			// the actual building size is 2x2
			// b_radius = 3.5f;

			// check 6x6
			for (int dx = -3; dx <= 3; ++dx) {
				for (int dy = -3; dy <= 3; ++dy) {
					Point2D grid_point = building_point + Point2D(dx, dy);
					base_build_map[grid_point] = b_bool;
				}
			}
		}

		// 3x3  +2 = 5x3
		else if (b_type == UNIT_TYPEID::TERRAN_ENGINEERINGBAY ||
			b_type == UNIT_TYPEID::TERRAN_ARMORY ||
			b_type == UNIT_TYPEID::TERRAN_FUSIONCORE ||
			b_type == UNIT_TYPEID::TERRAN_BUNKER) {

			// b_radius = 1.5f;

			Point2D center_point = building_point - offset;
			for (int dx = -1; dx <= 1; ++dx) {
				for (int dy = -1; dy <= 1; ++dy) {
					base_build_map[center_point + Point2D(dx, dy)] = b_bool;
				}
			}
		}
		// 5x5
		else if (b_type == UNIT_TYPEID::TERRAN_COMMANDCENTER ||
			b_type == UNIT_TYPEID::TERRAN_ORBITALCOMMAND ||
			b_type == UNIT_TYPEID::TERRAN_PLANETARYFORTRESS) {

			// b_radius = 2.5f;

			Point2D center_point = building_point - offset;
			for (int dx = -2; dx <= 2; ++dx) {
				for (int dy = -2; dy <= 2; ++dy) {
					Point2D grid_point = center_point + Point2D(dx, dy);
					base_build_map[grid_point] = b_bool;
				}
			}
		}
	}
}

// With the given point, check if there is a building in the area
// if addon is true, check 3x3 + 2x2
bool BasicSc2Bot::area33_check(const Point2D& b, const bool addon) {
	auto& base_build_map = build_map[0];
	Point2D offset(0.5, 0.5);
	Point2D b_offset = b - offset;
	Point2D grid_point;
	float distance_to_base = Distance2D(b, start_location);

	// in case of the building 3x3, should not be too close or too far
	if (!addon && (distance_to_base < 10.0f || distance_to_base > 15.0f)) {
		return false;
	}

	for (int dx = -1; dx <= 1; ++dx) {
		for (int dy = -1; dy <= 1; ++dy) {
			grid_point = b_offset + Point2D(dx, dy);
			if (base_build_map.find(grid_point) != base_build_map.end() &&
				base_build_map[grid_point]) {
				continue;
			}
			return false;
		}
	}
	// if addon is true, check 2x2 more
	if (addon) {
		Point2D addon_point = b_offset + Point2D(3, 0);
		for (int dx = -1; dx <= 0; ++dx) {
			for (int dy = -1; dy <= 0; ++dy) {
				grid_point = addon_point + Point2D(dx, dy);
				if (base_build_map.find(grid_point) != base_build_map.end() &&
					base_build_map[grid_point]) {
					continue;
				}
				return false;
			}
		}
	}
	return true;
}

// build 3x3 + addon
bool BasicSc2Bot::build33_after_check(
	const Unit* builder, const AbilityID& build_ability,
	const BasicSc2Bot::BaseLocation whereismybase, const bool addon) {
	// limit check for 3x3 buildings
	Point2D left_limit = main_mineral_convexHull.front();
	Point2D right_limit = main_mineral_convexHull.back();
	float distance_to_left = Distance2D(left_limit, start_location);
	float distance_to_right = Distance2D(right_limit, start_location);

	// different setting depending on the base location
	switch (whereismybase) {
	case BaseLocation::lefttop:
		// I need to check to the top and to the left and to the bottom
		for (float j = mainBase_barrack_point.y + 5.0f;
			j < build_map_minmax[1].y; j += 5.0f) {
			for (float i = mainBase_barrack_point.x - 6.0f;
				i > build_map_minmax[0].x; i -= 6.0f) {
				// Travel to the bottom
				for (float k = j; k > build_map_minmax[0].y; k -= 5.0f) {
					if (addon && InDepotArea(Point2D(i, k), whereismybase)) {
						continue;
					}
					if (area33_check(Point2D(i, k), addon)) {
						if (EnemyNearby(Point2D(i, k))) {
							return false;
						}
						Actions()->UnitCommand(builder, build_ability,
							Point2D(i, k));
						return true;
					}
				}
			}
		}
		break;
	case BaseLocation::righttop:
		for (float j = mainBase_barrack_point.y + 5.0f;
			j < build_map_minmax[1].y; j += 5.0f) {
			for (float i = mainBase_barrack_point.x; i > build_map_minmax[0].x;
				i -= 6.0f) {
				// Travel to the bottom
				for (float k = j; k > build_map_minmax[0].y; k -= 5.0f) {
					if (addon && InDepotArea(Point2D(i, k), whereismybase)) {
						continue;
					}
					if (area33_check(Point2D(i, k), addon)) {
						if (EnemyNearby(Point2D(i, k))) {
							return false;
						}
						Actions()->UnitCommand(builder, build_ability,
							Point2D(i, k));
						return true;
					}
				}
			}
		}
		break;
	case BaseLocation::leftbottom:
		for (float j = mainBase_barrack_point.y - 5.0f;
			j < build_map_minmax[1].y; j += 5.0f) {
			for (float i = mainBase_barrack_point.x; i < build_map_minmax[1].x;
				i += 6.0f) {
				// Travel to the bottom
				for (float k = j; k > build_map_minmax[0].y; k -= 5.0f) {
					if (addon && InDepotArea(Point2D(i, k), whereismybase)) {
						continue;
					}
					if (area33_check(Point2D(i, k), addon)) {
						if (EnemyNearby(Point2D(i, k))) {
							return false;
						}
						Actions()->UnitCommand(builder, build_ability,
							Point2D(i, k));
						return true;
					}
				}
			}
		}
		break;
	case BaseLocation::rightbottom:
		for (float j = mainBase_barrack_point.y - 5.0f;
			j < build_map_minmax[1].y; j += 5.0f) {
			for (float i = mainBase_barrack_point.x; i < build_map_minmax[1].x;
				i += 6.0f) {
				// Travel to the bottom
				for (float k = j; k > build_map_minmax[0].y; k -= 5.0f) {
					if (addon && InDepotArea(Point2D(i, k), whereismybase)) {
						continue;
					}
					if (area33_check(Point2D(i, k), addon)) {
						if (EnemyNearby(Point2D(i, k))) {
							return false;
						}
						Actions()->UnitCommand(builder, build_ability,
							Point2D(i, k));
						return true;
					}
				}
			}
		}
		break;
	}
	return false;
}

// check the area for the depot
bool BasicSc2Bot::depot_area_check(const Unit* builder,
	const AbilityID& build_ability,
	BasicSc2Bot::BaseLocation whereismybase) {
	// trying to build the depot beyond the mineral line
	Point2D left_limit = main_mineral_convexHull.front();
	Point2D right_limit = main_mineral_convexHull.back();
	float distance_to_left = Distance2D(left_limit, start_location);
	float distance_to_right = Distance2D(right_limit, start_location);
	float distance_to_query;

	switch (whereismybase) {
	case BaseLocation::lefttop:
		for (int j = left_limit.y; j < build_map_minmax[1].y; ++j) {
			for (int i = build_map_minmax[0].x; i < right_limit.x; ++i) {
				distance_to_query = Distance2D(Point2D(i, j), start_location);

				if (distance_to_query <= distance_to_right ||
					distance_to_query <= distance_to_left ||
					build_map[0].find(Point2D(i, j)) == build_map[0].end()) {
					continue;
				}
				// DrawBoxAtLocation(debug, Point3D(i + 0.5f, j + 0.5f,
				// height_at(Point2DI(i, j)) + 0.1f), 1.0f, sc2::Colors::Red);
				if (Query()->Placement(build_ability, Point2D(i, j))) {
					Actions()->UnitCommand(builder, build_ability,
						Point2D(i, j), false);
					return true;
				}
			}
		}
		break;

	case BaseLocation::righttop:
		for (int j = right_limit.y; j < build_map_minmax[1].y; ++j) {
			for (int i = left_limit.x; i < build_map_minmax[1].x; ++i) {
				distance_to_query = Distance2D(Point2D(i, j), start_location);

				if (distance_to_query <= distance_to_right ||
					distance_to_query <= distance_to_left ||
					build_map[0].find(Point2D(i, j)) == build_map[0].end()) {
					continue;
				}
				// DrawBoxAtLocation(debug, Point3D(i + 0.5f, j + 0.5f,
				// height_at(Point2DI(i, j)) + 0.1f), 1.0f, sc2::Colors::Red);
				if (Query()->Placement(build_ability, Point2D(i, j))) {
					Actions()->UnitCommand(builder, build_ability,
						Point2D(i, j), false);
					return true;
				}
			}
		}
		break;

	case BaseLocation::leftbottom:
		for (int j = build_map_minmax[0].y; j < left_limit.y; ++j) {
			for (int i = build_map_minmax[0].x; i < right_limit.x; ++i) {
				distance_to_query = Distance2D(Point2D(i, j), start_location);

				if (distance_to_query <= distance_to_right ||
					distance_to_query <= distance_to_left ||
					build_map[0].find(Point2D(i, j)) == build_map[0].end()) {
					continue;
				}
				// DrawBoxAtLocation(debug, Point3D(i + 0.5f, j + 0.5f,
				// height_at(Point2DI(i, j)) + 0.1f), 1.0f, sc2::Colors::Red);
				if (Query()->Placement(build_ability, Point2D(i, j))) {
					Actions()->UnitCommand(builder, build_ability,
						Point2D(i, j), false);
					return true;
				}
			}
		}
		break;
	case BaseLocation::rightbottom:
		for (int j = build_map_minmax[0].y; j < right_limit.y; ++j) {
			for (int i = left_limit.x; i < build_map_minmax[1].x; ++i) {
				distance_to_query = Distance2D(Point2D(i, j), start_location);

				if (distance_to_query <= distance_to_right ||
					distance_to_query <= distance_to_left ||
					build_map[0].find(Point2D(i, j)) == build_map[0].end()) {
					continue;
				}
				// DrawBoxAtLocation(debug, Point3D(i + 0.5f, j + 0.5f,
				// height_at(Point2DI(i, j)) + 0.1f), 1.0f, sc2::Colors::Red);
				if (Query()->Placement(build_ability, Point2D(i, j))) {
					Actions()->UnitCommand(builder, build_ability,
						Point2D(i, j), false);
					return true;
				}
			}
		}
		break;
	}

	return false;
}

// check if the given point is in the depot area
bool BasicSc2Bot::InDepotArea(const Point2D& p,
	const BasicSc2Bot::BaseLocation whereismybase) {
	Point2D left_limit = main_mineral_convexHull.front();
	Point2D right_limit = main_mineral_convexHull.back();

	switch (whereismybase) {
	case BaseLocation::lefttop:
		return p.x < right_limit.x && p.x >= build_map_minmax[0].x &&
			p.y < build_map_minmax[1].y && p.y >= left_limit.y;
		break;
	case BaseLocation::righttop:
		return p.x < build_map_minmax[1].x && p.x >= left_limit.x &&
			p.y < build_map_minmax[1].y && p.y >= right_limit.y;

		break;
	case BaseLocation::leftbottom:
		return p.x < right_limit.x && p.x >= build_map_minmax[0].x &&
			p.y < left_limit.y && p.y >= build_map_minmax[0].y;
		break;
	case BaseLocation::rightbottom:
		return p.x < build_map_minmax[1].x && p.x >= left_limit.x &&
			p.y < right_limit.y && p.y >= build_map_minmax[0].y;
		break;
	}
	return false;
}

// is my base on the left side of the map?
bool BasicSc2Bot::IsBaseOnLeft() const {
	return start_location.x < (playable_max.x / 2);
}

// is my base on the top side of the map?
bool BasicSc2Bot::IsBaseOnTop() const {
	return start_location.y > (playable_max.y / 2);
}

// return the base location
BasicSc2Bot::BaseLocation BasicSc2Bot::GetBaseLocation() const {
	bool is_left = IsBaseOnLeft();
	bool is_top = IsBaseOnTop();

	if (is_left && is_top) {
		return BaseLocation::lefttop;
	}
	else if (!is_left && is_top) {
		return BaseLocation::righttop;
	}
	else if (is_left && !is_top) {
		return BaseLocation::leftbottom;
	}
	else {
		return BaseLocation::rightbottom;
	}
}

// with the given point, find the closest mineral points
std::vector<Point2D>
BasicSc2Bot::get_close_mineral_points(Point2D& unit_pos) const {
	const ObservationInterface* obs = Observation();
	Units mineral_patches =
		obs->GetUnits(Unit::Alliance::Neutral, [unit_pos](const Unit& unit) {
		return IsMineralPatch()(unit) &&
			Distance2D(unit.pos, unit_pos) < 10.0f;
			});
	std::vector<Point2D> mineral_points;

	for (const auto& m : mineral_patches) {
		mineral_points.emplace_back(m->pos);
	}
	return mineral_points;
}

// calculate the proper location for the turret
// trying to build them towards the mineral patches
std::vector<Point2D>
BasicSc2Bot::find_terret_location_btw(std::vector<Point2D>& mineral_patches,
	Point2D& townhall) {
	std::vector<Point2D> townhall_verts;
	townhall_verts.reserve(4);

	std::vector<Point2D> turret_locations;
	turret_locations.reserve(4);

	// townhall's footprint radius is 2.5
	float offset = 2.5f;
	for (int dx = -1; dx <= 1; dx += 2) {
		for (int dy = -1; dy <= 1; dy += 2) {
			townhall_verts.emplace_back(townhall +
				Point2D(dx * offset, dy * offset));
		}
	}

	// find the closest vertex to mineral patches
	Point2D avg_point = Point2D_mean(mineral_patches);
	std::sort(townhall_verts.begin(), townhall_verts.end(),
		[&avg_point](const Point2D& a, const Point2D& b) {
			return Distance2D(a, avg_point) < Distance2D(b, avg_point);
		});

	// check 4 directions
	for (int dy = -1; dy <= 1; dy += 2) {
		for (int dx = -1; dx <= 1; dx += 2) {
			Point2D p = townhall_verts[0] + Point2D(dx, dy);
			turret_locations.emplace_back(p);
		}
	}
	std::sort(turret_locations.begin(), turret_locations.end(),
		[&townhall](const Point2D& a, const Point2D& b) {
			return Distance2D(a, townhall) < Distance2D(b, townhall);
		});
	// farthest to base has to be removed
	// closest to base has to be removed
	turret_locations.pop_back();
	turret_locations.erase(turret_locations.begin());

	return turret_locations;
}

// return highet at the given point
int BasicSc2Bot::height_at(const Point2DI& p) const {
	HeightMap h_map(Observation()->GetGameInfo());
	return static_cast<int>(h_map.TerrainHeight(p));
}

// return highet at the given point
float BasicSc2Bot::height_at_float(const Point2DI& p) const {
	HeightMap h_map(Observation()->GetGameInfo());
	return h_map.TerrainHeight(p);
}

// find groups of points
// either it is for ramps or buildable map
// if minimum_points_per_group is -1, it is for buildable map
void BasicSc2Bot::find_groups(std::vector<Point2D>& points,
	int minimum_points_per_group,
	int max_distance_between_points) {
	const ObservationInterface* obs = Observation();
	const int NOT_INTERESTED = -2;
	const int NOT_COLORED_YET = -1;
	int currentColor = NOT_COLORED_YET;
	const float step = minimum_points_per_group == -1 ? 0.5f : 1.0f;
	const unsigned int height =
		static_cast<unsigned int>(obs->GetGameInfo().height / step);
	const unsigned int width =
		static_cast<unsigned int>(obs->GetGameInfo().width / step);
	std::vector<std::vector<int>> picture(
		height, std::vector<int>(width, NOT_INTERESTED));

	auto paint = [&picture, &currentColor, step](const Point2D& pt) {
		int x = static_cast<int>(pt.x / step);
		int y = static_cast<int>(pt.y / step);
		picture[y][x] = currentColor;
		if (step == 0.5f) {
			picture[y + 1][x] = currentColor;
			picture[y][x + 1] = currentColor;
			picture[y + 1][x + 1] = currentColor;
		}
		};

	std::vector<Point2DI> nearby;
	for (int dx = -max_distance_between_points;
		dx <= max_distance_between_points; ++dx) {
		for (int dy = -max_distance_between_points;
			dy <= max_distance_between_points; ++dy) {
			if (abs(dx) + abs(dy) <= max_distance_between_points) {
				nearby.emplace_back(Point2DI(dx, dy));
			}
		}
	}
	for (const auto& point : points) {
		paint(point);
	}

	std::vector<Point2D> remaining(points.begin(), points.end());
	std::deque<Point2D> queue;

	// flood fill
	while (!remaining.empty()) {
		std::vector<Point2D> currentGroup;
		if (queue.empty()) {
			++currentColor;
			auto start = remaining.back();
			remaining.pop_back();
			paint(start);
			queue.emplace_back(start);
			currentGroup.emplace_back(start);
		}
		while (!queue.empty()) {
			Point2D base = queue.front();
			queue.pop_front();
			for (const auto& offset : nearby) {
				float px = base.x + offset.x * step;
				float py = base.y + offset.y * step;
				if (px < 0 || py < 0 || px >= width * step ||
					py >= height * step) {
					continue;
				}
				Point2D point(px, py);
				auto it = std::find(remaining.begin(), remaining.end(), point);
				if (it != remaining.end()) {
					remaining.erase(it);
					paint(point);
					queue.emplace_back(point);
					currentGroup.emplace_back(point);
				}
			}
		}
		if (minimum_points_per_group != -1) {
			if (currentGroup.size() >= minimum_points_per_group) {
				std::sort(currentGroup.begin(), currentGroup.end(),
					[this](const Point2D& a, const Point2D& b) {
						return height_at(Point2DI(a)) >
							height_at(Point2DI(b));
					});
				ramps.emplace_back(currentGroup);
			}
		}
		else {
			std::map<Point2D, bool, Point2DComparator> groups;
			if (currentGroup.size() != 1) {
				for (const auto& point : currentGroup) {
					groups.insert({ point, true });
				}
				build_map.emplace_back(groups);
			}
		}
	}
	// This means I am trying to build a buildable map
	if (minimum_points_per_group == -1) {
		std::sort(
			build_map.begin(), build_map.end(),
			[this](const std::map<Point2D, bool, Point2DComparator>& map1,
				const std::map<Point2D, bool, Point2DComparator>& map2) {
					return Distance2D(Point2D_mean(map1), start_location) <
						Distance2D(Point2D_mean(map2), start_location);
			});

		Point2D build_max;
		Point2D build_min;

		float minY = std::numeric_limits<float>::max();
		float maxY = std::numeric_limits<float>::lowest();

		// Iterate through the build_map to find min and max y values
		for (const auto& p : build_map[0]) {
			if (p.first.y < minY) {
				minY = p.first.y;
			}
			if (p.first.y > maxY) {
				maxY = p.first.y;
			}
		}
		build_min.x = build_map[0].begin()->first.x;  // x min
		build_max.x = build_map[0].rbegin()->first.x; // x max
		// Now minY and maxY hold the minimum and maximum y values respectively
		build_min.y = minY;
		build_max.y = maxY;

		build_map_minmax = { build_min, build_max };
	}
	return;
}

// find the ramps or the buildable map
void BasicSc2Bot::find_ramps_build_map(bool isRamp) {
	const ObservationInterface* obs = Observation();
	std::vector<Point2D> mapVec;
	unsigned int width = playable_max.x;
	unsigned int height = playable_max.y;
	int max_num_points = isRamp ? 8 : -1;

	for (unsigned int j = playable_min.y; j < height; ++j) {
		for (unsigned int i = playable_min.x; i < width; ++i) {
			Point2D temp(i, j);
			if (obs->IsPathable(temp) &&
				(isRamp ? !obs->IsPlacable(temp) : obs->IsPlacable(temp))) {
				mapVec.emplace_back(temp);
			}
		}
	}
	find_groups(mapVec, max_num_points, 2);
}

// return the points of upper or lower part of the ramp
// up is true, upper part, otherwise lower part
std::vector<Point2D>
BasicSc2Bot::upper_lower(const std::vector<Point2D>& points, bool up) const {
	std::vector<Point2D> up_low_points;
	int height;

	if (up) {
		height = height_at(Point2DI(points[0]));
		up_low_points.emplace_back(points[0]);

		for (size_t i = 1; i < points.size(); ++i) {
			if (height_at(Point2DI(points[i])) == height) {
				up_low_points.emplace_back(points[i]);
			}
		}
	}
	else {
		height = height_at(Point2DI(points[points.size() - 1]));
		up_low_points.emplace_back(points[points.size() - 1]);

		for (size_t i = points.size() - 2; i > 0; --i) {
			if (height_at(Point2DI(points[i])) == height) {
				up_low_points.emplace_back(points[i]);
			}
		}
	}
	return up_low_points;
}

// return the center point of up or lower part of the ramp
Point2D BasicSc2Bot::top_bottom_center(const std::vector<Point2D>& points,
	const bool up) const {
	std::vector<Point2D> top_bottom_points;
	if (up) {
		top_bottom_points = upper_lower(points, up);
	}
	else {
		top_bottom_points = upper_lower(points, up);
	}
	return Point2D_mean(top_bottom_points);
}

// return two upper points farthest from the bottom center
// it is gonna be used to find the depot locations
std::vector<Point2D>
BasicSc2Bot::upper2_for_ramp_wall(const std::vector<Point2D>& points) const {
	std::vector<Point2D> upper = upper_lower(points, true);
	Point2D bottom_center = top_bottom_center(points, false);

	std::sort(upper.begin(), upper.end(),
		[this, &bottom_center](const Point2D& a, const Point2D& b) {
			return Distance2D(a, bottom_center) >
				Distance2D(b, bottom_center);
		});
	return { upper[0], upper[1] };
}

// find the middle depot or barrack location
// if isdepot is true, it is depot, otherwise barrack
// depending on whether it is depot or barrack, the distance is different
Point2D BasicSc2Bot::depot_barrack_in_middle(const std::vector<Point2D>& points,
	const std::vector<Point2D>& upper2,
	const bool isdepot) const {
	Point2D offset(0.5, 0.5);
	const float inter_distance = isdepot ? 2.5 : 5;
	std::vector<Point2D> offsetUpper2 = { upper2[0] + offset,
										 upper2[1] + offset };
	std::vector<Point2D> intersects = circle_intersection(
		offsetUpper2[0], offsetUpper2[1], std::sqrt(inter_distance));
	std::vector<Point2D> lower = upper_lower(points, false);

	Point2D anyLowerPoint = lower[0];
	return *std::max_element(
		intersects.begin(), intersects.end(),
		[&anyLowerPoint](const Point2D& a, const Point2D& b) {
			return Distance2D(a, anyLowerPoint) < Distance2D(b, anyLowerPoint);
		});
}


// find the cornner depot locations
std::vector<Point2D>
BasicSc2Bot::corner_depots(const std::vector<Point2D>& points) const {
	std::vector<Point2D> corner_depots;
	// upper2 is the two upper points farthest from the bottom center
	std::vector<Point2D> upper2 = upper2_for_ramp_wall(points);
	Point2D offset(0.5, 0.5);

	for (const auto& p : upper2) {
		corner_depots.emplace_back(p + offset);
	}

	// after adding offset, find the middle point
	Point2D center =
		towards(corner_depots[0], corner_depots[1],
			(Distance2D(corner_depots[0], corner_depots[1])) / 2);
	// find the depot middle point
	Point2D depotPosition = depot_barrack_in_middle(points, upper2, true);
	// find the intersection points
	std::vector<Point2D> intersects =
		circle_intersection(center, depotPosition, std::sqrt(5));
	std::sort(intersects.begin(), intersects.end(),
		[](const Point2D& a, const Point2D& b) { return a.x > b.x; });

	return intersects;
}

// barrack location adjustment
bool BasicSc2Bot::barracks_can_fit_addon(const Point2D& barrack_point) const {
	return (barrack_point.x + 1) > (mainBase_depot_points[0].x);
}

// find the correct barrack location
Point2D BasicSc2Bot::barracks_correct_placement(
	const std::vector<Point2D>& ramp_points,
	const std::vector<Point2D>& corner_depots) const {
	Point2D bpoint = depot_barrack_in_middle(
		ramp_points, upper2_for_ramp_wall(ramp_points), false);
	if (barracks_can_fit_addon(bpoint)) {
		return bpoint;
	}
	else {
		return Point2D(bpoint.x - 2, bpoint.y);
	}
}


// find the right ramp vector
// For proxima map, the right ramp is not the closeest one
void BasicSc2Bot::find_right_ramp(const Point2D& location) {
	find_ramps_build_map(true);
	// location could be start location or any other command center location
	//  find the ramp set that is closest to the location

	std::sort(ramps.begin(), ramps.end(),
		[this, &location](const std::vector<Point2D>& a,
			const std::vector<Point2D>& b) {
				return Distance2D(Point2D_mean(a), location) <
					Distance2D(Point2D_mean(b), location);
		});

	std::vector<Point2D> main_ramp;
	ramps[0].size() < ramps[1].size() ? main_ramp = ramps[0]
		: main_ramp = ramps[1];

	//! right_ramp.size() == 2 and they are correct
	mainBase_depot_points = corner_depots(main_ramp);
	mainBase_barrack_point =
		barracks_correct_placement(main_ramp, mainBase_depot_points);
	return;
}


// Managing depot control
// Raise depots when enemies are nearby
// Lower depots when no enemies are nearby
void BasicSc2Bot::depot_control() {
	const ObservationInterface* obs = Observation();

	// checking the ramp depots
	Units dp_being_built_1 =
		obs->GetUnits(Unit::Self, [this](const Unit& unit) {
		// display_type == 4 means the unit is Placeholder(?)
		return unit.unit_type == UNIT_TYPEID::TERRAN_SUPPLYDEPOT &&
			Point2D(unit.pos) == mainBase_depot_points[0] &&
			unit.display_type != 4;
			});
	Units dp_being_built_2 =
		obs->GetUnits(Unit::Self, [this](const Unit& unit) {
		// display_type == 4 means the unit is Placeholder(?)
		return unit.unit_type == UNIT_TYPEID::TERRAN_SUPPLYDEPOT &&
			Point2D(unit.pos) == mainBase_depot_points[1] &&
			unit.display_type != 4;
			});

	//! first depot is the one that is built first
	if (!ramp_depots[0] && !dp_being_built_1.empty()) {
		ramp_depots[0] = const_cast<Unit*>(dp_being_built_1.front());
	}
	else if (!ramp_depots[1] && !dp_being_built_2.empty()) {
		ramp_depots[1] = const_cast<Unit*>(dp_being_built_2.front());
	}

	Units depots = obs->GetUnits(Unit::Alliance::Self,
		IsUnit(UNIT_TYPEID::TERRAN_SUPPLYDEPOT));
	Units lowered_depots = obs->GetUnits(
		Unit::Alliance::Self, IsUnit(UNIT_TYPEID::TERRAN_SUPPLYDEPOTLOWERED));
	Units enemy_units = obs->GetUnits(Unit::Alliance::Enemy);

	// Raise depots when enemies are nearby
	for (const auto& depo : depots) {
		if (!EnemyNearby(depo->pos, false)) {
			Actions()->UnitCommand(depo, ABILITY_ID::MORPH_SUPPLYDEPOT_LOWER);
		}
	}

	// Lower depots when no enemies are nearby
	for (const auto& depo : lowered_depots) {
		if (EnemyNearby(depo->pos, false, 10)) {
			Actions()->UnitCommand(depo, ABILITY_ID::MORPH_SUPPLYDEPOT_RAISE);
		}
	}
}
