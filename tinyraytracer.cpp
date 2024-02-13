#include <limits>
#include <cmath>
#include <iostream>
#include <string>
#include <fstream>
#include <vector>
#include "geometry.h"
#include "raylib.h"

const int width = 1024;
const int height = 768;
const int fov = 3.14159265 / 2;

struct Light {
    Light(const Vec3f& p, const float& i) : position(p), intensity(i) {}
    Vec3f position;
    float intensity;
};

struct MMaterial {
    MMaterial(const float& r, const Vec4f& a, const Vec3f& color, const float& spec) : refractive_index(r), albedo(a), diffuse_color(color), specular_exponent(spec) {}
    MMaterial() : refractive_index(1), albedo(1, 0, 0, 0), diffuse_color(), specular_exponent() {}
    float refractive_index;
    Vec4f albedo;
    Vec3f diffuse_color;
    float specular_exponent;
};

struct Sphere {
    Vec3f center;
    float radius;
    MMaterial material;

    Sphere(const Vec3f& c, const float& r, const MMaterial& m) : center(c), radius(r), material(m) {}

    bool ray_intersect(const Vec3f& orig, const Vec3f& dir, float& t0) const {
        Vec3f L = center - orig; // Vector orig to center
        float tca = L * dir; // Projection center to ray
        float d2 = L * L - tca * tca; // Squared distance center to tca
        float radius2 = radius * radius;
        if (d2 > radius2) return false; // If squared distance > squared radius then no intersect
        float thc = sqrtf(radius2 - d2); // tca to intersection distance
        t0 = tca - thc;
        float t1 = tca + thc;
        if (t0 < 0) t0 = t1;
        if (t0 < 0) return false;
        return true;
    }
};

Vec3f reflect(const Vec3f& I, const Vec3f& N) {
    return I - N * 2.f * (I * N);
}

Vec3f refract(const Vec3f& I, const Vec3f& N, const float& refractive_index) { // Snell's law
    float cosi = -std::max(-1.f, std::min(1.f, I * N));
    float etai = 1, etat = refractive_index;
    Vec3f n = N;
    if (cosi < 0) { // if the ray is inside the object, swap the indices and invert the normal to get the correct result
        cosi = -cosi;
        std::swap(etai, etat); n = -N;
    }
    float eta = etai / etat;
    float k = 1 - eta * eta * (1 - cosi * cosi);
    return k < 0 ? Vec3f(0, 0, 0) : I * eta + n * (eta * cosi - sqrtf(k));
}

bool scene_intersect(const Vec3f& orig, const Vec3f& dir, const std::vector<Sphere>& spheres, Vec3f& hit, Vec3f& N, MMaterial& material) {
    float spheres_dist = std::numeric_limits<float>::max();
    for (size_t i = 0; i < spheres.size(); i++) {
        float dist_i;
        if (spheres[i].ray_intersect(orig, dir, dist_i) && dist_i < spheres_dist) {
            spheres_dist = dist_i;
            hit = orig + dir * dist_i;
            N = (hit - spheres[i].center).normalize();
            material = spheres[i].material;
        }
    }

    float checkerboard_dist = std::numeric_limits<float>::max();
    if (fabs(dir.y) > 1e-3) {
        float d = -(orig.y + 4) / dir.y; // the checkerboard plane has equation y = -4
        Vec3f pt = orig + dir * d;
        if (d > 0 && fabs(pt.x) < 10 && pt.z<-10 && pt.z>-30 && d < spheres_dist) {
            checkerboard_dist = d;
            hit = pt;
            N = Vec3f(0, 1, 0);
            material.diffuse_color = (int(.5 * hit.x + 1000) + int(.5 * hit.z)) & 1 ? Vec3f(1, 1, 1) : Vec3f(1, .7, .3);
            material.diffuse_color = material.diffuse_color * .3;
        }
    }
    return std::min(spheres_dist, checkerboard_dist) < 1000;
}

Vec3f cast_ray(const Vec3f& orig, const Vec3f& dir, const std::vector<Sphere>& spheres, const std::vector<Light>& lights, size_t depth, int maxDepth) {
    Vec3f point, N;
    MMaterial material;

    if (depth > maxDepth || !scene_intersect(orig, dir, spheres, point, N, material)) {
        return Vec3f(0.2, 0.7, 0.8); // background color
    }

    Vec3f reflect_dir = reflect(dir, N).normalize();
    Vec3f refract_dir = refract(dir, N, material.refractive_index).normalize();
    Vec3f reflect_orig = reflect_dir * N < 0 ? point - N * 1e-3 : point + N * 1e-3; // offset the original point to avoid occlusion by the object itself
    Vec3f refract_orig = refract_dir * N < 0 ? point - N * 1e-3 : point + N * 1e-3;
    Vec3f reflect_color = cast_ray(reflect_orig, reflect_dir, spheres, lights, depth + 1, maxDepth);
    Vec3f refract_color = cast_ray(refract_orig, refract_dir, spheres, lights, depth + 1, maxDepth);

    float diffuse_light_intensity = 0, specular_light_intensity = 0;
    for (size_t i = 0; i < lights.size(); i++) {
        Vec3f light_dir = (lights[i].position - point).normalize();
        float light_distance = (lights[i].position - point).norm();

        Vec3f shadow_orig = light_dir * N < 0 ? point - N * 1e-3 : point + N * 1e-3; // checking if the point lies in the shadow of the lights[i]
        Vec3f shadow_pt, shadow_N;
        MMaterial tmpmaterial;
        if (scene_intersect(shadow_orig, light_dir, spheres, shadow_pt, shadow_N, tmpmaterial) && (shadow_pt - shadow_orig).norm() < light_distance)
            continue;

        diffuse_light_intensity += lights[i].intensity * std::max(0.f, light_dir * N);
        specular_light_intensity += powf(std::max(0.f, -reflect(-light_dir, N) * dir), material.specular_exponent) * lights[i].intensity;
    }
    return material.diffuse_color * diffuse_light_intensity * material.albedo[0] + Vec3f(1., 1., 1.) * specular_light_intensity * material.albedo[1] + reflect_color * material.albedo[2] + refract_color * material.albedo[3];
}

void render(const std::vector<Sphere>& spheres, const std::vector<Light>& lights, int scale, int maxDepth) {
    std::vector<Vec3f> framebuffer((width * height) / scale);

    // #pragma omp parallel for
    for (size_t j = 0; j < height / scale; j++) {
        for (size_t i = 0; i < width / scale; i++) {
            float x = (2 * (i + 0.5) / (width / scale) - 1) * tan(fov / 2.) * (width / scale) / (height / scale);
            float y = -(2 * (j + 0.5) / (height / scale) - 1) * tan(fov / 2.);
            Vec3f dir = Vec3f(x, y, -1).normalize();
            framebuffer[i + j * width] = cast_ray(Vec3f(0, 0, 0), dir, spheres, lights, 0, maxDepth);
        }
    }

    // Simple rectangle drawing
    /*for (int i = 0; i < (height * width / scale); ++i) {
        Vec3f& c = framebuffer[i];
        float max = std::max(c[0], std::max(c[1], c[2]));
        if (max > 1) c = c * (1. / max);
        DrawRectangle((i % width) * scale, (i / width) * scale, scale, scale, { (unsigned char)(255 * std::max(0.f, std::min(1.f, c[0]))), (unsigned char)(255 * std::max(0.f, std::min(1.f, c[1]))), (unsigned char)(255 * std::max(0.f, std::min(1.f, c[2]))), 255 });
    }*/

    // Contiguous rectangle drawing
    for (int row = 0; row < height / scale; row++) {
        int startIdx = row * width; // Start index of the current row in the framebuffer
        Vec3f currentColor = framebuffer[startIdx];

        // Loop through each pixel in the row
        int oldStartIdx = startIdx;
        for (int col = 0; col <= width / scale; col++) {
            int idx = oldStartIdx + col; // Index of the current pixel in the framebuffer

            // If the color changes or we reach the end of the row
            bool endRow = col == (width / scale) - 1;
            bool colorChange = framebuffer[idx][0] != currentColor[0] || framebuffer[idx][1] != currentColor[1] || framebuffer[idx][2] != currentColor[2];
            if (endRow || colorChange) {
                // Color correction
                Vec3f& c = currentColor;
                float max = std::max(c[0], std::max(c[1], c[2]));
                if (max > 1) c = c * (1. / max);
                
                // Calculate the rectangle position and dimensions
                int x = (startIdx % width) * scale;
                int y = (startIdx / width) * scale;
                int rectWidth = (col - (startIdx % (width / scale))) * scale;
                int rectHeight = scale;

                // Draw a rectangle with the current color
                DrawRectangle(x, y, rectWidth, rectHeight, { (unsigned char)(255 * currentColor[0]), (unsigned char)(255 * currentColor[1]), (unsigned char)(255 * currentColor[2]), 255 });

                // Update startIdx and currentColor for the next run
                if (colorChange) { 
                    startIdx = idx; 
                    currentColor = framebuffer[idx];
                }
            }
        }
    }
}

int main() {
    ///// INIT /////
    SetConfigFlags(FLAG_VSYNC_HINT);
    InitWindow(width, height, "TINY_RAY_TRACER");

    MMaterial      ivory(1.0, Vec4f(0.6, 0.3, 0.1, 0.0), Vec3f(0.4, 0.4, 0.3), 50.);
    MMaterial      glass(1.5, Vec4f(0.0, 0.5, 0.1, 0.8), Vec3f(0.6, 0.7, 0.8), 125.);
    MMaterial red_rubber(1.0, Vec4f(0.9, 0.1, 0.0, 0.0), Vec3f(0.3, 0.1, 0.1), 10.);
    MMaterial     mirror(1.0, Vec4f(0.0, 10.0, 0.8, 0.0), Vec3f(1.0, 1.0, 1.0), 1425.);

    std::vector<Sphere> spheres;
    spheres.push_back(Sphere(Vec3f(-3, 0, -16), 2, ivory));
    spheres.push_back(Sphere(Vec3f(-1.0, -1.5, -12), 2, glass));
    spheres.push_back(Sphere(Vec3f(1.5, -0.5, -18), 3, red_rubber));
    spheres.push_back(Sphere(Vec3f(7, 5, -18), 4, mirror));

    std::vector<Light>  lights;
    lights.push_back(Light(Vec3f(-20, 20, 20), 1.5));
    lights.push_back(Light(Vec3f(30, 50, -25), 1.8));
    lights.push_back(Light(Vec3f(30, 20, 30), 1.7));

    int scale = 8;     // 8
    int maxDepth = 4;  // 4
    
    int angle = 0;

    ///// LOOP /////
    SetTargetFPS(60);
    while (!WindowShouldClose())
    {
        ///// UPDATE /////
        angle = (angle + 4) % 360;
        spheres[0].center[0] = cos(angle * DEG2RAD) * 8;
        spheres[0].center[2] = sin(angle * DEG2RAD) * 8 - 16;
        // spheres[0].center[0] = GetMouseX() / 64 - 8;
        // spheres[0].center[2] = GetMouseY() / 48 - 24;
        if (scale > 1 && IsKeyPressed(KEY_LEFT)) { scale /= 2; }
        else if (scale < 16 && IsKeyPressed(KEY_RIGHT)) { scale *= 2; }
        if (maxDepth > 1 && IsKeyPressed(KEY_DOWN)) { maxDepth -= 1; }
        else if (maxDepth < 4 && IsKeyPressed(KEY_UP)) { maxDepth += 1; }

        ///// DRAW /////
        BeginDrawing();
        ClearBackground(BLACK);

        render(spheres, lights, scale, maxDepth);

        // DrawRectangle(0, 0, 90, 80, BLACK);
        // DrawFPS(10, 10);
        // DrawText(std::to_string(scale).c_str(), 10, 30, 20, GREEN);
        // DrawText(std::to_string(maxDepth).c_str(), 10, 50, 20, GREEN);
        EndDrawing();
    }

    ///// SHUT /////
    CloseWindow();
    return 0;
}