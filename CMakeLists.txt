cmake_minimum_required(VERSION 3.15)
project(MyVulkanApp LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Поиск Vulkan
find_package(Vulkan REQUIRED)

# Поиск GLFW
find_package(PkgConfig REQUIRED)
pkg_check_modules(GLFW REQUIRED glfw3)

# Определяем исполняемый файл
add_executable(MyVulkanApp main.cpp)

# Включаем заголовочные файлы Vulkan и GLFW
target_include_directories(MyVulkanApp PRIVATE ${Vulkan_INCLUDE_DIRS} ${GLFW_INCLUDE_DIRS})

# Линкуем библиотеки Vulkan и GLFW
target_link_libraries(MyVulkanApp PRIVATE ${Vulkan_LIBRARIES} ${GLFW_LIBRARIES})
