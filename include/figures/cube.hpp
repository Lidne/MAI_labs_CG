#pragma once

#include <base.hpp>
#include <cstdint>
#include <vector>

namespace figures {

class Cube : public Figure {
    Vertex pos;

   public:
    Cube();
    Cube(Vertex pos);
    Cube(Vertex& pos);
    static void createCube(std::vector<Vertex>& vertices, std::vector<uint32_t>& indices);

    uint32_t getVertexCount() const override;
    uint32_t getIndexCount() const override;
};

}  // namespace figures