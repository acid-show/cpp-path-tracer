# C++ Path Tracer

A minimal C++ path tracer that renders a Cornell Box scene with a mirror sphere, diffuse walls and an area light source.

The project demonstrates basic physically based rendering techniques: ray tracing, path tracing, Lambertian reflection, ideal mirror reflection, direct lighting, Russian Roulette path termination, anti-aliasing and gamma correction.

## Features

* Cornell Box scene built from triangles
* Mirror sphere as an analytic primitive
* Area light source
* Triangle intersection using the Möller–Trumbore algorithm
* Sphere intersection using an analytic quadratic solution
* Diffuse Lambertian materials
* Ideal mirror reflection
* Direct lighting with Next Event Estimation
* Indirect lighting with recursive path tracing
* Russian Roulette path termination
* Cosine-weighted hemisphere sampling
* Anti-aliasing through random pixel sampling
* Gamma correction
* PPM image output

## Preview

![Render preview](screenshots/render-preview.png)

## Tech Stack

* C++
* CMake
* Standard Library only

## Build

```bash
mkdir build
cd build
cmake ..
cmake --build .
```

## Run

From the build directory:

```bash
./path_tracer
```

On Windows:

```bash
path_tracer.exe
```

The program opens an interactive menu where rendering parameters can be changed before starting the render.

## Output

The rendered image is saved as a `.ppm` file, for example:

```text
render_100spp.ppm
```

The number in the filename depends on the selected samples per pixel value.

## Example Configuration

The repository includes `config.example.txt` with example render settings.
The current version uses an interactive menu; automatic config loading can be added later.

## Implementation Notes

The renderer uses path tracing to approximate global illumination. Rays are generated from a pinhole camera and traced recursively through the scene. Diffuse surfaces use cosine-weighted hemisphere sampling, while the mirror sphere reflects rays in the ideal specular direction.

Direct lighting is estimated by sampling a point on the area light source and casting a shadow ray. Russian Roulette is used after several bounces to terminate paths while keeping the estimator unbiased.



