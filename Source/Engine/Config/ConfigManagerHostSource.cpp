/**
 * @file ConfigManagerHostSource.cpp
 * @brief 개발 구성의 호스트 설정 읽기 — 엔진 설정(`Config/Runtime`)과 고른 게임의 프리셋을 디스크에서 읽는다(없으면 멈춘다).
 * @details 배포는 `ConfigManagerHostSourceShipping.cpp`(베이크한 JSON)를 CMake 가 고른다(`Source/Engine/CMakeLists.txt`).
 */
#include "pch.h"

#include "Engine/Config/ConfigManager.h"
#include "Engine/Config/EngineConfig.h"
#include "Engine/Config/GameConfig.h"
#include "Engine/Config/GameSelection.h"

#include "sw/config/ConfigConstants.h"

namespace sw
{
    SubsystemInitResult ConfigManager::initializeSubsystem()
    {
        // 읽은 값은 호스트 자리(`getHostEngineConfig` · `getHostGameConfig`)에 담긴다 - 서비스 표가 그 주소를 이미 쥐고 있다.
        if ( loadHostConfig<EngineConfig>( ConfigSource::disk( config::kFileRuntimeEngineConfig ) ) == nullptr )
            return SubsystemInitResult::Failed;
        if ( loadHostConfig<GameConfig>( ConfigSource::disk( GameSelection::getConfigPath() ) ) == nullptr )
            return SubsystemInitResult::Failed;
        return SubsystemInitResult::Succeeded;
    }
} // namespace sw
