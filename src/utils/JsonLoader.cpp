//
// Created by hagoel on 8/1/25.
//
#include "JsonLoader.h"

#include <fstream>
#include <iostream>

void parseJson() {
    std::ifstream f("../data/satisfactory.json");
    json data = json::parse(f);
    if (data.is_null()) {
        throw std::runtime_error("Failed to parse JSON data");
    }
    std::cout << "JSON data parsed successfully!" << std::endl;
    // Example of accessing data
    for (const auto& item : data) {
        std::cout << "Item: " << item.dump(4) << std::endl; // Pretty print each item
    }
}
