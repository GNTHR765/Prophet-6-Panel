#pragma once
#include <vector>
/*  The 500 program slots of the Prophet-6 (banks 0-4; the factory banks 5-9 mirror them). This list carries no program
    names and no categories: the names come from the synth itself (SCAN NAMES in the preset browser), the categories from
    what you file in the panel. */
namespace P6Presets
{
    struct Entry { int number; const char* category; const char* name; const char* description; bool p5; };
    inline const std::vector<Entry>& all()
    {
        static const std::vector<Entry> v = []
        {
            std::vector<Entry> a; a.reserve (500);
            for (int i = 0; i < 500; ++i) a.push_back ({ i, "Unsorted", "", "", false });
            return a;
        }();
        return v;
    }
}
