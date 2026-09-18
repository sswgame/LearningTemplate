/**
 * @file IConfig.h
 * @brief 모든 설정 구조체의 공통 조상. `ConfigManager` 가 이것으로 한 표에 담는다.
 */
#pragma once
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    /**
     * @struct IConfig
     * @brief 설정 구조체의 표식(tag) 타입입니다. 값은 없고, `ConfigManager` 의 표에 담기기 위한 것입니다.
     * @details 파생 타입은 `REFLECT()` 여야 합니다 — `ConfigManager` 가 `StaticType()` 으로 표의
     *          열쇠를 뽑고 JSON 역직렬화도 그 `TypeInfo` 로 한다.
     */
    struct SW_API IConfig
    {
        IConfig()                                = default;
        virtual ~IConfig()                       = default;
        IConfig( const IConfig& )                = default;
        IConfig& operator=( const IConfig& )     = default;
        IConfig( IConfig&& ) noexcept            = default;
        IConfig& operator=( IConfig&& ) noexcept = default;

        REFLECT_BODY();
    };
} // namespace sw
