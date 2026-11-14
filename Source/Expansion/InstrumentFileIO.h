#pragma once

#include "InstrumentModel.h"

#include <string>
#include <vector>

namespace sampler
{
struct RelinkIssue
{
    std::string sampleId;
    std::string originalPath;
    std::vector<std::string> candidates;
};

struct RelinkReport
{
    std::size_t relinkedSamples = 0;
    std::vector<RelinkIssue> unresolved;
};

bool saveInstrumentFile(const InstrumentModel& model,
                        const std::string& filePath,
                        std::string* error = nullptr);

bool loadInstrumentFile(const std::string& filePath,
                        InstrumentModel& model,
                        std::string* error = nullptr);

// Resolves missing sample files by exact filename. Ambiguous matches are never guessed.
// The destination model is only replaced when every missing reference is resolved.
bool relinkMissingSamples(const InstrumentModel& source,
                          const std::vector<std::string>& searchRoots,
                          InstrumentModel& destination,
                          RelinkReport& report,
                          std::string* error = nullptr);
}
