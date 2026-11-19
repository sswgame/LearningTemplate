/**
 * @file StateHashTestComponent.h
 * @brief 상태 해시 단위 테스트(`TestSceneStateHash.cpp`)가 다는 컴포넌트입니다 — 상태 역할 칸(`RuntimeState`)의 형식마다 하나씩, 개발 구성만 코드젠하고 메뉴에 나오지 않는다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Math/VectorMath.h"

#include "Engine/Foundation/GameObjectHandle.h"
#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    /** @brief 상태 칸 안의 구조체 값입니다 — 안쪽 칸도 해시에 접힌다. */
    REFLECT()
    struct StateHashTestPart
    {
        REFLECT_BODY();

        PROPERTY()
        int32 _count = 0;

        PROPERTY()
        float32 _weight = 1.0f;
    };

    /** @brief 상태 칸의 형식(정수 · 실수 · 벡터 · 정수 좌표 · 글 · 이름 · 오브젝트 핸들 · 비트필드 · 구조체 · 시퀀스)을 하나씩 든 컴포넌트입니다. 상태가 아닌 칸(`_tuning`)은 해시에 들지 않는다. */
    REFLECT( HideInMenu )
    struct StateHashTestComponent : public Component
    {
        REFLECT_BODY();

        PROPERTY( RuntimeState )
        int32 _steps = 0;

        PROPERTY( RuntimeState )
        float32 _timeLeft = 2.0f;

        PROPERTY( RuntimeState )
        float3 _target{ 1.0f, 2.0f, 3.0f };

        PROPERTY( RuntimeState )
        string _phase = "idle";

        PROPERTY( RuntimeState )
        int2 _cell{ 0, 0 }; ///< i32 Layout — 성분마다 접는다

        PROPERTY( RuntimeState )
        hashed_string _label{}; ///< 손 처리기 — 글자로 접는다

        PROPERTY( RuntimeState )
        GameObjectHandle _target; ///< 손 처리기 — objectID 로 접는다

        PROPERTY( RuntimeState )
        StateHashTestPart _part;

        PROPERTY( RuntimeState )
        vector<int32> _listVisited;

        PROPERTY()
        float32 _tuning = 0.5f; ///< 저작 값 — 상태가 아니다

        PROPERTY( RuntimeState )
        uint8                  _bOpen    : 1;
        [[maybe_unused]] uint8 _reserved : 7;

        StateHashTestComponent()
            : _bOpen{ SW_FALSE }
            , _reserved{ 0 }
        {
        }
    };
} // namespace sw
