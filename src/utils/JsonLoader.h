#ifndef JSONLOADER_H
#define JSONLOADER_H

#include <nlohmann/json.hpp>
#include <unordered_map>
#include <string>

#include "../core/Recipe.h"
#include "../core/Resource.h"

using json = nlohmann::json;

class JsonLoader {
public:
    static json loadFromFile(const std::string &filePath);

    static bool validateSchema(const json &data);

    static Resource parseResource(const json &itemJson, int id);

    static Recipe parseRecipe(const json &recipeJson, int id,
                              const std::unordered_map<std::string, int> &keyNameToId);
};

#endif // JSONLOADER_H
