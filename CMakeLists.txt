# The following five lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)

# Agregar SOLO el componente sensor_reader de la nueva arquitectura
# (otros componentes en components_new/ son solo headers por ahora)
set(EXTRA_COMPONENT_DIRS "components_new/sensor_reader")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(iot-soc-smart-irrigation)
