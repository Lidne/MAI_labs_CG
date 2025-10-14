#include <scene.hpp>

void Scene::addFigure(std::shared_ptr<Figure> figure) {
    figures.push_back(figure);
}

uint32_t Scene::getTotalIndexCount() const {
    uint32_t total = 0;
    for (const auto& figure : figures) {
        total += figure->getIndexCount();
    }
    return total;
}

uint32_t Scene::getTotalVertexCount() const {
    uint32_t total = 0;
    for (const auto& figure : figures) {
        total += figure->getVertexCount();
    }
    return total;
}