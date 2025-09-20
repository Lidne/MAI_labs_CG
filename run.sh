#!/bin/bash

# Скрипт для запуска Vulkan приложения под Hyprland
cd "$(dirname "$0")"

echo "Запуск Vulkan приложения с белым экраном под Hyprland..."

# Устанавливаем переменные окружения для Wayland
export WAYLAND_DISPLAY=wayland-1
export XDG_SESSION_TYPE=wayland
export QT_QPA_PLATFORM=wayland
export GDK_BACKEND=wayland

# Отключаем предупреждения libdecor
export LIBDECOR_FORCE_CSD=1

# Проверяем существование исполняемого файла
if [ ! -f "./build/MyVulkanApp" ]; then
    echo "Ошибка: исполняемый файл не найден. Сначала соберите проект:"
    echo "mkdir -p build && cd build && cmake .. && make"
    exit 1
fi

echo "Нажмите ESC или закройте окно для выхода."
./build/MyVulkanApp
