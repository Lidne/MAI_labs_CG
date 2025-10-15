#include <cstdint>
#include <figures/cube.hpp>
#include <vector>

void figures::Cube::createCube(std::vector<Vertex>& vertices, std::vector<uint32_t>& indices) {
    const Vector base_positions[8] = {
        {-0.5f, -0.5f, -0.5f},  // 0
        { 0.5f, -0.5f, -0.5f},  // 1
        { 0.5f,  0.5f, -0.5f},  // 2
        {-0.5f,  0.5f, -0.5f},  // 3
        {-0.5f, -0.5f,  0.5f},  // 4
        { 0.5f, -0.5f,  0.5f},  // 5
        { 0.5f,  0.5f,  0.5f},  // 6
        {-0.5f,  0.5f,  0.5f},  // 7
    };

    const uint32_t tri_indices[36] = {
        0, 1, 2, 2, 3, 0,  // передняя
        6, 5, 4, 4, 7, 6,  // задняя
        2, 6, 7, 2, 7, 3,  // нижняя
        0, 4, 1, 1, 4, 5,  // верхняя
        0, 3, 7, 0, 7, 4,  // левая
        1, 5, 6, 1, 6, 2   // правая
    };

    vertices.clear();
    indices.clear();
    vertices.reserve(36);
    indices.reserve(36);

    const Vector bc_a{1.0f, 0.0f, 0.0f};
    const Vector bc_b{0.0f, 1.0f, 0.0f};
    const Vector bc_c{0.0f, 0.0f, 1.0f};

    for (size_t t = 0; t < 36; t += 3) {
        const uint32_t i0 = tri_indices[t + 0];
        const uint32_t i1 = tri_indices[t + 1];
        const uint32_t i2 = tri_indices[t + 2];

        const Vector col0{(base_positions[i0].x + 0.5f), (base_positions[i0].y + 0.5f), (base_positions[i0].z + 0.5f)};
        const Vector col1{(base_positions[i1].x + 0.5f), (base_positions[i1].y + 0.5f), (base_positions[i1].z + 0.5f)};
        const Vector col2{(base_positions[i2].x + 0.5f), (base_positions[i2].y + 0.5f), (base_positions[i2].z + 0.5f)};

        const uint32_t base = static_cast<uint32_t>(vertices.size());

        vertices.push_back(Vertex{base_positions[i0], col0, bc_a});
        vertices.push_back(Vertex{base_positions[i1], col1, bc_b});
        vertices.push_back(Vertex{base_positions[i2], col2, bc_c});

        indices.push_back(base + 0);
        indices.push_back(base + 1);
        indices.push_back(base + 2);
    }
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