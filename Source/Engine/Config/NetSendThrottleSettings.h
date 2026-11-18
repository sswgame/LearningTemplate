/**
 * @file NetSendThrottleSettings.h
 * @brief 연결 송신 속도 조절(`NetSendThrottle`, AIMD)의 설정 파일 모양입니다. 값은 `Config/Engine/NetSendThrottle.json` 에 있습니다.
 * @details 설정 층(1)에 두어 설정 파일 검사(`ConfigFileSchemaTest`)가 넷 층 없이 읽는다. 게임 세션(`GameNetSession::initialize`)이 읽어
 *          `NetEndpointConfig::_maxBytesPerSecond` 와 `_sendThrottle` 에 옮긴다. 파일이 없거나 틀리면 세션을 열지 않는다(기본값으로 대신하지 않는다).
 */
#pragma once
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Network/Connection/NetSendThrottle.h"

#include "Engine/Config/IConfig.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    /**
     * @struct NetSendThrottleSettings
     * @brief 송신 속도의 상한 · 하한과 AIMD 계수입니다(모든 칸이 필수). 검사는 `NetSendThrottle::validate` 가 한다.
     */
    REFLECT()
    struct SW_API NetSendThrottleSettings : IConfig
    {
        REFLECT_BODY();

        /** @brief 설정 파일의 위치입니다(프로젝트 폴더 기준). */
        static constexpr const utf8* kConfigFile = "Config/Engine/NetSendThrottle.json";

        PROPERTY( Required, Tooltip = "Per-connection send rate ceiling in bytes per second (Unreal NetSpeed)" )
        int32 _maxBytesPerSecond{ 0 };
        PROPERTY( Required, Tooltip = "Send rate floor in bytes per second - reliable traffic keeps moving at this rate under loss (one packet or more, ceiling or less)" )
        int32 _minBytesPerSecond{ 0 };
        PROPERTY( Required, Tooltip = "Bytes per second added per adjustment without congestion (additive increase, > 0)" )
        int32 _increaseBytesPerSecond{ 0 };
        PROPERTY( Required, Tooltip = "Rate multiplier per congested adjustment (multiplicative decrease, in (0, 1))" )
        float64 _decreaseFactor{ 0.0 };
        PROPERTY( Required, Tooltip = "Packet loss ratio above which the link counts as congested, in (0, 1)" )
        float64 _lossThreshold{ 0.0 };
        PROPERTY( Required, Tooltip = "RTT above the smallest seen RTT by more than this many seconds counts as congestion (queueing delay, > 0)" )
        float64 _maxQueueDelaySeconds{ 0.0 };
        PROPERTY( Required, Tooltip = "Seconds between adjustments (> 0, connection stats refresh every 0.25 s)" )
        float64 _adjustIntervalSeconds{ 0.0 };

        /** @brief Core 계수로 옮깁니다(상한은 `_maxBytesPerSecond` 로 따로 쓴다). */
        NetSendThrottleConfig makeThrottleConfig() const
        {
            NetSendThrottleConfig config;
            config._minBytesPerSecond      = _minBytesPerSecond;
            config._increaseBytesPerSecond = _increaseBytesPerSecond;
            config._decreaseFactor         = _decreaseFactor;
            config._lossThreshold          = _lossThreshold;
            config._maxQueueDelaySeconds   = _maxQueueDelaySeconds;
            config._adjustIntervalSeconds  = _adjustIntervalSeconds;
            return config;
        }
    };
} // namespace sw
