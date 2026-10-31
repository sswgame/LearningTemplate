/**
 * @file ConfigManagerHostSourceShipping.cpp
 * @brief 배포 구성의 호스트 설정 읽기 — 디스크의 `Config/` 를 보지 않고 베이크한 JSON(엔진 설정 · 베이크한 게임의 프리셋)만 읽는다.
 * @details 그 밖의 설정은 디스크 파일이 필수다 — 없으면 베이크한 사본이나 `T{}` 로 서지 않고 멈춘다. 개발은 `ConfigManagerHostSource.cpp` 를 CMake 가 고른다.
 */
#include "pch.h"

#include "Core/Log/Logger.h"

#include "Engine/Config/ConfigManager.h"
#include "Engine/Config/EngineConfig.h"
#include "Engine/Config/GameConfig.h"
#include "Engine/Serialization/Format/JSONSerializer.h"

#include "sw/config/ShippingGameDefaults.h" // 베이크한 게임의 프리셋
#include "sw/config/ShippingHostDefaults.h"

namespace sw
{
    SW_LOG_CALLER( "ConfigManager" );

    SubsystemInitResult ConfigManager::initializeSubsystem()
    {
        EngineConfig probe;
        if ( JSONSerializer::deserialize( &probe, *EngineConfig::StaticType(), shipping_host::kEngineConfigJSON ) == false )
        {
            SW_LOG_ERROR( "The generated EngineConfig JSON does not deserialize - a reflection registrar was dropped at link time" );
            return SubsystemInitResult::Failed;
        }
        // 읽은 값은 호스트 자리(`getHostEngineConfig` · `getHostGameConfig`)에 담긴다 - 서비스 표가 그 주소를 이미 쥐고 있다.
        if ( loadHostConfig<EngineConfig>( ConfigSource::baked( shipping_host::kEngineConfigJSON ) ) == nullptr )
            return SubsystemInitResult::Failed;
        if ( loadHostConfig<GameConfig>( ConfigSource::baked( shipping_host::kGameConfigJSON ) ) == nullptr )
            return SubsystemInitResult::Failed;
        return SubsystemInitResult::Succeeded;
    }
} // namespace sw
