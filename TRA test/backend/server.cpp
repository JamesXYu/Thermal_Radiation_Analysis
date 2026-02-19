#include "include/httplib.h"
#include <iostream>
#include <string>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <numeric>
#include <optional>
#include <random>
#include <tuple>
#include <utility>
#include <vector>
#include <fstream>
#include <sstream>
#include <cstdlib>
#include <map>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// ===== Include all the calculation logic from calcus.cpp =====

// Simple 3D vector struct with basic operations
struct Vec3 {
    double x;
    double y;
    double z;

    Vec3() : x(0.0), y(0.0), z(0.0) {}
    Vec3(double x_, double y_, double z_) : x(x_), y(y_), z(z_) {}

    Vec3 operator+(const Vec3& other) const { return {x + other.x, y + other.y, z + other.z}; }
    Vec3 operator-(const Vec3& other) const { return {x - other.x, y - other.y, z - other.z}; }
    Vec3 operator*(double s) const { return {x * s, y * s, z * s}; }
    Vec3 operator/(double s) const { return {x / s, y / s, z / s}; }

    Vec3& operator+=(const Vec3& other) { x += other.x; y += other.y; z += other.z; return *this; }
    Vec3& operator-=(const Vec3& other) { x -= other.x; y -= other.y; z -= other.z; return *this; }
    Vec3& operator*=(double s) { x *= s; y *= s; z *= s; return *this; }
    Vec3& operator/=(double s) { x /= s; y /= s; z /= s; return *this; }
};

static inline double dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static inline Vec3 cross(const Vec3& a, const Vec3& b) {
    return {
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x
    };
}
static inline double length(const Vec3& v) { return std::sqrt(dot(v, v)); }
static inline Vec3 normalize(const Vec3& v) {
    double len = length(v);
    if (len <= 1e-12) return {0.0, 0.0, 0.0};
    return v / len;
}

struct Plane {
    Vec3 normal;
    Vec3 point; // any point on plane
};

struct PolygonWithTemp {
	std::vector<Vec3> vertices;
	double temperature;
};

struct ReceiverPoint {
	Vec3 origin;
	Vec3 normal;
};

struct PlaneData {
	size_t width;
	size_t height;
	size_t numPoints;
};

// Compute plane from polygon vertices (assumes first 3 non-collinear define plane)
inline std::optional<Plane> getPolygonPlane(const std::vector<Vec3>& verts) {
    if (verts.size() < 3) return std::nullopt;
    Vec3 v1 = verts[1] - verts[0];
    Vec3 v2 = verts[2] - verts[0];
    Vec3 n = cross(v1, v2);
    double nmag = length(n);
    if (nmag < 1e-9) return std::nullopt;
    n = n / nmag;
    return Plane{n, verts[0]};
}

// Ray-plane intersection: returns intersection point and t, or nullopt/inf if no forward hit
inline std::pair<std::optional<Vec3>, double> rayPlaneIntersect(
    const Vec3& rayOrigin,
    const Vec3& rayDir,
    const Vec3& planeNormal,
    const Vec3& pointOnPlane
) {
    double ndotu = dot(planeNormal, rayDir);
    if (std::fabs(ndotu) < 1e-9) {
        return {std::nullopt, std::numeric_limits<double>::infinity()};
    }
    Vec3 w = rayOrigin - pointOnPlane;
    double t = -dot(planeNormal, w) / ndotu;
    if (t < 1e-7) {
        return {std::nullopt, std::numeric_limits<double>::infinity()};
    }
    Vec3 p = rayOrigin + rayDir * t;
    return {p, t};
}

// Project 3D polygon and point onto the dominant plane and do 2D point-in-polygon test (winding/non-zero)
static bool isPointInPolygon2D(const std::vector<std::array<double,2>>& poly, double x, double y) {
    // Ray casting even-odd rule
    bool inside = false;
    size_t n = poly.size();
    for (size_t i = 0, j = n - 1; i < n; j = i++) {
        const auto& pi = poly[i];
        const auto& pj = poly[j];
        bool intersect = ((pi[1] > y) != (pj[1] > y)) &&
                         (x < (pj[0] - pi[0]) * (y - pi[1]) / ((pj[1] - pi[1]) + 1e-30) + pi[0]);
        if (intersect) inside = !inside;
    }
    return inside;
}

inline bool isPointInPolygon3D(const Vec3& p, const std::vector<Vec3>& polygon, const Vec3& polygonNormal) {
    Vec3 absn { std::fabs(polygonNormal.x), std::fabs(polygonNormal.y), std::fabs(polygonNormal.z) };
    int a = 0, b = 1; // indices to keep
    if (absn.x >= absn.y && absn.x >= absn.z) { a = 1; b = 2; }
    else if (absn.y >= absn.x && absn.y >= absn.z) { a = 0; b = 2; }
    else { a = 0; b = 1; }

    std::vector<std::array<double,2>> poly2d;
    poly2d.reserve(polygon.size());
    for (const auto& v : polygon) {
        double coords[3] = {v.x, v.y, v.z};
        poly2d.push_back({coords[a], coords[b]});
    }
    double pc[3] = {p.x, p.y, p.z};
    return isPointInPolygon2D(poly2d, pc[a], pc[b]);
}

// Generate cosine-weighted hemisphere directions around a given normal (using provided RNG)
std::vector<Vec3> generateCosineHemisphereRays(size_t numRays, const Vec3& surfaceNormal, std::mt19937_64& rng) {
    std::vector<Vec3> rays;
    if (numRays == 0) return rays;
    rays.reserve(numRays);

    Vec3 w = normalize(surfaceNormal);
    Vec3 u;
    if (std::fabs(w.x) > 0.9999) {
        u = normalize(cross({0.0, 1.0, 0.0}, w));
    } else {
        u = normalize(cross({1.0, 0.0, 0.0}, w));
    }
	Vec3 v = cross(w, u);
    std::uniform_real_distribution<double> dist(0.0, 1.0);

    for (size_t i = 0; i < numRays; ++i) {
        double u1 = dist(rng);
        double u2 = dist(rng);
        double phi = 2.0 * M_PI * u1;
        double cosTheta = std::sqrt(1.0 - u2);
        double sinTheta = std::sqrt(u2);
        double x = sinTheta * std::cos(phi);
        double y = sinTheta * std::sin(phi);
        double z = cosTheta;
        Vec3 local {x, y, z};
        // rotate to world
        Vec3 world {
            u.x * local.x + v.x * local.y + w.x * local.z,
            u.y * local.x + v.y * local.y + w.y * local.z,
            u.z * local.x + v.z * local.y + w.z * local.z
        };
        rays.push_back(world);
    }
    return rays;
}

// Calculate view factors from a point origin to a set of polygon emitters with occlusion between them
struct ViewFactorResult {
    std::vector<double> viewFactors; // per polygon
    std::vector<Vec3> allRayDirs;
    std::vector<Vec3> hitPoints;
    std::vector<Vec3> hitRayDirs; // those rays that hit some polygon
};

ViewFactorResult calculateViewFactorsWithBlockage(
	const Vec3& origin,
	const Vec3& originNormal,
	const std::vector<PolygonWithTemp>& emitterPolygons,
	const std::vector<std::vector<Vec3>>& inertPolygons,
	size_t numRays,
	std::mt19937_64& rng
) {
	ViewFactorResult res;
	res.viewFactors.assign(emitterPolygons.size(), 0.0);
	if (numRays == 0) return res;

	std::vector<Vec3> rays = generateCosineHemisphereRays(numRays, originNormal, rng);
	res.allRayDirs = rays;

	struct PolyData { std::vector<Vec3> verts; Vec3 normal; Vec3 point; bool valid; };
	std::vector<PolyData> inertScene;
	inertScene.reserve(inertPolygons.size());
	for (const auto& poly : inertPolygons) {
		auto pl = getPolygonPlane(poly);
		if (!pl) inertScene.push_back({poly, {0,0,0}, {0,0,0}, false});
		else inertScene.push_back({poly, pl->normal, pl->point, true});
	}

	std::vector<PolyData> emitScene;
	emitScene.reserve(emitterPolygons.size());
	for (const auto& poly : emitterPolygons) {
		auto pl = getPolygonPlane(poly.vertices);
		if (!pl) emitScene.push_back({poly.vertices, {0,0,0}, {0,0,0}, false});
		else emitScene.push_back({poly.vertices, pl->normal, pl->point, true});
	}

	std::vector<std::size_t> hitCounts(emitterPolygons.size(), 0);

	for (size_t i = 0; i < numRays; ++i) {
		const Vec3& rdir = rays[i];
		double closestInert = std::numeric_limits<double>::infinity();
		double closestEmit = std::numeric_limits<double>::infinity();
		int finalIdx = -1;
		Vec3 closestPoint;

		// Check inert polygons first (find closest inert hit)
		for (size_t p = 0; p < inertScene.size(); ++p) {
			const auto& pd = inertScene[p];
			if (!pd.valid) continue;
			auto [hit, t] = rayPlaneIntersect(origin, rdir, pd.normal, pd.point);
			if (hit) {
				if (t < closestInert) {
					if (isPointInPolygon3D(*hit, pd.verts, pd.normal)) {
						closestInert = t;
					}
				}
			}
		}

		// Then check emitter polygons (find closest emitter hit)
		for (size_t p = 0; p < emitScene.size(); ++p) {
			const auto& pd = emitScene[p];
			if (!pd.valid) continue;
			auto [hit, t] = rayPlaneIntersect(origin, rdir, pd.normal, pd.point);
			if (hit) {
				if (t < closestEmit) {
					if (isPointInPolygon3D(*hit, pd.verts, pd.normal)) {
						closestEmit = t;
						finalIdx = static_cast<int>(p);
						closestPoint = *hit;
					}
				}
			}
		}

		bool blockedByInert = (closestInert < std::numeric_limits<double>::infinity()) && (closestInert <= closestEmit);
		if (!blockedByInert && finalIdx != -1) {
			hitCounts[static_cast<size_t>(finalIdx)] += 1;
			res.hitPoints.push_back(closestPoint);
			res.hitRayDirs.push_back(rdir);
		}
	}

	for (size_t p = 0; p < emitterPolygons.size(); ++p) {
		res.viewFactors[p] = static_cast<double>(hitCounts[p]) / static_cast<double>(numRays);
	}
	return res;
}

// JSON parsing functions
namespace mini_json {
	inline void skipSpaces(const std::string& s, size_t& i) {
		while (i < s.size() && (s[i] == ' ' || s[i] == '\n' || s[i] == '\r' || s[i] == '\t')) ++i;
	}
	inline bool expectChar(const std::string& s, size_t& i, char c) {
		skipSpaces(s, i);
		if (i < s.size() && s[i] == c) { ++i; return true; }
		return false;
	}
	inline bool parseNumber(const std::string& s, size_t& i, double& out) {
		skipSpaces(s, i);
		const char* start = s.c_str() + i;
		char* endptr = nullptr;
		out = std::strtod(start, &endptr);
		if (endptr == start) return false;
		i += static_cast<size_t>(endptr - start);
		return true;
	}
	inline bool parseUInt64(const std::string& s, size_t& i, std::uint64_t& out) {
		skipSpaces(s, i);
		const char* start = s.c_str() + i;
		char* endptr = nullptr;
		unsigned long long v = std::strtoull(start, &endptr, 10);
		if (endptr == start) return false;
		out = static_cast<std::uint64_t>(v);
		i += static_cast<size_t>(endptr - start);
		return true;
	}
	inline bool parseKey(const std::string& s, size_t& i, const std::string& key) {
		skipSpaces(s, i);
		if (!expectChar(s, i, '"')) return false;
		size_t k = i;
		while (k < s.size() && s[k] != '"') ++k;
		if (k >= s.size()) return false;
		std::string found = s.substr(i, k - i);
		i = k + 1;
		skipSpaces(s, i);
		if (!expectChar(s, i, ':')) return false;
		return found == key;
	}
	inline bool parseVec3(const std::string& s, size_t& i, Vec3& v) {
		if (!expectChar(s, i, '[')) return false;
		double a, b, c;
		if (!parseNumber(s, i, a)) return false;
		if (!expectChar(s, i, ',')) return false;
		if (!parseNumber(s, i, b)) return false;
		if (!expectChar(s, i, ',')) return false;
		if (!parseNumber(s, i, c)) return false;
		if (!expectChar(s, i, ']')) return false;
		v = Vec3{a, b, c};
		return true;
	}
	inline bool parsePolygons(const std::string& s, size_t& i, std::vector<std::vector<Vec3>>& polys) {
		if (!expectChar(s, i, '[')) return false; // polygons array start
		skipSpaces(s, i);
		if (i < s.size() && s[i] == ']') { ++i; return true; }
		while (i < s.size()) {
			if (!expectChar(s, i, '[')) return false; // one polygon start
			std::vector<Vec3> poly;
			skipSpaces(s, i);
			if (i < s.size() && s[i] == ']') { ++i; polys.push_back(std::move(poly)); }
			else {
				while (i < s.size()) {
					Vec3 v;
					if (!parseVec3(s, i, v)) return false;
					poly.push_back(v);
					skipSpaces(s, i);
					if (i < s.size() && s[i] == ',') { ++i; continue; }
					if (i < s.size() && s[i] == ']') { ++i; break; }
					return false;
				}
				polys.push_back(std::move(poly));
			}
			skipSpaces(s, i);
			if (i < s.size() && s[i] == ',') { ++i; continue; }
			if (i < s.size() && s[i] == ']') { ++i; break; }
			return false;
		}
		return true;
	}
	inline bool parseReceiverPoint(const std::string& s, size_t& i, ReceiverPoint& rp) {
		if (!expectChar(s, i, '{')) return false;
		
		bool haveOrigin = false, haveNormal = false;
		
		while (i < s.size()) {
			skipSpaces(s, i);
			if (i < s.size() && s[i] == '}') { ++i; break; }
			
			size_t save = i;
			if (parseKey(s, i, "origin")) {
				Vec3 v; if (!parseVec3(s, i, v)) return false;
				rp.origin = v; haveOrigin = true;
			} else { i = save; }
			
			save = i;
			if (parseKey(s, i, "normal")) {
				Vec3 v; if (!parseVec3(s, i, v)) return false;
				rp.normal = v; haveNormal = true;
			} else { i = save; }
			
			skipSpaces(s, i);
			if (i < s.size() && s[i] == ',') { ++i; continue; }
		}
		
		return haveOrigin && haveNormal;
	}
	inline bool parseReceiverPoints(const std::string& s, size_t& i, std::vector<ReceiverPoint>& points) {
		if (!expectChar(s, i, '[')) return false;
		skipSpaces(s, i);
		if (i < s.size() && s[i] == ']') { ++i; return true; }
		
		while (i < s.size()) {
			ReceiverPoint rp;
			if (!parseReceiverPoint(s, i, rp)) return false;
			points.push_back(rp);
			
			skipSpaces(s, i);
			if (i < s.size() && s[i] == ',') { ++i; continue; }
			if (i < s.size() && s[i] == ']') { ++i; break; }
			return false;
		}
		return true;
	}
	
	inline bool parseReceiverPlaneData(const std::string& s, size_t& i, std::string& planeName, double& width, double& height, std::vector<ReceiverPoint>& points) {
		if (!expectChar(s, i, '{')) return false;
		skipSpaces(s, i);
		if (i < s.size() && s[i] == '}') { ++i; return true; }
		
		while (i < s.size()) {
			skipSpaces(s, i);
			if (i < s.size() && s[i] == '}') { ++i; break; }
			
			size_t save = i;
			if (parseKey(s, i, "width")) {
				double w;
				if (!parseNumber(s, i, w)) return false;
				width = w;
			} else { i = save; }
			
			save = i;
			if (parseKey(s, i, "height")) {
				double h;
				if (!parseNumber(s, i, h)) return false;
				height = h;
			} else { i = save; }
			
			save = i;
			if (parseKey(s, i, "points")) {
				if (!parseReceiverPoints(s, i, points)) return false;
			} else { i = save; }
			
			skipSpaces(s, i);
			if (i < s.size() && s[i] == ',') { ++i; continue; }
		}
		return true;
	}
	
	inline bool parseReceiverPlanes(const std::string& s, size_t& i, std::string& planeName, double& width, double& height, std::vector<ReceiverPoint>& points) {
		if (!expectChar(s, i, '{')) return false;
		skipSpaces(s, i);
		if (i < s.size() && s[i] == '}') { ++i; return true; }
		
		while (i < s.size()) {
			skipSpaces(s, i);
			if (i < s.size() && s[i] == '}') { ++i; break; }
			
			// Parse plane name as key
			if (!expectChar(s, i, '"')) return false;
			size_t nameStart = i;
			while (i < s.size() && s[i] != '"') ++i;
			if (i >= s.size()) return false;
			planeName = s.substr(nameStart, i - nameStart);
			++i; // skip closing quote
			
			skipSpaces(s, i);
			if (!expectChar(s, i, ':')) return false;
			
			if (!parseReceiverPlaneData(s, i, planeName, width, height, points)) return false;
			
			skipSpaces(s, i);
			if (i < s.size() && s[i] == ',') { ++i; continue; }
		}
		return true;
	}
	
	// New function to parse multiple receiver planes into a map
	inline bool parseReceiverPlanesMulti(const std::string& s, size_t& i, std::map<std::string, PlaneData>& planeMap, std::vector<ReceiverPoint>& allPoints) {
		if (!expectChar(s, i, '{')) return false;
		skipSpaces(s, i);
		if (i < s.size() && s[i] == '}') { ++i; return true; }
		
		while (i < s.size()) {
			skipSpaces(s, i);
			if (i < s.size() && s[i] == '}') { ++i; break; }
			
			// Parse plane name as key
			if (!expectChar(s, i, '"')) return false;
			size_t nameStart = i;
			while (i < s.size() && s[i] != '"') ++i;
			if (i >= s.size()) return false;
			std::string planeName = s.substr(nameStart, i - nameStart);
			++i; // skip closing quote
			
			skipSpaces(s, i);
			if (!expectChar(s, i, ':')) return false;
			
			// Parse plane data
			double width = 0, height = 0;
			std::vector<ReceiverPoint> planePoints;
			std::string tempName;
			if (!parseReceiverPlaneData(s, i, tempName, width, height, planePoints)) return false;
			
			// Store plane metadata
			PlaneData pd;
			pd.width = static_cast<size_t>(width);
			pd.height = static_cast<size_t>(height);
			pd.numPoints = planePoints.size();
			planeMap[planeName] = pd;
			
			// Add points to global list
			for (const auto& pt : planePoints) {
				allPoints.push_back(pt);
			}
			
			skipSpaces(s, i);
			if (i < s.size() && s[i] == ',') { ++i; continue; }
		}
		return true;
	}
	
	inline bool parseSinglePolygon(const std::string& s, size_t& i, std::vector<Vec3>& vertices) {
		if (!expectChar(s, i, '[')) return false;
		skipSpaces(s, i);
		if (i < s.size() && s[i] == ']') { ++i; return true; }
		
		while (i < s.size()) {
			Vec3 v;
			if (!parseVec3(s, i, v)) return false;
			vertices.push_back(v);
			
			skipSpaces(s, i);
			if (i < s.size() && s[i] == ',') { ++i; continue; }
			if (i < s.size() && s[i] == ']') { ++i; break; }
			return false;
		}
		return true;
	}
	
	inline bool parsePolygonWithTemp(const std::string& s, size_t& i, PolygonWithTemp& poly) {
		if (!expectChar(s, i, '{')) return false;
		
		bool havePolygon = false, haveTemperature = false;
		
		while (i < s.size()) {
			skipSpaces(s, i);
			if (i < s.size() && s[i] == '}') { ++i; break; }
			
			size_t save = i;
			if (parseKey(s, i, "polygon")) {
				if (!parseSinglePolygon(s, i, poly.vertices)) return false;
				havePolygon = true;
			} else { i = save; }
			
			save = i;
			if (parseKey(s, i, "temperature")) {
				double temp;
				if (!parseNumber(s, i, temp)) return false;
				poly.temperature = temp;
				haveTemperature = true;
			} else { i = save; }
			
			skipSpaces(s, i);
			if (i < s.size() && s[i] == ',') { ++i; continue; }
		}
		
		return havePolygon && haveTemperature;
	}
	
	inline bool parsePolygonsWithTemp(const std::string& s, size_t& i, std::vector<PolygonWithTemp>& polys) {
		if (!expectChar(s, i, '[')) return false;
		skipSpaces(s, i);
		if (i < s.size() && s[i] == ']') { ++i; return true; }
		
		while (i < s.size()) {
			PolygonWithTemp poly;
			if (!parsePolygonWithTemp(s, i, poly)) return false;
			polys.push_back(poly);
			
			skipSpaces(s, i);
			if (i < s.size() && s[i] == ',') { ++i; continue; }
			if (i < s.size() && s[i] == ']') { ++i; break; }
			return false;
		}
		return true;
	}
}

struct JsonInput {
	std::vector<ReceiverPoint> receiverPoints;
	std::vector<PolygonWithTemp> polygons;
	std::vector<std::vector<Vec3>> inertPolygons;
	std::size_t numRays {100000};
	std::optional<std::uint64_t> seed;
	
	// Map of plane name -> plane metadata
	std::map<std::string, PlaneData> planeDataMap;
};

static bool parseInputJson(const std::string& json, JsonInput& out, std::string& error) {
	using namespace mini_json;
	size_t i = 0;
	skipSpaces(json, i);
	if (!expectChar(json, i, '{')) { error = "Expected '{'"; return false; }

	bool haveReceiverPlanes = false, havePolygons = false;

	while (i < json.size()) {
		skipSpaces(json, i);
		if (i < json.size() && json[i] == '}') { ++i; break; }

		size_t save = i;
		if (parseKey(json, i, "receiver_planes")) {
			if (!parseReceiverPlanesMulti(json, i, out.planeDataMap, out.receiverPoints)) { 
				error = "Invalid receiver_planes"; 
				return false; 
			}
			haveReceiverPlanes = true;
		} else { i = save; }

		save = i;
		if (parseKey(json, i, "polygons")) {
			size_t savePos = i;
			if (parsePolygonsWithTemp(json, i, out.polygons)) {
				havePolygons = true;
			} else {
				i = savePos;
				std::vector<std::vector<Vec3>> legacyPolygons;
				if (parsePolygons(json, i, legacyPolygons)) {
					out.polygons.clear();
					for (const auto& poly : legacyPolygons) {
						out.polygons.push_back({poly, 0.0});
					}
					havePolygons = true;
				} else {
					error = "Invalid polygons format";
					return false;
				}
			}
		} else { i = save; }

		save = i;
		if (parseKey(json, i, "inert_polygons")) {
			std::vector<std::vector<Vec3>> tmp;
			if (!parsePolygons(json, i, tmp)) { error = "Invalid inert_polygons"; return false; }
			out.inertPolygons = std::move(tmp);
		} else { i = save; }

		save = i;
		if (parseKey(json, i, "num_rays")) {
			double n; if (!parseNumber(json, i, n)) { error = "Invalid num_rays"; return false; }
			if (n < 0) n = 0; out.numRays = static_cast<std::size_t>(n);
		} else { i = save; }

		save = i;
		if (parseKey(json, i, "seed")) {
			std::uint64_t s; if (!parseUInt64(json, i, s)) { error = "Invalid seed"; return false; }
			out.seed = s;
		} else { i = save; }

		skipSpaces(json, i);
		if (i < json.size() && json[i] == ',') { ++i; continue; }
	}

	if (!haveReceiverPlanes) {
		error = "Must provide 'receiver_planes' field";
		return false;
	}
	
	if (out.receiverPoints.empty()) {
		error = "receiver_planes is empty";
		return false;
	}
	
	if (!havePolygons) { error = "Missing polygons"; return false; }
	return true;
}

static std::string runCalculation(const std::string& jsonInput, bool& ok) {
	JsonInput in;
	std::string err;
	if (!parseInputJson(jsonInput, in, err)) {
		ok = false;
		return std::string("{\"error\": \"") + err + "\"}";
	}

	std::mt19937_64 rng;
	if (in.seed.has_value()) {
		rng.seed(in.seed.value());
	} else {
		std::random_device rd;
		std::seed_seq seedSeq{rd(), rd(), rd(), rd(), rd(), rd()};
		rng = std::mt19937_64(seedSeq);
	}

	// Process each receiver plane separately
	std::ostringstream out;
	out << "{";
	out << "\"success\":true,";
	out << "\"planes\":[";
	
	size_t globalPointIdx = 0;
	bool firstPlane = true;
	
	std::cout << "=== Processing " << in.planeDataMap.size() << " receiver planes ===" << std::endl;
	std::cout << "Total receiver points: " << in.receiverPoints.size() << std::endl;
	
	// Iterate through each plane in the map
	for (const auto& planePair : in.planeDataMap) {
		const std::string& planeName = planePair.first;
		const PlaneData& planeData = planePair.second;
		
		std::cout << "Processing plane: \"" << planeName << "\"" << std::endl;
		std::cout << "  Grid: " << planeData.width << "x" << planeData.height << std::endl;
		std::cout << "  Num points: " << planeData.numPoints << std::endl;
		std::cout << "  Starting at globalPointIdx: " << globalPointIdx << std::endl;
		
		// Log first point's position and normal for debugging
		if (globalPointIdx < in.receiverPoints.size()) {
			const auto& firstPoint = in.receiverPoints[globalPointIdx];
			std::cout << "  Sample point 0 origin: [" << firstPoint.origin.x << ", " << firstPoint.origin.y << ", " << firstPoint.origin.z << "]" << std::endl;
			std::cout << "  Sample point 0 normal: [" << firstPoint.normal.x << ", " << firstPoint.normal.y << ", " << firstPoint.normal.z << "]" << std::endl;
		}
		
		// Log emitter info
		std::cout << "  Number of emitters: " << in.polygons.size() << std::endl;
		for (size_t i = 0; i < in.polygons.size(); ++i) {
			std::cout << "    Emitter " << i << ": temp=" << in.polygons[i].temperature << ", vertices=" << in.polygons[i].vertices.size() << std::endl;
			if (in.polygons[i].vertices.size() > 0) {
				std::cout << "      First vertex: [" << in.polygons[i].vertices[0].x << ", " << in.polygons[i].vertices[0].y << ", " << in.polygons[i].vertices[0].z << "]" << std::endl;
			}
		}
		
		if (!firstPlane) out << ",";
		firstPlane = false;
		
		// Calculate temperatures for this plane's points
		std::vector<double> planeTemperatures;
		planeTemperatures.reserve(planeData.numPoints);
		
		double minTemp = std::numeric_limits<double>::infinity();
		double maxTemp = -std::numeric_limits<double>::infinity();
		
		for (size_t localIdx = 0; localIdx < planeData.numPoints; ++localIdx) {
			if (globalPointIdx >= in.receiverPoints.size()) {
				std::cerr << "ERROR: globalPointIdx " << globalPointIdx << " exceeds receiverPoints size " << in.receiverPoints.size() << std::endl;
				break;
			}
			
			const auto& receiverPoint = in.receiverPoints[globalPointIdx];
			
			std::mt19937_64 pointRng = rng;
			if (in.seed.has_value()) {
				pointRng.seed(in.seed.value() + globalPointIdx * 12345);
			}
			
			auto res = calculateViewFactorsWithBlockage(receiverPoint.origin, receiverPoint.normal, in.polygons, in.inertPolygons, in.numRays, pointRng);
			
			double totalTemperature = 0.0;
			for (size_t p = 0; p < in.polygons.size(); ++p) {
				double temperatureContribution = res.viewFactors[p] * in.polygons[p].temperature;
				totalTemperature += temperatureContribution;
			}
			
			planeTemperatures.push_back(totalTemperature);
			
			if (totalTemperature < minTemp) minTemp = totalTemperature;
			if (totalTemperature > maxTemp) maxTemp = totalTemperature;
			
			globalPointIdx++;
		}
		
		std::cout << "  Finished plane \"" << planeName << "\"" << std::endl;
		std::cout << "    Temperature range: " << minTemp << " to " << maxTemp << std::endl;
		std::cout << "    Next globalPointIdx: " << globalPointIdx << std::endl;
		
		// Output this plane's data
		out << "{";
		out << "\"name\":\"" << planeName << "\",";
		out << "\"width\":" << planeData.width << ",";
		out << "\"height\":" << planeData.height << ",";
		out << "\"values\":[";
		for (size_t i = 0; i < planeTemperatures.size(); ++i) {
			if (i > 0) out << ",";
			out << planeTemperatures[i];
		}
		out << "]";
		out << "}";
	}
	
	out << "]";
	out << "}";
	
	ok = true;
	return out.str();
}

int main() {
    using namespace httplib;

    Server svr;

    // Long timeouts for Monte Carlo calculations (can take minutes)
    svr.set_read_timeout(300, 0);   // 5 min to receive request
    svr.set_write_timeout(300, 0);  // 5 min to send response
    svr.set_keep_alive_timeout(60); // 60 s keep-alive

    // Enable CORS for all routes
    svr.set_default_headers({
        {"Access-Control-Allow-Origin", "*"},
        {"Access-Control-Allow-Methods", "GET, POST, OPTIONS"},
        {"Access-Control-Allow-Headers", "Content-Type"}
    });

    // Handle OPTIONS requests (CORS preflight)
    // Note: Headers are already set globally above, just need to respond with 200
    svr.Options(".*", [](const Request& req, Response& res) {
        res.status = 200;
    });

    // Health check endpoint
    svr.Get("/health", [](const Request& req, Response& res) {
        res.set_content("{\"status\": \"ok\"}", "application/json");
    });

    // Status endpoint
    svr.Get("/status", [](const Request& req, Response& res) {
        res.set_content("{\"status\": \"running\", \"version\": \"1.0\"}", "application/json");
    });

    // Main calculation endpoint
    svr.Post("/calculate", [](const Request& req, Response& res) {
        std::cout << "Received calculation request" << std::endl;
        std::cout << "Request body length: " << req.body.length() << " bytes" << std::endl;
        
        bool ok = false;
        std::string result = runCalculation(req.body, ok);
        
        if (ok) {
            std::cout << "Calculation successful" << std::endl;
            res.set_content(result, "application/json");
        } else {
            std::cout << "Calculation failed: " << result << std::endl;
            res.status = 400;
            res.set_content(result, "application/json");
        }
    });

    std::cout << "========================================" << std::endl;
    std::cout << "Thermal Radiation Analysis Server" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "Server starting on 0.0.0.0:8080" << std::endl;
    std::cout << "  Local:   http://localhost:8080" << std::endl;
    std::cout << "  Network: http://192.168.0.218:8080" << std::endl;
    std::cout << "Endpoints:" << std::endl;
    std::cout << "  GET  /health     - Health check" << std::endl;
    std::cout << "  GET  /status     - Server status" << std::endl;
    std::cout << "  POST /calculate  - Run calculation" << std::endl;
    std::cout << "========================================" << std::endl;

    svr.listen("0.0.0.0", 8080);

    return 0;
}
