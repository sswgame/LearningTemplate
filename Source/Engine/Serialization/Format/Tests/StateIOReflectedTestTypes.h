/**
 * @file StateIOReflectedTestTypes.h
 * @brief `StateIO::reflected` 단위 테스트(`TestStateIOReflected.cpp`)가 쓰는 리플렉션 상태 형식입니다 — 칸 형식마다 하나씩, 판과 패치 사슬, 손 본문 더하기.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/VectorMath.h"

#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/Serialization/Format/StateIO.h"

namespace sw
{
    ENUM()
    enum class StateIOReflectedTestPhase : uint8
    {
        Idle,
        Running,
        Done,
    };

    /** @brief 상태 칸 안의 구조체 값입니다 — 안쪽 칸은 머리 없이 실리고 배치는 바깥 타입의 해시에 든다. */
    REFLECT()
    struct StateIOReflectedTestPart
    {
        REFLECT_BODY();

        PROPERTY()
        int32 _count = 0;

        PROPERTY()
        float32 _weight = 1.0f;
    };

    /** @brief 칸 형식(정수 · 실수 벡터 · 글 · 이름 · 열거 · 구조체 · 구조체 시퀀스 · 값 시퀀스 · 비트필드)을 하나씩 든 상태입니다. 상태가 아닌 칸(`_tuning`)은 실리지 않는다. */
    REFLECT()
    struct StateIOReflectedTestState
    {
        REFLECT_BODY();
        SW_DECLARE_REFLECTED_STATE_IO();

        PROPERTY( RuntimeState, Min = 0 )
        int32 _steps = 0;

        PROPERTY( RuntimeState )
        float3 _target{ 1.0f, 2.0f, 3.0f };
        PROPERTY( RuntimeState )
        int2 _cell{ 1, 1 };

        PROPERTY( RuntimeState )
        string _note = "idle";

        PROPERTY( RuntimeState )
        hashed_string _label{};

        PROPERTY( RuntimeState )
        StateIOReflectedTestPhase _phase = StateIOReflectedTestPhase::Idle;

        PROPERTY( RuntimeState )
        StateIOReflectedTestPart _part;

        PROPERTY( RuntimeState )
        vector<StateIOReflectedTestPart> _listPart;

        PROPERTY( RuntimeState )
        vector<int32> _listVisited;

        PROPERTY()
        float32 _tuning = 0.5f; ///< 저작 값 — 상태가 아니다

        PROPERTY( RuntimeState )
        uint8                  _bOpen    : 1;
        [[maybe_unused]] uint8 _reserved : 7;

        StateIOReflectedTestState()
            : _bOpen{ SW_FALSE }
            , _reserved{ 0 }
        {
        }
    };

    /** @brief 판 2 인 상태입니다. 판 1 은 체력 하나(int16)였고 `SW_TYPE_VERSION_PATCH` 가 올린다(테스트 .cpp). */
    REFLECT( Version = 2 )
    struct StateIOReflectedTestVersioned
    {
        REFLECT_BODY();
        SW_DECLARE_REFLECTED_STATE_IO();

        PROPERTY( RuntimeState )
        int32 _health = 0;

        PROPERTY( RuntimeState )
        float32 _speed = 0.0f;
    };

    /** @brief 리플렉션 칸에 손 본문이 리플렉션 밖 값(`_handOnly`)과 칸 사이 검사를 더하는 상태입니다. */
    REFLECT()
    struct StateIOReflectedTestMixed
    {
        REFLECT_BODY();
        SW_DECLARE_STATE_IO()
        {
            io.reflected( *this );
            io.value( _handOnly );
            io.check( _handOnly <= _limit );
        }

        PROPERTY( RuntimeState )
        int32 _limit = 10;

        int32 _handOnly = 0; ///< 리플렉션 밖 값 — 손 본문이 싣는다
    };
} // namespace sw
