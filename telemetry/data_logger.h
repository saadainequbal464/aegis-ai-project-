#pragma once

#include "unified_telemetry.h"

#include <cstddef>
#include <cstdint>
#include <fstream>
#include <string>

class DataLogger
{
public:
    DataLogger(
        const std::string& file_path,
        size_t flush_interval = 100
    );

    ~DataLogger();

    bool open();

    bool write(
        const TelemetryRecord& record
    );

    void flush();

    void close();

    bool isOpen() const;

    uint64_t rowsWritten() const;

    const std::string& sessionId() const;

private:
    void writeHeader();

    std::string GenerateSessionId();

    static int BoolToInt(bool value);

private:
    std::string file_path;

    size_t flush_interval;

    size_t pending_rows;

    uint64_t rows_written;

    bool opened;

    std::string session_id;

    std::ofstream file;
};

void SetExperimentMetadata(
    const ExperimentMetadata& metadata
);

ExperimentMetadata GetExperimentMetadata();

void ClearExperimentMetadata();