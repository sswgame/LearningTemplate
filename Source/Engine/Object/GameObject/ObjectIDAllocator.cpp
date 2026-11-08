/**
 * @file ObjectIDAllocator.cpp
 * @brief 오브젝트 · 컴포넌트 id 번호 공간의 구현과 엔진 서비스 등록입니다.
 */
#include "pch.h"

#include "Engine/Object/GameObject/ObjectIDAllocator.h"

#include "Engine/Foundation/EngineServices.h"
#include "Engine/Foundation/EngineSubsystemRegistration.h"

namespace sw
{
    ObjectIDAllocator::ObjectIDAllocator()
        : _nextObjectID{ 1 }
        , _nextComponentID{ 1 }
    {
    }

    uint64 ObjectIDAllocator::reserveObjectID( uint64 objectID )
    {
        _nextObjectID.fetch_max( objectID + 1, std::memory_order_relaxed );
        return objectID;
    }

    uint64 ObjectIDAllocator::reserveComponentID( uint64 componentID )
    {
        _nextComponentID.fetch_max( componentID + 1, std::memory_order_relaxed );
        return componentID;
    }
} // namespace sw

namespace sw
{
    // 오브젝트 · 컴포넌트 id 공간 — 엔진의 모든 씬이 나눠 쓴다(`SceneEngineServices::makeFromBoundEngine` 이 칸에 싣는다).
    SW_ENGINE_SERVICE_SUBSYSTEM( ObjectIDAllocator, HostOnly, Services, (), SW_ENGINE_DEFAULT_FACTORY( ObjectIDAllocator ) );
} // namespace sw
