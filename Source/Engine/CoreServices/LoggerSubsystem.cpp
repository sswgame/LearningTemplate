#include "pch.h"

#include "Engine/CoreServices/LoggerSubsystem.h"

#include "Core/Log/Sink/AsyncLogSink.h"

#include "Engine/CoreServices/NamePoolSubsystem.h"

namespace sw
{
    LoggerSubsystem::LoggerSubsystem()
        : _logger{ nullptr }
    {
    }

    LoggerSubsystem::~LoggerSubsystem() = default;

    SubsystemInitResult LoggerSubsystem::initializeSubsystem()
    {
        if ( _logger == nullptr )
        {
            _logger = make_unique<AsyncLogSink>();
            _logger->initialize();
        }
        return SubsystemInitResult::Succeeded;
    }

    void LoggerSubsystem::shutdownSubsystem()
    {
        if ( _logger != nullptr )
            _logger->shutdown();
    }

    span<const utf8* const> LoggerSubsystem::getSubsystemDependencies() const
    {
        // 로그 인자로 이름(hashed_string)이 들어온다 — 이름 풀이 먼저 서고 로거 뒤에 내려간다.
        static constexpr const utf8* kArrDependency[] = { NamePoolSubsystem::kSubsystemName };
        return kArrDependency;
    }
} // namespace sw
