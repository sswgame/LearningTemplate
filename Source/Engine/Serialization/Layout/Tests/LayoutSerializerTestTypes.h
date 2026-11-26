/**
 * @file LayoutSerializerTestTypes.h
 * @brief 스키마 결합형 해석기 단위 테스트(`TestLayoutSerializer.cpp`)가 쓰는 반사 타입입니다 — 코드젠이 서술자 표를 내 Engine 모듈에 오른다.
 * @details 개발 구성만 코드젠한다. 서술자 칸 종류를 한 번씩 다 쓴다: bool · 비트필드 · 범위 정수 · 가변 정수 · 앞선 필드가 범위인 정수 · 실수 셋(원본 · 허용 범위 · `Step`) ·
 *          float64 · 수학 타입 · 열거형 · 비트 플래그 · 글 · vector · array · 펼친 중첩 타입(바깥 필드가 범위) · 다른 스키마(검증 함수가 있어 펼치지 않는다) · 기반 클래스.
 */
#pragma once
#include "Core/Container/EnumArray.h"
#include "Core/Container/array.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/BitUtil.h"
#include "Core/Math/VectorMath.h"

#include "Engine/EngineMinimal.h"
#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/Reflection/ReflectionValidation.h"

namespace sw
{
    /** @brief `Count` 가 있는 열거형입니다 — 범위는 [Idle, Run] 이다. */
    ENUM()
    enum class LayoutSampleMode : uint8
    {
        Idle,
        Crouch,
        Run,
        Count,
    };

    /** @brief 비트 플래그입니다 — 등록 비트는 0 · 2 번이다. */
    ENUM( Flags )
    enum class LayoutSampleFlags : uint8
    {
        None    = 0,
        Visible = BitUtil::bit( 0 ),
        Solid   = BitUtil::bit( 2 ),
    };

    /** @brief 펼쳐지는 중첩 타입입니다. 범위가 바깥 타입의 필드(`_limit`)라 혼자서는 스키마이 없고 `LayoutSample` 안에서만 풀린다. */
    REFLECT()
    struct LayoutSamplePoint
    {
        REFLECT_BODY();

        PROPERTY( Min = -_limit, Max = _limit )
        int32 _offset = 0;

        PROPERTY()
        float32 _weight = 0.0f;
    };

    /** @brief 검증 함수가 있어 펼치지 않는 타입입니다 — 부모 표는 이 스키마을 의존으로 부른다. 체력 13 은 검증 실패다. */
    REFLECT( ReadValidate = validateHealth )
    struct LayoutSampleStat
    {
        REFLECT_BODY();

        PROPERTY( Min = 0, Max = 1000 )
        int32 _health = 100;

        void validateHealth( ValidationContext& context ) const
        {
            if ( _health == 13 )
                context.addError( "unlucky health" );
        }
    };

    /** @brief 기반 클래스입니다. 파생 스키마이 먼저 쓴다. */
    REFLECT()
    struct LayoutSampleBase
    {
        REFLECT_BODY();

        PROPERTY()
        uint16 _id = 0;
    };

    /** @brief 서술자 칸 종류를 모두 쓰는 샘플입니다. 필드 차례가 와이어 차례다 — 손 인코딩(`TestLayoutSerializer.cpp`)과 같은 차례로 둔다. */
    REFLECT()
    struct LayoutSample : public LayoutSampleBase
    {
        REFLECT_BODY();

        LayoutSample()
            : _bFirst{ 0 }
            , _bSecond{ 0 }
        {
        }

        PROPERTY()
        bool _bEnabled = false; // NOLINT(sw-bool-flag): 스키마의 bool 칸(비트필드가 아닌 것)을 보는 테스트 칸이다

        PROPERTY()
        uint8 _bFirst : 1;

        PROPERTY()
        uint8 _bSecond : 1;

        PROPERTY( Min = 0, Max = 100 )
        int32 _percent = 0;

        PROPERTY( Min = 0 )
        int64 _score = 0;

        PROPERTY( Min = 1, Max = 64 )
        int32 _limit = 8;

        PROPERTY( Max = _limit )
        uint32 _used = 0;

        PROPERTY()
        float32 _raw = 0.0f;

        PROPERTY( Min = -1, Max = 1 )
        float32 _bounded = 0.0f;

        PROPERTY( Min = 0, Max = 10, Step = 0.5 )
        float32 _quantized = 0.0f;

        PROPERTY( Min = 0 )
        float64 _time = 0.0;

        PROPERTY( Min = -10, Max = 10 )
        float3 _position{ 0.0f, 0.0f, 0.0f };

        PROPERTY()
        LayoutSampleMode _mode = LayoutSampleMode::Idle;

        PROPERTY()
        LayoutSampleFlags _flags = LayoutSampleFlags::None;

        PROPERTY( MaxCount = 16 )
        string _name;

        PROPERTY( MaxCount = 4 )
        vector<int16> _listValue;

        PROPERTY()
        array<uint8, 3> _arrByte{};

        PROPERTY()
        LayoutSamplePoint _point;

        PROPERTY( MaxCount = 2 )
        vector<LayoutSampleStat> _listStat;
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 복제 스키마 샘플입니다(`ReplicatedLayoutTest`). `Replicated` 칸(범위 정수 · 양자화 실수 · bool · 범위 벡터)과 복제하지 않는 칸이 섞였다.
     * @details 길이 없는 vector 칸이 있어 타입 전체 스키마은 적격이 아니다 — 복제 스키마는 그래도 생긴다(그 칸은 `Replicated` 가 아니다).
     */
    REFLECT()
    struct ReplicatedLayoutSample
    {
        REFLECT_BODY();

        PROPERTY( Replicated, Min = 0, Max = 100 )
        int32 _health = 100;

        PROPERTY()
        string _debugName = "local"; ///< 복제하지 않는다

        PROPERTY( Replicated, Min = -100, Max = 100, Step = 0.01 )
        float32 _speed = 0.0f;

        PROPERTY( Replicated )
        bool _bAlive = true;

        PROPERTY( Replicated, Min = -1000, Max = 1000 )
        float3 _position{ 0.0f, 0.0f, 0.0f };

        PROPERTY()
        vector<int32> _listCache; ///< 길이가 없어 전체 스키마은 적격이 아니다
    };
} // namespace sw

namespace sw
{
    /** @brief `array<int32, 3>` 칸 하나입니다 — `LayoutEnumArraySample` 와 바이트를 견준다(`LayoutSerializerTest.EnumArrayMatchesStaticArrayBytes`). */
    REFLECT()
    struct LayoutPlainArraySample
    {
        REFLECT_BODY();

        PROPERTY()
        array<int32, 3> _arrValue{};
    };

    /** @brief 열거자로 색인하는 배열 칸입니다 — 머리(스키마 해시) 뒤의 본문은 원소 수가 같은 고정 배열과 한 비트도 다르지 않아야 한다. 해시는 타입 · 칸 철자를 섞어 다르다. */
    REFLECT()
    struct LayoutEnumArraySample
    {
        REFLECT_BODY();

        PROPERTY()
        EnumArray<LayoutSampleMode, int32> _arrValue{};
    };
} // namespace sw
