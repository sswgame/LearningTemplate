/**
 * @file NetSimSampleMessages.h
 * @brief 한 프로세스 재생 샘플 게임(`NetSimSampleGame`)의 메시지 본문입니다. 고정 배치형(`NetMessageBodyDef`)이라 본문 판이 스키마 해시입니다.
 * @details 에디터 확장 모듈(GF_EditorPlugin_NetSimulation)도 반사 단계를 거친다(`sw_add_module_library` 가 `NO_REFLECTION` 없이 부른다) — 이 헤더의 형식은 그 모듈에서 코드젠된다.
 *          필드 순서가 와이어 형식이다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"

#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    /** @brief 샘플 선 형식의 상한입니다. */
    struct NetSimSampleLimit
    {
        static constexpr int32 kMaxAvatarCount = 64; ///< 상태 메시지 하나의 아바타 수 상한(연결 하나에 아바타 하나)
        static constexpr int32 kAxisMax        = 1;  ///< 이동 입력 한 축의 정수 범위(−1..1) — 아래 `PROPERTY( Min · Max )` 와 같다
    };

    /** @brief 이동 입력(클라이언트 → 서버) 본문입니다. 순서가 뒤바뀐 낡은 입력은 서버가 버린다. */
    REFLECT()
    struct NetSimSampleInputBody
    {
        REFLECT_BODY();
        PROPERTY()
        uint32 _clientTick{ 0 };
        PROPERTY( Min = -1, Max = 1 )
        int32 _moveX{ 0 };
        PROPERTY( Min = -1, Max = 1 )
        int32 _moveY{ 0 };
    };

    /** @brief 아바타 하나의 위치입니다(서버 상태 본문의 원소). */
    REFLECT()
    struct NetSimSampleAvatarState
    {
        REFLECT_BODY();
        PROPERTY()
        uint32 _entityID{ 0 };
        PROPERTY( Min = -100000, Max = 100000 )
        float32 _x{ 0.0f }; ///< 허용 범위 검사(유한하지 않은 값도 거절한다)
        PROPERTY( Min = -100000, Max = 100000 )
        float32 _z{ 0.0f };
    };

    /** @brief 아바타 목록(서버 → 클라이언트) 본문입니다. 낡은 것은 클라이언트가 버린다. */
    REFLECT()
    struct NetSimSampleStateBody
    {
        REFLECT_BODY();
        PROPERTY()
        uint32 _serverTick{ 0 };
        PROPERTY( MaxCount = NetSimSampleLimit::kMaxAvatarCount )
        vector<NetSimSampleAvatarState> _listAvatar{};
    };
} // namespace sw
