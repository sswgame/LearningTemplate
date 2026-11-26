/**
 * @file TerrainTestServices.h
 * @brief 지형 테스트의 씬 서비스입니다 — 엔진 설정(`_terrain`)을 씬 범위에 값으로 넘깁니다(지형은 전역 설정을 읽지 않는다).
 */
#pragma once
#include "Core/Subsystem/SubsystemScope.h"

#include "Engine/Config/EngineConfig.h"
#include "Engine/Foundation/EngineSubsystems.h"
#include "Engine/Object/GameObject/Subsystem/SceneEngineSubsystems.h"

namespace sw::test
{
    /** @brief 바인딩된 엔진 서비스 위에 청크 메시 한도를 적은 엔진 설정을 얹습니다. 매니저보다 오래 산다. */
    struct TerrainTestServices
    {
        EngineConfig   _config{};
        SubsystemScope _scope{ engine::getEngineSubsystems() };

        explicit TerrainTestServices( uint32 chunkMeshRebuildCount = 4 )
        {
            _config._terrain._maxChunkMeshRebuildsPerFrame = chunkMeshRebuildCount;
            SW_REQUIRE_TRUE( _scope.bind( &_config ) );
        }

        SceneEngineSubsystems makeServices() const
        {
            SceneEngineSubsystems services = SceneEngineSubsystems::makeFromBoundEngine();
            services._pEngineSubsystems    = &_scope;
            return services;
        }
    };
} // namespace sw::test
