#pragma once

#include <atomic>
#include <thread>

enum class ForegroundWorkloadLevel
{
    LOW = 0,
    MEDIUM = 1,
    HIGH = 2,
    PEAK = 3
};

bool StartForegroundWorkload(
    ForegroundWorkloadLevel level);

void SetForegroundWorkloadLevel(
    ForegroundWorkloadLevel level);

void SetForegroundWorkloadIntensity(
    double intensity);

void SetForegroundWorkload(
    ForegroundWorkloadLevel level,
    double intensity);

void StopForegroundWorkload();

bool IsForegroundWorkloadRunning();

ForegroundWorkloadLevel
GetForegroundWorkloadLevel();

double GetForegroundWorkloadIntensity();