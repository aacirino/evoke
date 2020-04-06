#pragma once

#include "File.h"

#include <ostream>
#include <string>
#include <vector>

struct PendingCommand
{
public:
    PendingCommand(const std::string &command);
    void AddInput(File *input);
    void AddOutput(File *output);
    std::vector<File *> inputs;
    std::vector<File *> outputs;
    void Check();

public:
    std::string commandToRun;
    enum State
    {
        Unknown,
        ToBeRun,
        Running,
        Done,
        Depfail
    } state = Unknown;
    void SetResult(int errorcode, std::string messages, double timeTaken, uint64_t spaceUsed);
    bool CanRun();
    struct Result {
        std::string output;
        int errorcode = 0;
        int measurementCount = 0;
        double timeEstimate = 1; // assumption: 1 second.
        uint64_t spaceNeeded = 1 << 30; // assumption: 1 GB of memory use
    };
    Result* result = nullptr;
};

std::ostream &operator<<(std::ostream &os, const PendingCommand &);
