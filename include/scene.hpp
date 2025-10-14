#pragma once

#include <base.hpp>
#include <memory>
#include <vector>

class Scene {
   public:
    void addFigure(std::shared_ptr<Figure> figure);
    uint32_t getTotalIndexCount() const;
    uint32_t getTotalVertexCount() const;

   private:
    std::vector<std::shared_ptr<Figure>> figures;
};
