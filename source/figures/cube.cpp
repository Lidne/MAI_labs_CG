#include <cstdint>
#include <figures/cube.hpp>
#include <vector>

void figures::Cube::createCube(std::vector<Vertex>& vertices, std::vector<uint32_t>& indices) {
    // 8 вершин куба (центр в начале координат, длина ребра 1)
    vertices = {
        {{-0.5f, -0.5f, -0.5f}},  // 0
        {{0.5f, -0.5f, -0.5f}},   // 1
        {{0.5f, 0.5f, -0.5f}},    // 2
        {{-0.5f, 0.5f, -0.5f}},   // 3
        {{-0.5f, -0.5f, 0.5f}},   // 4
        {{0.5f, -0.5f, 0.5f}},    // 5
        {{0.5f, 0.5f, 0.5f}},     // 6
        {{-0.5f, 0.5f, 0.5f}}     // 7
    };

    // Индексы для 12 треугольников (2 на каждую грань)
    indices = {
        0, 1, 2, 2, 3, 0,  // передняя грань
        6, 5, 4, 4, 7, 6,  // задняя грань
        2, 6, 7, 2, 7, 3,  // нижняя грань
        0, 4, 1, 1, 4, 5,  // верхняя грань
        0, 3, 7, 0, 7, 4,  // левая грань
        1, 5, 6, 1, 6, 2   // правая грань
    };
}

uint32_t figures::Cube::getVertexCount() const {
    return 8;
}

uint32_t figures::Cube::getIndexCount() const {
    return 36;
}

figures::Cube::Cube() {
    pos = Vertex{{0.0f, 0.0f, 0.0f}};
}

figures::Cube::Cube(Vertex pos) {
    pos = pos;
}

figures::Cube::Cube(Vertex& pos) {
    pos = pos;
}