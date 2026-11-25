/**
 * @file ServiceRecordUtilTestTypes.h
 * @brief 레코드 판 패치 사슬 단위 테스트(`TestServiceRecordUtil.cpp`)의 레코드 타입입니다 — 판 1 레코드와 판 0 의 옛 모양.
 */
#pragma once
#include "Core/Foundation/Types.h"

#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Runtime/Online/Store/ServiceRecordUnknownFields.h"

namespace sw
{
    /** @brief 판 0 레코드의 옛 모양입니다 — 체력을 정수 `_hp` 로 실었다. 패치(`ServiceRecordUtil::upgradeRecordBody`)가 이 모양으로 옛 본문을 읽는다. */
    REFLECT()
    struct ServiceRecordTestShape0
    {
        REFLECT_BODY();

        PROPERTY()
        int32 _hp = 0;
    };

    /** @brief 판 1 레코드입니다. 체력이 실수 `_health` 로 뜻이 바뀌어 판을 올렸다. */
    REFLECT( Version = 1 )
    struct ServiceRecordTestRecord
    {
        REFLECT_BODY();

        PROPERTY()
        float32 _health = 0.0f;

        ServiceRecordUnknownFields _unknownProperties{};
    };
} // namespace sw
