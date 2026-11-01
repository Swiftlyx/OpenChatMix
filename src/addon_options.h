#pragma once

#include <map>
#include <string>
#include <vector>

#include "settings.h"

// A setting an add-on program describes for the panel. The program carries the description as a resource:
// RCDATA "OPENCHATMIX_OPTIONS", UTF-8 JSON {"options": [...]}, each entry with "type" (section, switch, choice,
// multi, text, search or hint), "key", "default", "label" and "hint" (a string, or an object of strings by language
// code such as "en", "uk", "tr"), "choices" ([{"value", "label"}]), "showIf" (the key of a switch that must be on)
// and, for multi, "query" (true: the program lists the choices) and "empty" (the label while none is ticked).
// Reading it loads the file as data only; none of its code runs.
//
// Choices a program lists, and the results of a search, come from running it with
// --openchatmix-query <key> [--openchatmix-text <typed text>] --openchatmix-language <code>; it answers on standard
// output with {"choices": [{"value", "label"}]} and may add "error". A multi option keeps the ticked values one per
// line; a search keeps the picked value under its key and its label under "<key>.label".
struct AddOnOption {
    enum class Kind { Section, Switch, Choice, Multi, Text, Search, Hint };

    struct Choice {
        std::wstring value;
        std::wstring label;
    };

    Kind kind = Kind::Hint;
    std::wstring key;
    std::wstring label;
    std::wstring hint;
    std::wstring fallback;  // The value until the user picks one; switches use "1" and "0".
    std::vector<Choice> choices;
    std::wstring showIf;
    bool query = false;
    std::wstring empty;
};

// What the program answered to a query.
struct AddOnQueryResult {
    std::wstring path;
    std::wstring key;
    std::wstring text;
    bool answered = false;
    std::vector<AddOnOption::Choice> choices;
    std::wstring error;
};

// The options of the add-on at path in the panel's language; empty when it describes none. Cached by the file's
// write time. UI thread only.
const std::vector<AddOnOption>& AddOnOptions(const std::wstring& path);
std::wstring AddOnOptionValue(const AddOnSettings& addOn, const AddOnOption& option);
bool AddOnOptionShown(const AddOnSettings& addOn, const std::vector<AddOnOption>& options, const AddOnOption& option);
// The options as the add-on gets them at its start, after --openchatmix-options: key=value pairs joined by ';',
// with '%', ';', '=', '"', '\' and control characters written as %XX.
std::wstring EncodeAddOnOptions(const std::map<std::wstring, std::wstring>& options);
// Runs the program to answer a query and waits for it, a few seconds at most; any thread.
AddOnQueryResult QueryAddOn(const std::wstring& path, const std::wstring& key, const std::wstring& text);
