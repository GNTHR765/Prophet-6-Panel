#pragma once
#include <juce_core/juce_core.h>

// All Claude panels keep their data in one place: ~/Documents/Claude Plugins Data/<name>/.
// A folder that still sits directly in Documents from earlier versions is moved there the first time.
inline juce::File claudeDataDir (const char* name)
{
    auto docs = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory);
    auto root = docs.getChildFile ("Claude Plugins Data"); root.createDirectory();
    auto dir = root.getChildFile (name), old = docs.getChildFile (name);
    if (! dir.exists() && old.isDirectory()) old.moveFileTo (dir);
    dir.createDirectory();
    return dir;
}
