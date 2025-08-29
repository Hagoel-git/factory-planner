#ifndef STRINGUTILS_H
#define STRINGUTILS_H

#include <string>
#include <algorithm>
#include <cctype>
#include <locale>
#include <regex>

/**
 * @brief Trims leading and trailing whitespace from a string.
 * * @param s The string to trim, passed by reference.
 */
inline void trim(std::string &s) {
    // Erase leading whitespace
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](unsigned char ch) {
        return !std::isspace(ch);
    }));
    // Erase trailing whitespace
    s.erase(std::find_if(s.rbegin(), s.rend(), [](unsigned char ch) {
        return !std::isspace(ch);
    }).base(), s.end());
}

/**
 * @brief Slugifies a string based on a set of rules.
 * This function converts the string to lowercase, trims it, and performs a series
 * of regex-based replacements to create a clean, URL-friendly-like string.
 * @param text The input string to slugify.
 * @return The slugified string.
 */
inline std::string slugify(std::string text) {
    if (text.empty()) {
        return text;
    }
    // Convert the entire string to lowercase.
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char c){ return std::tolower(c); });

    // Trim leading and trailing whitespace.
    trim(text);

    // Replace all instances of '+' with 'plus'.
    text = std::regex_replace(text, std::regex("\\+"), "plus");

    // Replace all instances of ':' or '.' with '-'.
    text = std::regex_replace(text, std::regex("[:.]"), "-");

    // Remove characters that are not letters, numbers, whitespace, or specific symbols.
    text = std::regex_replace(text, std::regex("[^a-z0-9\\s:._-]"), "");

    // Collapse consecutive whitespace characters and/or hyphens into a single hyphen.
    text = std::regex_replace(text, std::regex("[\\s-]+"), "-");

    // Remove any leading or trailing hyphens that might have been created.
    text = std::regex_replace(text, std::regex("^-+|-+$"), "");

    return text;
}

#endif //STRINGUTILS_H
