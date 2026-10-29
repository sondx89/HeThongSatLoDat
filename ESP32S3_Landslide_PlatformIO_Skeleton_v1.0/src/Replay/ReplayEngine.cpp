#include "project/Replay/ReplayEngine.h"

#include <cerrno>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>

namespace project::replay {
namespace {

constexpr char kCsvHeader[] = "timestamp,tilt,soil,rain,stress,risk";
constexpr std::size_t kMaximumLineLength = 192;

void incrementSaturated(std::uint64_t& value) noexcept {
    if (value != std::numeric_limits<std::uint64_t>::max()) {
        ++value;
    }
}

bool validColumn(CsvColumn column) noexcept {
    switch (column) {
        case CsvColumn::Tilt:
        case CsvColumn::Soil:
        case CsvColumn::Rain:
        case CsvColumn::Stress:
            return true;
        case CsvColumn::Risk:
        case CsvColumn::Timestamp:
            return false;
    }
    return false;
}

bool parseUnsigned(const char* text, std::size_t length,
                   std::uint64_t& value) noexcept {
    if (text == nullptr || length == 0U) {
        return false;
    }
    value = 0U;
    for (std::size_t i = 0; i < length; ++i) {
        const char digit = text[i];
        if (digit < '0' || digit > '9') {
            return false;
        }
        const std::uint64_t number =
            static_cast<std::uint64_t>(digit - '0');
        if (value > (std::numeric_limits<std::uint64_t>::max() - number) /
                        10U) {
            return false;
        }
        value = value * 10U + number;
    }
    return true;
}

bool parseFloat(const char* text, std::size_t length, float& value) noexcept {
    if (text == nullptr || length == 0U || length >= 32U) {
        return false;
    }
    std::size_t index = 0U;
    if (text[index] == '-' || text[index] == '+') {
        ++index;
    }
    bool hasMantissaDigit = false;
    while (index < length && text[index] >= '0' && text[index] <= '9') {
        hasMantissaDigit = true;
        ++index;
    }
    if (index < length && text[index] == '.') {
        ++index;
        while (index < length && text[index] >= '0' && text[index] <= '9') {
            hasMantissaDigit = true;
            ++index;
        }
    }
    if (!hasMantissaDigit) {
        return false;
    }
    if (index < length && (text[index] == 'e' || text[index] == 'E')) {
        ++index;
        if (index < length && (text[index] == '-' || text[index] == '+')) {
            ++index;
        }
        const std::size_t exponentStart = index;
        while (index < length && text[index] >= '0' && text[index] <= '9') {
            ++index;
        }
        if (index == exponentStart) {
            return false;
        }
    }
    if (index != length) {
        return false;
    }

    char buffer[32]{};
    std::memcpy(buffer, text, length);
    char* end = nullptr;
    errno = 0;
    const float parsed = std::strtof(buffer, &end);
    if (errno == ERANGE || end != buffer + length || !std::isfinite(parsed)) {
        return false;
    }
    value = parsed;
    return true;
}

bool validRiskToken(const char* text, std::size_t length) noexcept {
    for (std::size_t i = 0; i < length; ++i) {
        const char value = text[i];
        if (!((value >= 'a' && value <= 'z') ||
              (value >= 'A' && value <= 'Z') ||
              (value >= '0' && value <= '9') || value == '_' ||
              value == '-')) {
            return false;
        }
    }
    return true;
}

std::size_t csvColumnIndex(CsvColumn column) noexcept {
    switch (column) {
        case CsvColumn::Timestamp:
            return 0U;
        case CsvColumn::Tilt:
            return 1U;
        case CsvColumn::Soil:
            return 2U;
        case CsvColumn::Rain:
            return 3U;
        case CsvColumn::Stress:
            return 4U;
        case CsvColumn::Risk:
            return 5U;
    }
    return 6U;
}

}  // namespace

ReplayEngine::ReplayEngine(ICsvReplaySource& source,
                           const ReplayConfig& config) noexcept
    : source_(source), config_(config) {}

ReplayStatus ReplayEngine::validateConfig(
    const ReplayConfig& config) noexcept {
    if (!config.datasetManifestVerified || !config.datasetHashVerified) {
        return ReplayStatus::ManifestNotVerified;
    }
    if (!config.featureSchemaApproved || !config.modelMetadataVerified ||
        !config.flatLayoutApproved || !config.columnMappingApproved) {
        return ReplayStatus::SchemaNotApproved;
    }
    if (config.csvSchemaVersion == 0U ||
        config.featureSchemaVersion == 0U ||
        config.featureSchemaId == 0U ||
        !config.featureSchemaHashValid || config.featureCount == 0U ||
        config.featureCount > project::models::FeatureVector::kMaxFeatures ||
        config.windowLengthSamples == 0U ||
        config.windowLengthSamples > kMaxWindowSamples ||
        config.strideSamples == 0U || config.replaySessionId == 0U ||
        !config.sampleRateConfigured ||
        !config.samplePeriodToleranceConfigured ||
        !std::isfinite(config.sampleRateHz) || config.sampleRateHz <= 0.0F) {
        return ReplayStatus::InvalidConfiguration;
    }

    const double expectedSamplePeriodMs =
        1000.0 / static_cast<double>(config.sampleRateHz);
    if (expectedSamplePeriodMs < 1.0 ||
        expectedSamplePeriodMs >
            static_cast<double>(std::numeric_limits<std::uint64_t>::max()) ||
        std::fabs(expectedSamplePeriodMs -
                  std::round(expectedSamplePeriodMs)) >
            static_cast<double>(config.samplePeriodToleranceMs)) {
        return ReplayStatus::InvalidConfiguration;
    }

    bool hashHasNonZeroByte = false;
    for (const std::uint8_t byte : config.featureSchemaHash) {
        hashHasNonZeroByte = hashHasNonZeroByte || byte != 0U;
    }
    bool datasetHashHasNonZeroByte = false;
    for (const std::uint8_t byte : config.datasetSha256) {
        datasetHashHasNonZeroByte = datasetHashHasNonZeroByte || byte != 0U;
    }
    if (!hashHasNonZeroByte || !datasetHashHasNonZeroByte) {
        return ReplayStatus::InvalidConfiguration;
    }

    const std::size_t valueCount =
        static_cast<std::size_t>(config.featureCount) *
        static_cast<std::size_t>(config.windowLengthSamples);
    if (valueCount != config.modelInputElementCount) {
        return ReplayStatus::SchemaNotApproved;
    }
    if (valueCount > project::models::FeatureVector::kMaxFeatures) {
        return ReplayStatus::OutputTooLarge;
    }

    for (std::size_t i = 0; i < config.featureCount; ++i) {
        const ReplayFeatureMapping& mapping = config.features[i];
        if (!validColumn(mapping.source)) {
            return ReplayStatus::UnsupportedMapping;
        }
        if (mapping.featureIndex != i || !mapping.modelInputEligible) {
            return ReplayStatus::SchemaNotApproved;
        }
        for (std::size_t previous = 0; previous < i; ++previous) {
            if (config.features[previous].source == mapping.source ||
                config.features[previous].featureIndex ==
                    mapping.featureIndex) {
                return ReplayStatus::InvalidConfiguration;
            }
        }
    }
    return ReplayStatus::Ok;
}

ReplayStatus ReplayEngine::begin() noexcept {
    initialized_ = false;
    clearWindow();
    rowSequence_ = 0U;
    previousTimestampMs_ = 0U;
    hasPreviousTimestamp_ = false;
    stats_ = {};

    status_ = validateConfig(config_);
    if (status_ != ReplayStatus::Ok) {
        return status_;
    }
    const CsvReplaySourceStatus rewindStatus = source_.rewind();
    if (rewindStatus != CsvReplaySourceStatus::Ok) {
        status_ = ReplayStatus::SourceError;
        return status_;
    }
    bufferPosition_ = 0U;
    bufferLength_ = 0U;
    discardingLine_ = false;
    status_ = validateHeader();
    if (status_ != ReplayStatus::Ok) {
        return status_;
    }
    initialized_ = true;
    status_ = ReplayStatus::Ok;
    return status_;
}

ReplayEngine::LineStatus ReplayEngine::readLine(
    char* output, std::size_t capacity, std::size_t& length) noexcept {
    length = 0U;
    if (output == nullptr || capacity < 2U) {
        return LineStatus::TooLong;
    }

    for (;;) {
        if (bufferPosition_ == bufferLength_) {
            std::size_t bytesRead = 0U;
            const CsvReplaySourceStatus sourceStatus =
                source_.read(readBuffer_.data(), readBuffer_.size(), bytesRead);
            if (sourceStatus == CsvReplaySourceStatus::IoError ||
                bytesRead > readBuffer_.size()) {
                return LineStatus::SourceError;
            }
            bufferPosition_ = 0U;
            bufferLength_ = bytesRead;
            if (sourceStatus == CsvReplaySourceStatus::EndOfFile) {
                if (bytesRead != 0U) {
                    return LineStatus::SourceError;
                }
                return length == 0U ? LineStatus::EndOfFile
                                    : LineStatus::Truncated;
            }
            if (bytesRead == 0U) {
                return LineStatus::SourceError;
            }
        }

        const char value =
            static_cast<char>(readBuffer_[bufferPosition_++]);
        if (discardingLine_) {
            if (value == '\n') {
                discardingLine_ = false;
                return LineStatus::TooLong;
            }
            continue;
        }
        if (value == '\n') {
            if (length > 0U && output[length - 1U] == '\r') {
                --length;
            }
            output[length] = '\0';
            return LineStatus::Complete;
        }
        if (length + 1U >= capacity) {
            discardingLine_ = true;
            continue;
        }
        output[length++] = value;
    }
}

ReplayStatus ReplayEngine::validateHeader() noexcept {
    std::array<char, kMaximumLineLength> line{};
    std::size_t length = 0U;
    const LineStatus lineStatus =
        readLine(line.data(), line.size(), length);
    if (lineStatus == LineStatus::SourceError) {
        return ReplayStatus::SourceError;
    }
    if (lineStatus != LineStatus::Complete ||
        length != sizeof(kCsvHeader) - 1U ||
        std::memcmp(line.data(), kCsvHeader, length) != 0) {
        return ReplayStatus::InvalidHeader;
    }
    return ReplayStatus::Ok;
}

ReplayStatus ReplayEngine::parseRow(
    const char* line, std::size_t length, ReplaySample& sample) noexcept {
    if (line == nullptr || length == 0U) {
        return ReplayStatus::InvalidRow;
    }

    const char* fields[6]{};
    std::size_t fieldLengths[6]{};
    std::size_t fieldIndex = 0U;
    std::size_t fieldStart = 0U;
    for (std::size_t i = 0; i < length; ++i) {
        if (line[i] != ',') {
            continue;
        }
        if (fieldIndex >= 5U) {
            return ReplayStatus::InvalidRow;
        }
        fields[fieldIndex] = line + fieldStart;
        fieldLengths[fieldIndex] = i - fieldStart;
        ++fieldIndex;
        fieldStart = i + 1U;
    }
    if (fieldIndex != 5U) {
        return ReplayStatus::InvalidRow;
    }
    fields[5] = line + fieldStart;
    fieldLengths[5] = length - fieldStart;

    std::uint64_t timestamp = 0U;
    if (!parseUnsigned(fields[0], fieldLengths[0], timestamp)) {
        return ReplayStatus::InvalidRow;
    }

    if (fieldLengths[5] != 0U &&
        !validRiskToken(fields[5], fieldLengths[5])) {
        return ReplayStatus::InvalidRow;
    }

    std::array<float, 5> numeric{};
    const std::size_t numericFieldColumns[] = {1U, 2U, 3U, 4U};
    for (const std::size_t column : numericFieldColumns) {
        if (fieldLengths[column] != 0U &&
            !parseFloat(fields[column], fieldLengths[column],
                        numeric[column - 1U])) {
            return ReplayStatus::InvalidRow;
        }
    }

    sample = {};
    for (std::size_t feature = 0; feature < config_.featureCount; ++feature) {
        const std::size_t column =
            csvColumnIndex(config_.features[feature].source);
        if (column == 0U || column >= 5U || fieldLengths[column] == 0U) {
            return ReplayStatus::InvalidRow;
        }
        sample.values[feature] = numeric[column - 1U];
    }

    if (hasPreviousTimestamp_) {
        const double expected =
            1000.0 / static_cast<double>(config_.sampleRateHz);
        const bool timestampDiscontinuous =
            timestamp <= previousTimestampMs_ ||
            std::fabs(static_cast<double>(timestamp - previousTimestampMs_) -
                      expected) >
                static_cast<double>(config_.samplePeriodToleranceMs);
        if (timestampDiscontinuous) {
            previousTimestampMs_ = timestamp;
            return ReplayStatus::TimestampDiscontinuity;
        }
    }
    previousTimestampMs_ = timestamp;
    hasPreviousTimestamp_ = true;

    sample.timestampMs = timestamp;
    sample.rowSequence = rowSequence_;
    return ReplayStatus::Ok;
}

ReplayStatus ReplayEngine::buildVector(
    project::models::FeatureVector& output) noexcept {
    output = {};
    const std::size_t valueCount =
        static_cast<std::size_t>(config_.featureCount) *
        static_cast<std::size_t>(config_.windowLengthSamples);
    if (valueCount > project::models::FeatureVector::kMaxFeatures) {
        return ReplayStatus::OutputTooLarge;
    }

    const std::size_t first =
        (sampleCount_ == config_.windowLengthSamples)
            ? 0U
            : sampleCount_ - config_.windowLengthSamples;
    for (std::size_t sampleOffset = 0;
         sampleOffset < config_.windowLengthSamples; ++sampleOffset) {
        const ReplaySample& sample = samples_[first + sampleOffset];
        for (std::size_t feature = 0; feature < config_.featureCount; ++feature) {
            const std::size_t outputIndex =
                sampleOffset * config_.featureCount + feature;
            output.values[outputIndex] = sample.values[feature];
            output.featureValidMask |=
                static_cast<std::uint32_t>(1UL << outputIndex);
        }
    }

    output.schemaVersion = config_.featureSchemaVersion;
    output.schemaId = config_.featureSchemaId;
    for (std::size_t i = 0; i < config_.featureSchemaHash.size(); ++i) {
        output.schemaHashSha256[i] = config_.featureSchemaHash[i];
    }
    output.schemaHashValid = config_.featureSchemaHashValid;
    output.bootId = config_.replaySessionId;
    output.windowStartMs = samples_[first].timestampMs;
    const ReplaySample& last =
        samples_[first + config_.windowLengthSamples - 1U];
    output.windowEndMs = last.timestampMs;
    output.relatedSequenceNo = last.rowSequence;
    output.windowSampleCount = config_.windowLengthSamples;
    output.count = static_cast<std::uint16_t>(valueCount);
    output.valid = true;
    incrementSaturated(stats_.vectorsProduced);
    samplesSinceBuild_ = 0U;
    return ReplayStatus::VectorReady;
}

ReplayStatus ReplayEngine::next(
    project::models::FeatureVector& output) noexcept {
    output = {};
    if (!initialized_) {
        status_ = ReplayStatus::NotInitialized;
        return status_;
    }

    std::array<char, kMaximumLineLength> line{};
    for (;;) {
        std::size_t length = 0U;
        const LineStatus lineStatus =
            readLine(line.data(), line.size(), length);
        if (lineStatus == LineStatus::EndOfFile) {
            status_ = ReplayStatus::EndOfFile;
            return status_;
        }
        if (lineStatus == LineStatus::SourceError) {
            clearWindow();
            status_ = ReplayStatus::SourceError;
            return status_;
        }
        if (lineStatus == LineStatus::Truncated ||
            lineStatus == LineStatus::TooLong) {
            clearWindow();
            incrementSaturated(stats_.rowsRejected);
            if (lineStatus == LineStatus::TooLong &&
                rowSequence_ != std::numeric_limits<std::uint64_t>::max()) {
                ++rowSequence_;
                stats_.lastRowSequence = rowSequence_;
            }
            status_ = lineStatus == LineStatus::Truncated
                          ? ReplayStatus::TruncatedRow
                          : ReplayStatus::InvalidRow;
            return status_;
        }

        ReplaySample sample{};
        if (rowSequence_ == std::numeric_limits<std::uint64_t>::max()) {
            incrementSaturated(stats_.rowsRejected);
            status_ = ReplayStatus::InvalidRow;
            return status_;
        }
        ++rowSequence_;
        stats_.lastRowSequence = rowSequence_;
        const ReplayStatus rowStatus = parseRow(line.data(), length, sample);
        if (rowStatus != ReplayStatus::Ok) {
            clearWindow();
            incrementSaturated(stats_.rowsRejected);
            status_ = rowStatus;
            return status_;
        }
        incrementSaturated(stats_.rowsRead);
        if (sampleCount_ == kMaxWindowSamples) {
            for (std::size_t i = 1U; i < sampleCount_; ++i) {
                samples_[i - 1U] = samples_[i];
            }
            --sampleCount_;
        }
        samples_[sampleCount_++] = sample;
        if (samplesSinceBuild_ < config_.strideSamples) {
            ++samplesSinceBuild_;
        }

        if (sampleCount_ >= config_.windowLengthSamples &&
            samplesSinceBuild_ >= config_.strideSamples) {
            status_ = buildVector(output);
            return status_;
        }
    }
}

void ReplayEngine::clearWindow() noexcept {
    samples_ = {};
    sampleCount_ = 0U;
    samplesSinceBuild_ = 0U;
}

void ReplayEngine::reset() noexcept {
    initialized_ = false;
    bufferPosition_ = 0U;
    bufferLength_ = 0U;
    discardingLine_ = false;
    rowSequence_ = 0U;
    previousTimestampMs_ = 0U;
    hasPreviousTimestamp_ = false;
    stats_ = {};
    clearWindow();
    status_ = ReplayStatus::NotInitialized;
}

}  // namespace project::replay
