cmake_minimum_required(VERSION 3.30)
project(factory_planner)

set(CMAKE_CXX_STANDARD 17)

include_directories(${CMAKE_SOURCE_DIR}/third_party)

add_executable(factory_planner src/main.cpp
        src/core/Node.h
        src/common/NodeType.h
        src/core/Connection.h
        src/core/FactoryGraph.h
        src/core/FactoryGraph.cpp
        src/utils/JsonLoader.cpp
        src/utils/JsonLoader.h
        src/core/Recipe.h
        src/core/Resource.h
        src/utils/GameDataLoader.cpp
        src/utils/GameDataLoader.h)

