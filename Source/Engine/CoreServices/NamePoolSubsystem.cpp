#include "pch.h"

#include "Engine/CoreServices/NamePoolSubsystem.h"

#include "Core/String/hashed_string.h"

#include "Engine/CoreServices/DiagnosticsSubsystem.h"

namespace sw
{
    NamePoolSubsystem::NamePoolSubsystem()
        : _bInitialized{ false }
    {
    }

    NamePoolSubsystem::~NamePoolSubsystem()
    {
        if ( _bInitialized )
            HashedStringPool::shutdown();
    }

    SubsystemInitResult NamePoolSubsystem::initializeSubsystem()
    {
        HashedStringPool::initialize();
        _bInitialized = true;
        return SubsystemInitResult::Succeeded;
    }

    span<const utf8* const> NamePoolSubsystem::getSubsystemDependencies() const
    {
        // 풀의 블록도 메모리 프로파일러가 센다 — 그것이 먼저 선다.
        static constexpr const utf8* kArrDependency[] = { DiagnosticsSubsystem::kSubsystemName };
        return kArrDependency;
    }
} // namespace sw
