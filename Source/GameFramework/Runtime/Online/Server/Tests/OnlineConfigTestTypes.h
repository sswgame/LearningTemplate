/**
 * @file OnlineConfigTestTypes.h
 * @brief 온라인 서버 설정 테스트의 키트 설정 형식입니다. 리플렉션 파서가 헤더만 읽으므로 테스트 `.cpp` 밖에 둡니다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"

#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    /** @brief 칸 형식마다 하나씩 둔 설정입니다. */
    REFLECT()
    struct OnlineConfigTestKitConfig
    {
        REFLECT_BODY();
        PROPERTY( Min = 1, Max = 65535 )
        int32 _port{ 1 };
        PROPERTY()
        bool _bDebug{ false };
        PROPERTY()
        vector<string> _listMode{};
        PROPERTY()
        int32 _untouched{ 42 };
    };

    /** @brief 필수 칸이 있는 설정입니다. */
    REFLECT()
    struct OnlineConfigTestRequiredConfig
    {
        REFLECT_BODY();
        PROPERTY( Required )
        string _name{};
    };

    /** @brief 런타임 테스트 키트의 설정입니다. `init` · `start` 면 그 단계에 실패합니다. */
    REFLECT()
    struct OnlineRuntimeTestKitConfig
    {
        REFLECT_BODY();
        PROPERTY()
        string _failStage{};
    };
} // namespace sw
