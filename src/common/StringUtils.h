#ifndef STRINGUTILS_H
#define STRINGUTILS_H

#include <string>
#include <algorithm>
#include <cctype>
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

/**
 * @brief Compares two strings using natural sort order (e.g., "file2" < "file10").
 * Handles numeric segments by value and other characters case-insensitively.
 */
inline bool naturalLess(const std::string& a, const std::string& b) {
    size_t i = 0, j = 0;
    while (i < a.length() && j < b.length()) {
        // Check if both characters are digits
        if (std::isdigit(a[i]) && std::isdigit(b[j])) {
            // Scan full numeric chunk in a
            size_t startI = i;
            while (i < a.length() && std::isdigit(a[i])) i++;

            // Scan full numeric chunk in b
            size_t startJ = j;
            while (j < b.length() && std::isdigit(b[j])) j++;

            // Compare numeric chunks
            size_t lenA = i - startI;
            size_t lenB = j - startJ;

            // 1. Compare by length (shorter number is smaller, assuming no leading zeros like 01 vs 1)
            if (lenA != lenB) {
                return lenA < lenB;
            }

            // 2. If lengths are equal, compare lexicographically (effectively by value)
            int cmp = a.compare(startI, lenA, b, startJ, lenB);
            if (cmp != 0) {
                return cmp < 0;
            }
            // If numbers are identical, continue to next characters
        } else {
            // Case-insensitive character comparison
            char cA = static_cast<char> (std::tolower(a[i]));
            char cB = static_cast<char> (std::tolower(b[j]));

            if (cA != cB) {
                return cA < cB;
            }
            i++;
            j++;
        }
    }
    // If one string is a prefix of the other, the shorter one comes first
    return a.length() < b.length();
}

#endif //STRINGUTILS_H
