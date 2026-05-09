#pragma once
#include <string>

struct NameComponent
{
    std::string name;

    NameComponent() = default;
    NameComponent(std::string name);
};