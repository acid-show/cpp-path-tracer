#include <iostream>
#include <vector>
#include <cmath>
#include <random>
#include <fstream>
#include <algorithm>
#include <string>
#include <sstream>
#include <limits>

constexpr double PI = 3.14159265358979323846;

// ==================== CONFIGURATION ====================

struct Config {
    // Rendering
    int width = 600;
    int height = 600;
    int spp = 100;          // Samples Per Pixel
    int maxDepth = 5;
    double gamma = 2.2;

    // Camera
    struct {
        double pos[3] = {0.5, 0.5, -1.5};
        double target[3] = {0.5, 0.5, 0};
        double fov = 45;
    } camera;

    // Materials
    struct {
        double white[3] = {0.8, 0.8, 0.8};   // White walls
        double red[3] = {0.8, 0.2, 0.2};     // Red wall
        double green[3] = {0.2, 0.8, 0.2};   // Green wall
        double mirror[3] = {0.9, 0.9, 0.9};  // Mirror
        double light[3] = {15, 15, 15};      // Light brightness (radiance)
        double lightSize = 0.2;              // Light source size
    } materials;

    // Additional parameters
    bool enableRussianRoulette = true;
    int rrStartDepth = 3;
    double rrProbability = 0.8; // continue probability (0..1)

    // Display flags
    bool showDirectLighting = true;
    bool showIndirectLighting = true;
    bool enableAntiAliasing = true;
};

// Global configuration
Config g_config;

// ==================== MATHEMATICS ====================

struct Vec3 {
    double x, y, z;

    Vec3() : x(0), y(0), z(0) {}
    Vec3(double v) : x(v), y(v), z(v) {}
    Vec3(double x_, double y_, double z_) : x(x_), y(y_), z(z_) {}

    Vec3 operator+(const Vec3& v) const { return {x+v.x, y+v.y, z+v.z}; }
    Vec3 operator-(const Vec3& v) const { return {x-v.x, y-v.y, z-v.z}; }
    Vec3 operator*(double s) const { return {x*s, y*s, z*s}; }
    Vec3 operator/(double s) const { return {x/s, y/s, z/s}; }
    Vec3 operator-() const { return {-x, -y, -z}; }

    Vec3 mul(const Vec3& v) const { return {x*v.x, y*v.y, z*v.z}; }

    bool hasNan() const {
        return std::isnan(x) || std::isnan(y) || std::isnan(z);
    }
};

double dot(const Vec3& a, const Vec3& b) {
    return a.x*b.x + a.y*b.y + a.z*b.z;
}

Vec3 cross(const Vec3& a, const Vec3& b) {
    return {
            a.y*b.z - a.z*b.y,
            a.z*b.x - a.x*b.z,
            a.x*b.y - a.y*b.x
    };
}

double length(const Vec3& v) {
    return std::sqrt(dot(v, v));
}

Vec3 normalize(const Vec3& v) {
    double len = length(v);
    if (len < 1e-8) return Vec3(0);
    return v / len;
}

struct Ray {
    Vec3 origin, direction;
    Ray(const Vec3& o, const Vec3& d) : origin(o), direction(normalize(d)) {}
};

static inline double cosineHemispherePDF(double cosTheta) {
    return cosTheta > 0.0 ? cosTheta / PI : 0.0;
}

// ==================== MATERIALS ====================

struct Material {
    Vec3 kd;        // Diffuse
    Vec3 ks;        // Specular (ideal mirror)
    Vec3 emission;  // Emission (radiance)

    Material(const Vec3& kd_ = Vec3(0), const Vec3& ks_ = Vec3(0),
             const Vec3& em_ = Vec3(0)) : kd(kd_), ks(ks_), emission(em_) {}

    bool isLight() const {
        return length(emission) > 1e-6;
    }
};

struct Triangle {
    Vec3 v0, v1, v2;
    Material* material;
    Vec3 normal;
    double area;

    Triangle(const Vec3& a, const Vec3& b, const Vec3& c, Material* mat)
            : v0(a), v1(b), v2(c), material(mat) {
        normal = normalize(cross(v1 - v0, v2 - v0));
        area = 0.5 * length(cross(v1 - v0, v2 - v0));
    }

    // Uniform sampling on triangle by area (sqrt trick)
    Vec3 randomPoint(std::mt19937& rng) const {
        std::uniform_real_distribution<double> dist(0, 1);
        double r1 = dist(rng);
        double r2 = dist(rng);

        double su = std::sqrt(r1);
        double u = 1.0 - su;
        double v = r2 * su;

        return v0 * (1.0 - u - v) + v1 * u + v2 * v;
    }
};

struct Hit {
    double t;
    Vec3 point, normal;
    Material* material;
    bool frontFace;

    Hit() : t(1e10), material(nullptr), frontFace(true) {}
};

// ==================== SPHERE FUNCTIONS ====================

struct Sphere {
    Vec3 center;
    double radius;
    Material* material;

    Sphere(const Vec3& c, double r, Material* mat) : center(c), radius(r), material(mat) {}

    bool intersect(const Ray& ray, Hit& hit) const {
        Vec3 oc = ray.origin - center;
        double a = dot(ray.direction, ray.direction);
        double b = 2.0 * dot(oc, ray.direction);
        double c = dot(oc, oc) - radius * radius;

        double discriminant = b * b - 4.0 * a * c;

        if (discriminant < 0) return false;

        double sqrtd = std::sqrt(discriminant);
        double t1 = (-b - sqrtd) / (2.0 * a);
        double t2 = (-b + sqrtd) / (2.0 * a);

        double t = t1;
        if (t < 1e-4) t = t2;
        if (t < 1e-4) return false;

        if (t < hit.t) {
            hit.t = t;
            hit.point = ray.origin + ray.direction * t;
            hit.normal = normalize(hit.point - center);
            hit.material = material;
            hit.frontFace = dot(ray.direction, hit.normal) < 0;
            if (!hit.frontFace) {
                hit.normal = -hit.normal;
            }
            return true;
        }
        return false;
    }
};

// ==================== INTERACTIVE INTERFACE ====================

void printMenu() {
    std::cout << "\n=== PATH TRACER - SETTINGS MENU ===\n";
    std::cout << "1. Render settings\n";
    std::cout << "2. Camera settings\n";
    std::cout << "3. Material settings\n";
    std::cout << "4. Lighting settings\n";
    std::cout << "5. Toggle effects\n";
    std::cout << "6. Quick presets\n";
    std::cout << "7. Show current settings\n";
    std::cout << "8. Start rendering\n";
    std::cout << "0. Exit\n";
    std::cout << "Choice: ";
}

void showRenderSettings() {
    std::cout << "\n=== RENDER SETTINGS ===\n";
    std::cout << "1. Resolution: " << g_config.width << "x" << g_config.height << "\n";
    std::cout << "2. SPP (samples per pixel): " << g_config.spp << "\n";
    std::cout << "3. Max path depth: " << g_config.maxDepth << "\n";
    std::cout << "4. Gamma correction: " << g_config.gamma << "\n";
    std::cout << "Choose parameter to change (1-4): ";

    int choice;
    std::cin >> choice;

    switch(choice) {
        case 1:
            std::cout << "Width: ";
            std::cin >> g_config.width;
            std::cout << "Height: ";
            std::cin >> g_config.height;
            break;
        case 2:
            std::cout << "SPP (10-2000): ";
            std::cin >> g_config.spp;
            break;
        case 3:
            std::cout << "Max depth (1-20): ";
            std::cin >> g_config.maxDepth;
            break;
        case 4:
            std::cout << "Gamma (1.0-3.0): ";
            std::cin >> g_config.gamma;
            break;
    }
}

void showCameraSettings() {
    std::cout << "\n=== CAMERA SETTINGS ===\n";
    std::cout << "1. Position: [" << g_config.camera.pos[0] << ", "
              << g_config.camera.pos[1] << ", " << g_config.camera.pos[2] << "]\n";
    std::cout << "2. Target: [" << g_config.camera.target[0] << ", "
              << g_config.camera.target[1] << ", " << g_config.camera.target[2] << "]\n";
    std::cout << "3. Field of view: " << g_config.camera.fov << " degrees\n";
    std::cout << "Choose parameter (1-3): ";

    int choice;
    std::cin >> choice;

    if(choice == 1) {
        std::cout << "X: ";
        std::cin >> g_config.camera.pos[0];
        std::cout << "Y: ";
        std::cin >> g_config.camera.pos[1];
        std::cout << "Z: ";
        std::cin >> g_config.camera.pos[2];
    }
    else if(choice == 2) {
        std::cout << "Target X: ";
        std::cin >> g_config.camera.target[0];
        std::cout << "Target Y: ";
        std::cin >> g_config.camera.target[1];
        std::cout << "Target Z: ";
        std::cin >> g_config.camera.target[2];
    }
    else if(choice == 3) {
        std::cout << "Field of view (20-120): ";
        std::cin >> g_config.camera.fov;
    }
}

void showMaterialSettings() {
    std::cout << "\n=== MATERIAL SETTINGS ===\n";
    std::cout << "1. White walls color (RGB): [" << g_config.materials.white[0] << ", "
              << g_config.materials.white[1] << ", " << g_config.materials.white[2] << "]\n";
    std::cout << "2. Red wall color: [" << g_config.materials.red[0] << ", "
              << g_config.materials.red[1] << ", " << g_config.materials.red[2] << "]\n";
    std::cout << "3. Green wall color: [" << g_config.materials.green[0] << ", "
              << g_config.materials.green[1] << ", " << g_config.materials.green[2] << "]\n";
    std::cout << "4. Mirror reflectivity: [" << g_config.materials.mirror[0] << ", "
              << g_config.materials.mirror[1] << ", " << g_config.materials.mirror[2] << "]\n";
    std::cout << "Choose material (1-4): ";

    int choice;
    std::cin >> choice;

    if(choice >= 1 && choice <= 4) {
        double* color = nullptr;
        std::string name;

        switch(choice) {
            case 1: color = g_config.materials.white; name = "white walls"; break;
            case 2: color = g_config.materials.red; name = "red wall"; break;
            case 3: color = g_config.materials.green; name = "green wall"; break;
            case 4: color = g_config.materials.mirror; name = "mirror"; break;
        }

        std::cout << "R for " << name << " (0-1): ";
        std::cin >> color[0];
        std::cout << "G (0-1): ";
        std::cin >> color[1];
        std::cout << "B (0-1): ";
        std::cin >> color[2];

        if (choice != 4) {
            std::cout << "Note: physicality check is per-channel: kd + ks <= 1 for each RGB.\n";
        }
    }
}

void showLightSettings() {
    std::cout << "\n=== LIGHTING SETTINGS ===\n";
    std::cout << "1. Light brightness: [" << g_config.materials.light[0] << ", "
              << g_config.materials.light[1] << ", " << g_config.materials.light[2] << "]\n";
    std::cout << "2. Light source size: " << g_config.materials.lightSize
              << " (0.1-0.9)\n";
    std::cout << "Choose parameter (1-2): ";

    int choice;
    std::cin >> choice;

    if(choice == 1) {
        std::cout << "R brightness: ";
        std::cin >> g_config.materials.light[0];
        std::cout << "G: ";
        std::cin >> g_config.materials.light[1];
        std::cout << "B: ";
        std::cin >> g_config.materials.light[2];
    }
    else if(choice == 2) {
        std::cout << "Light size (0.1-0.9): ";
        std::cin >> g_config.materials.lightSize;
        g_config.materials.lightSize = std::max(0.1, std::min(0.9, g_config.materials.lightSize));
    }
}

void toggleEffects() {
    std::cout << "\n=== TOGGLE EFFECTS ===\n";
    std::cout << "1. Russian Roulette: " << (g_config.enableRussianRoulette ? "ON" : "OFF") << "\n";
    std::cout << "2. Direct lighting: " << (g_config.showDirectLighting ? "ON" : "OFF") << "\n";
    std::cout << "3. Indirect lighting: " << (g_config.showIndirectLighting ? "ON" : "OFF") << "\n";
    std::cout << "4. Anti-aliasing: " << (g_config.enableAntiAliasing ? "ON" : "OFF") << "\n";
    std::cout << "Choose effect (1-4) to toggle: ";

    int choice;
    std::cin >> choice;

    switch(choice) {
        case 1: g_config.enableRussianRoulette = !g_config.enableRussianRoulette; break;
        case 2: g_config.showDirectLighting = !g_config.showDirectLighting; break;
        case 3: g_config.showIndirectLighting = !g_config.showIndirectLighting; break;
        case 4: g_config.enableAntiAliasing = !g_config.enableAntiAliasing; break;
    }

    std::cout << "Effect toggled!\n";
}

void applyPreset() {
    std::cout << "\n=== QUICK PRESETS ===\n";
    std::cout << "1. Fast render (noisy)\n";
    std::cout << "2. Quality render (slow)\n";
    std::cout << "3. Direct lighting only\n";
    std::cout << "4. Global illumination only\n";
    std::cout << "5. Mirror sphere (сфера в центре комнаты)\n";
    std::cout << "Choose preset (1-5): ";

    int choice;
    std::cin >> choice;

    switch(choice) {
        case 1: // Fast
            g_config.width = 400;
            g_config.height = 400;
            g_config.spp = 20;
            g_config.maxDepth = 3;
            break;
        case 2: // Quality
            g_config.width = 800;
            g_config.height = 800;
            g_config.spp = 200;
            g_config.maxDepth = 6;
            break;
        case 3: // Direct only
            g_config.showDirectLighting = true;
            g_config.showIndirectLighting = false;
            g_config.maxDepth = 1;
            break;
        case 4: // Global only
            g_config.showDirectLighting = false;
            g_config.showIndirectLighting = true;
            g_config.maxDepth = 5;
            break;
        case 5: // Mirror sphere
            for(int i = 0; i < 3; i++) {
                g_config.materials.white[i] = 0.8;
                g_config.materials.red[i] = 0.8;
                g_config.materials.green[i] = 0.2;
                g_config.materials.mirror[i] = 0.9;
            }
            g_config.materials.red[1] = 0.2;
            g_config.materials.red[2] = 0.2;
            g_config.materials.green[0] = 0.2;
            g_config.materials.green[2] = 0.2;
            break;
    }

    std::cout << "Preset applied!\n";
}

void showCurrentSettings() {
    std::cout << "\n=== CURRENT SETTINGS ===\n";
    std::cout << "Resolution: " << g_config.width << "x" << g_config.height << "\n";
    std::cout << "SPP: " << g_config.spp << "\n";
    std::cout << "Max depth: " << g_config.maxDepth << "\n";
    std::cout << "Camera: pos(" << g_config.camera.pos[0] << ","
              << g_config.camera.pos[1] << "," << g_config.camera.pos[2]
              << ") target(" << g_config.camera.target[0] << ","
              << g_config.camera.target[1] << "," << g_config.camera.target[2]
              << ") fov=" << g_config.camera.fov << "\n";
    std::cout << "Materials: white(" << g_config.materials.white[0] << ","
              << g_config.materials.white[1] << "," << g_config.materials.white[2]
              << ") red(" << g_config.materials.red[0] << ","
              << g_config.materials.red[1] << "," << g_config.materials.red[2]
              << ") green(" << g_config.materials.green[0] << ","
              << g_config.materials.green[1] << "," << g_config.materials.green[2]
              << ")\n";
    std::cout << "Mirror: (" << g_config.materials.mirror[0] << ","
              << g_config.materials.mirror[1] << "," << g_config.materials.mirror[2]
              << ")\n";
    std::cout << "Light: brightness(" << g_config.materials.light[0] << ","
              << g_config.materials.light[1] << "," << g_config.materials.light[2]
              << ") size=" << g_config.materials.lightSize << "\n";
    std::cout << "Effects: RR=" << (g_config.enableRussianRoulette ? "ON" : "OFF")
              << " direct=" << (g_config.showDirectLighting ? "ON" : "OFF")
              << " indirect=" << (g_config.showIndirectLighting ? "ON" : "OFF")
              << " AA=" << (g_config.enableAntiAliasing ? "ON" : "OFF") << "\n";
}

// ==================== RENDERING FUNCTIONS ====================

bool rayTriangleIntersect(const Ray& ray, const Triangle& tri, Hit& hit) {
    const double EPS = 1e-8;

    Vec3 e1 = tri.v1 - tri.v0;
    Vec3 e2 = tri.v2 - tri.v0;
    Vec3 h = cross(ray.direction, e2);

    double a = dot(e1, h);
    if (std::fabs(a) < EPS) return false;

    double f = 1.0 / a;
    Vec3 s = ray.origin - tri.v0;
    double u = f * dot(s, h);
    if (u < 0.0 || u > 1.0) return false;

    Vec3 q = cross(s, e1);
    double v = f * dot(ray.direction, q);
    if (v < 0.0 || u + v > 1.0) return false;

    double t = f * dot(e2, q);
    if (t < EPS) return false;

    if (t < hit.t) {
        hit.t = t;
        hit.point = ray.origin + ray.direction * t;
        hit.normal = tri.normal;
        hit.material = tri.material;
        hit.frontFace = dot(ray.direction, tri.normal) < 0;
        if (!hit.frontFace) {
            hit.normal = -hit.normal;
        }
        return true;
    }
    return false;
}

class Scene {
public:
    std::vector<Triangle> triangles;
    std::vector<Sphere> spheres;
    std::vector<Material> materials;


    std::vector<int> lightTriangles;

    Scene() {
        createScene();
    }

    void createScene() {
        // Create materials from global configuration
        materials.clear();
        materials.push_back(Material(
                Vec3(g_config.materials.white[0], g_config.materials.white[1], g_config.materials.white[2])
        ));
        materials.push_back(Material(
                Vec3(g_config.materials.red[0], g_config.materials.red[1], g_config.materials.red[2])
        ));
        materials.push_back(Material(
                Vec3(g_config.materials.green[0], g_config.materials.green[1], g_config.materials.green[2])
        ));
        materials.push_back(Material(
                Vec3(0),
                Vec3(g_config.materials.mirror[0], g_config.materials.mirror[1], g_config.materials.mirror[2])
        ));
        materials.push_back(Material(
                Vec3(0), Vec3(0),
                Vec3(g_config.materials.light[0], g_config.materials.light[1], g_config.materials.light[2])
        ));

        // Clear old geometry
        triangles.clear();
        spheres.clear();

        // Cornell Box (1x1x1 cube)
        // Floor
        triangles.push_back(Triangle(Vec3(0,0,0), Vec3(1,0,0), Vec3(1,0,1), &materials[0]));
        triangles.push_back(Triangle(Vec3(0,0,0), Vec3(1,0,1), Vec3(0,0,1), &materials[0]));

        // Ceiling
        triangles.push_back(Triangle(Vec3(0,1,0), Vec3(0,1,1), Vec3(1,1,1), &materials[0]));
        triangles.push_back(Triangle(Vec3(0,1,0), Vec3(1,1,1), Vec3(1,1,0), &materials[0]));

        // Back wall
        triangles.push_back(Triangle(Vec3(0,0,1), Vec3(1,0,1), Vec3(1,1,1), &materials[0]));
        triangles.push_back(Triangle(Vec3(0,0,1), Vec3(1,1,1), Vec3(0,1,1), &materials[0]));

        // Left wall (green)
        triangles.push_back(Triangle(Vec3(0,0,0), Vec3(0,0,1), Vec3(0,1,1), &materials[2]));
        triangles.push_back(Triangle(Vec3(0,0,0), Vec3(0,1,1), Vec3(0,1,0), &materials[2]));

        // Right wall (red)
        triangles.push_back(Triangle(Vec3(1,0,0), Vec3(1,1,0), Vec3(1,1,1), &materials[1]));
        triangles.push_back(Triangle(Vec3(1,0,0), Vec3(1,1,1), Vec3(1,0,1), &materials[1]));

        // Light source (two triangles)
        double ls = g_config.materials.lightSize;
        double start = 0.5 - ls/2.0;
        double end = 0.5 + ls/2.0;
        triangles.push_back(Triangle(Vec3(start,0.999,end), Vec3(end,0.999,end),
                                     Vec3(end,0.999,start), &materials[4]));
        triangles.push_back(Triangle(Vec3(start,0.999,end), Vec3(end,0.999,start),
                                     Vec3(start,0.999,start), &materials[4]));

        Vec3 sphereCenter(0.5, 0.3, 0.5);  // Центр сферы
        double sphereRadius = 0.15;        // Радиус сферы
        spheres.push_back(Sphere(sphereCenter, sphereRadius, &materials[3]));

        lightTriangles.clear();
        for (int i = 0; i < (int)triangles.size(); i++) {
            if (triangles[i].material && triangles[i].material->isLight()) {
                lightTriangles.push_back(i);
            }
        }
    }

    bool intersect(const Ray& ray, Hit& hit) const {
        bool intersected = false;

        // Проверяем пересечение с треугольниками
        for (const Triangle& tri : triangles) {
            if (rayTriangleIntersect(ray, tri, hit)) {
                intersected = true;
            }
        }

        // Проверяем пересечение со сферами
        for (const Sphere& sphere : spheres) {
            if (sphere.intersect(ray, hit)) {
                intersected = true;
            }
        }

        return intersected;
    }
};

Vec3 sampleCosineHemisphere(const Vec3& normal, std::mt19937& rng) {
    std::uniform_real_distribution<double> dist(0, 1);

    double u1 = dist(rng);
    double u2 = dist(rng);

    double r = std::sqrt(u1);
    double theta = 2 * PI * u2;

    double x = r * std::cos(theta);
    double y = r * std::sin(theta);
    double z = std::sqrt(std::max(0.0, 1.0 - x*x - y*y));

    Vec3 up = std::fabs(normal.z) < 0.99 ? Vec3(0,0,1) : Vec3(1,0,0);
    Vec3 tangent = normalize(cross(up, normal));
    Vec3 bitangent = cross(normal, tangent);

    return tangent * x + bitangent * y + normal * z;
}

// NEE direct lighting from AREA LIGHT
Vec3 directLighting(const Vec3& point, const Vec3& normal, const Material* material,
                    const Scene& scene, std::mt19937& rng) {

    if (!g_config.showDirectLighting) return Vec3(0);
    if (scene.lightTriangles.empty()) return Vec3(0);

    // 1) Choose one light triangle uniformly
    std::uniform_int_distribution<int> pick(0, (int)scene.lightTriangles.size() - 1);
    int triId = scene.lightTriangles[pick(rng)];
    const Triangle& lightTri = scene.triangles[triId];

    // 2) Sample point on triangle (uniform by area)
    Vec3 lightPoint = lightTri.randomPoint(rng);
    Vec3 lightNormal = lightTri.normal;

    Vec3 toLight = lightPoint - point;
    double dist2 = dot(toLight, toLight);
    double dist = std::sqrt(dist2);
    if (dist < 1e-6) return Vec3(0);

    Vec3 wi = toLight / dist;

    double cosSurf = std::max(0.0, dot(normal, wi));
    if (cosSurf <= 1e-6) return Vec3(0);

    // Light emits only to hemisphere of its normal
    double cosLight = std::max(0.0, dot(lightNormal, -wi));
    if (cosLight <= 1e-6) return Vec3(0);

    // 3) Shadow ray
    Ray shadowRay(point + normal * 1e-4, wi);
    Hit shadowHit;
    if (scene.intersect(shadowRay, shadowHit)) {
        if (shadowHit.t < dist - 1e-3) return Vec3(0);
    }

    // 4) Lambert BRDF: f = kd/pi
    Vec3 brdf = material->kd * (1.0 / PI);

    // 5) PDF in solid angle:
    // p_select = 1 / Nlights
    // p_area   = 1 / area
    // p_omega  = p_select * p_area * dist^2 / cosLight
    double N = (double)scene.lightTriangles.size();
    double pdfSelect = 1.0 / std::max(N, 1.0);
    double pdfArea = 1.0 / std::max(lightTri.area, 1e-12);
    double pdfOmega = pdfSelect * pdfArea * dist2 / std::max(cosLight, 1e-12);

    Vec3 Le = lightTri.material->emission;

    return Le.mul(brdf) * (cosSurf / std::max(pdfOmega, 1e-12));
}

Vec3 tracePath(const Ray& ray, const Scene& scene, std::mt19937& rng, int depth = 0) {
    if (depth >= g_config.maxDepth) return Vec3(0);

    Hit hit;
    if (!scene.intersect(ray, hit)) {
        return Vec3(0);
    }

    // if hit? return light
    if (hit.material->isLight()) {
        return hit.material->emission;
    }

    Vec3 color(0);

    // direct light (NEE)
    if (g_config.showDirectLighting) {
        color = color + directLighting(hit.point, hit.normal, hit.material, scene, rng);
    }

    if (!g_config.showIndirectLighting) {
        return color;
    }

    // Russian Roulette (single, unbiased)
    double rrScale = 1.0;
    if (g_config.enableRussianRoulette && depth >= g_config.rrStartDepth) {
        std::uniform_real_distribution<double> dist(0, 1);
        double p = std::clamp(g_config.rrProbability, 0.05, 0.95);
        if (dist(rng) > p) {
            return color; // terminate
        }
        rrScale = 1.0 / p; // compensation
    }

    double kd_strength = std::max({hit.material->kd.x, hit.material->kd.y, hit.material->kd.z});
    double ks_strength = std::max({hit.material->ks.x, hit.material->ks.y, hit.material->ks.z});

    if (kd_strength + ks_strength < 1e-6) {
        return color;
    }

    std::uniform_real_distribution<double> dist(0, 1);

    Vec3 newDir;
    Vec3 weight;
    double sum = kd_strength + ks_strength;
    double specular_prob = ks_strength / sum;
    double diffuse_prob  = kd_strength / sum;

    if (ks_strength > 0 && dist(rng) < specular_prob) {
        // mirror
        newDir = normalize(ray.direction - hit.normal * 2.0 * dot(ray.direction, hit.normal));

        // weight = ks / P(event)
        weight = hit.material->ks * (1.0 / std::max(specular_prob, 1e-6));

    } else {
        // lambert
        newDir = sampleCosineHemisphere(hit.normal, rng);
        double cosTheta = std::max(0.0, dot(hit.normal, newDir));

        double pdf = cosineHemispherePDF(cosTheta); // cos/pi
        Vec3 brdf = hit.material->kd * (1.0 / PI);

        // throughput factor: brdf * cos / pdf = kd (но оставляем в общем виде)
        weight = brdf * (cosTheta / std::max(pdf, 1e-6)) * (1.0 / std::max(diffuse_prob, 1e-6));
    }

    // Russian Roulette to weight
    weight = weight * rrScale;

    Vec3 indirect = tracePath(Ray(hit.point + hit.normal * 1e-4, newDir),
                              scene, rng, depth + 1);

    return color + weight.mul(indirect);
}

class Camera {
public:
    Vec3 position;
    Vec3 lookAt;
    Vec3 up;
    double fov;

    Camera() {
        updateFromConfig();
    }

    void updateFromConfig() {
        position = Vec3(g_config.camera.pos[0], g_config.camera.pos[1], g_config.camera.pos[2]);
        lookAt = Vec3(g_config.camera.target[0], g_config.camera.target[1], g_config.camera.target[2]);
        fov = g_config.camera.fov;
        up = Vec3(0, 1, 0);
    }

    Ray getRay(double px, double py, int width, int height, std::mt19937& rng) const {
        double x = px;
        double y = py;

        if (g_config.enableAntiAliasing) {
            std::uniform_real_distribution<double> dist(-0.5, 0.5);
            x += dist(rng);
            y += dist(rng);
        }

        double aspect = (double)width / height;
        double screenX = (2.0 * x / width - 1.0) * aspect;
        double screenY = 1.0 - 2.0 * y / height;

        Vec3 forward = normalize(lookAt - position);
        Vec3 right = normalize(cross(forward, up));
        Vec3 cameraUp = cross(right, forward);

        double scale = std::tan(fov * 0.5 * PI / 180.0);
        Vec3 direction = normalize(forward + right * screenX * scale +
                                   cameraUp * screenY * scale);

        return Ray(position, direction);
    }
};

void toneMapping(std::vector<Vec3>& image, double gamma = 2.2) {
    for (Vec3& p : image) {
        p.x = std::max(0.0, p.x);
        p.y = std::max(0.0, p.y);
        p.z = std::max(0.0, p.z);

        // clamp
        p.x = std::min(1.0, p.x);
        p.y = std::min(1.0, p.y);
        p.z = std::min(1.0, p.z);

        // gamma
        p.x = std::pow(p.x, 1.0 / gamma);
        p.y = std::pow(p.y, 1.0 / gamma);
        p.z = std::pow(p.z, 1.0 / gamma);
    }
}

void savePPM(const std::string& filename, const std::vector<Vec3>& image,
             int width, int height) {
    std::ofstream file(filename);
    file << "P3\n" << width << " " << height << "\n255\n";

    for (const Vec3& pixel : image) {
        int r = static_cast<int>(std::clamp(pixel.x, 0.0, 1.0) * 255.0);
        int g = static_cast<int>(std::clamp(pixel.y, 0.0, 1.0) * 255.0);
        int b = static_cast<int>(std::clamp(pixel.z, 0.0, 1.0) * 255.0);

        file << r << " " << g << " " << b << "\n";
    }

    file.close();
    std::cout << "\nImage saved to " << filename << std::endl;
}

void renderScene() {
    std::cout << "\n=== RENDERING START ===\n";

    // Update scene and camera
    Scene scene;
    Camera camera;
    camera.updateFromConfig();

    // Initialize random number generator
    std::random_device rd;
    std::mt19937 rng(rd());

    // Image buffer
    std::vector<Vec3> image(g_config.width * g_config.height, Vec3(0));

    std::cout << "Resolution: " << g_config.width << "x" << g_config.height << "\n";
    std::cout << "SPP: " << g_config.spp << "\n";
    std::cout << "Max path depth: " << g_config.maxDepth << "\n";
    std::cout << "Rendering...\n";

    // Main rendering loop
    for (int y = 0; y < g_config.height; y++) {
        if (y % 10 == 0) {
            std::cerr << "\rProgress: " << (100 * y / g_config.height) << "%" << std::flush;
        }

        for (int x = 0; x < g_config.width; x++) {
            Vec3 color(0);

            for (int s = 0; s < g_config.spp; s++) {
                Ray ray = camera.getRay(x, y, g_config.width, g_config.height, rng);
                color = color + tracePath(ray, scene, rng, 0);
            }

            color = color * (1.0 / g_config.spp);
            image[y * g_config.width + x] = color;
        }
    }

    std::cerr << "\rRendering completed!      \n";

    // Tone mapping
    toneMapping(image, g_config.gamma);

    // Save image
    std::string filename = "render_" + std::to_string(g_config.spp) + "spp.ppm";
    savePPM(filename, image, g_config.width, g_config.height);

    // Information
    std::cout << "\n=== INFORMATION ===\n";
    std::cout << "Pixels: " << g_config.width * g_config.height << "\n";
    std::cout << "Total rays: " << (long long)g_config.width * (long long)g_config.height * (long long)g_config.spp << "\n";
    std::cout << "File: " << filename << "\n";
}

// ==================== MAIN FUNCTION ====================

int main() {
    std::cout << "=== C++ Path Tracer ===\n";
    std::cout << "Interactive renderer for a Cornell Box scene\n";
    std::cout << "Scene: Cornell Box with a mirror sphere and area light\n";

    bool running = true;

    while (running) {
        printMenu();

        int choice;
        std::cin >> choice;

        switch (choice) {
            case 1:
                showRenderSettings();
                break;
            case 2:
                showCameraSettings();
                break;
            case 3:
                showMaterialSettings();
                break;
            case 4:
                showLightSettings();
                break;
            case 5:
                toggleEffects();
                break;
            case 6:
                applyPreset();
                break;
            case 7:
                showCurrentSettings();
                break;
            case 8:
                renderScene();
                break;
            case 0:
                running = false;
                std::cout << "Exit...\n";
                break;
            default:
                std::cout << "Wrong choice!\n";
                break;
        }
    }

    return 0;
}