#include "pch.h"

#include "Engine/CoreServices/LoggerSubsystem.h"

#include "Core/CommandLine/CommandLineManager.h"
#include "Core/Log/Sink/AsyncLogSink.h"

#include "Engine/CoreServices/CommandLineSubsystem.h"

namespace sw
{
    LoggerSubsystem::LoggerSubsystem( const CommandLineSubsystem& commandLine )
        : _commandLine{ commandLine }
        , _logger{ nullptr }
    {
    }

    LoggerSubsystem::~LoggerSubsystem() = default;

    SubsystemInitResult LoggerSubsystem::initializeSubsystem()
    {
        if ( _logger == nullptr )
        {
            string logFilePath; // 비면 기본 폴더(Saved/Logs)다
            (void)_commandLine.findCommandLine()->getArgument( CommandLineArgument::LOG_FILE, logFilePath );
            _logger = make_unique<AsyncLogSink>( std::move( logFilePath ) );
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
        // 로그 파일이 명령줄로 정해진다. 이름 풀은 명령줄이 먼저 기댄다(CommandLineSubsystem).
        static constexpr const utf8* kArrDependency[] = { CommandLineSubsystem::kSubsystemName };
        return kArrDependency;
    }
} // namespace sw
