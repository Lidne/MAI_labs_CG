#pragma once

#include <cstdint>

typedef struct {
    float m[4][4];
} Matrix;

typedef struct {
    float x, y, z;
} Vector;

typedef struct {
    Vector position;
    Vector color;
    Vector barycentric;
} Vertex;

class Figure {
   public:
    virtual ~Figure() = default;
    virtual uint32_t getVertexCount() const = 0;
    virtual uint32_t getIndexCount() const = 0;
};