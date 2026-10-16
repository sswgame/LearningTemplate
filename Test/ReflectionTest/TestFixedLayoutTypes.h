/**
 * @file TestFixedLayoutTypes.h
 * @brief 고정 배치형(B) 코덱 테스트용 타입입니다. `REFLECT` · `PROPERTY` · `ENUM` 선언만 있고 손 코덱 · 비교 함수는 없습니다.
 * @details 세 표본(스칼라만, 컨테이너 · 열거형, 중첩)과 스키마 변경 거부 · 적격 판정을 보는 짝 타입들입니다(`TestFixedLayout.cpp`).
 */
#pragma once
#include "Core/Common/StdHeaders.h"
#include "Core/Container/array.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Math/VectorMath.h"

#include "Engine/Reflection/ReflectionCore.h"

namespace sw
{
    // ------------------------------------------------------------------------------
    // 1) 스칼라만
    // ------------------------------------------------------------------------------
    REFLECT()
    struct FixedScalarSample
    {
        REFLECT_BODY();
        PROPERTY()
        bool _bAlive = false;
        PROPERTY()
        uint8 _bFlag : 1;
        PROPERTY( Min = 0, Max = 100 )
        int32 _hp = 0;
        PROPERTY( Min = -500, Max = 500 )
        int16 _delta = 0;
        PROPERTY()
        uint16 _ammo = 0;
        PROPERTY()
        int64 _score = 0;
        PROPERTY( Min = 0 )
        int32 _seed = 0;
        PROPERTY()
        float32 _speed = 0.0f;
        PROPERTY( Min = 1, Max = 1000 )
        float32 _halfSize = 1.0f;
        PROPERTY( Step = 0.5, Min = "-_halfSize", Max = "_halfSize" )
        float32 _offset = 0.0f;
        PROPERTY( Step = 0.01, Min = 0, Max = 1 )
        float32 _ratio = 0.0f;
        PROPERTY()
        float64 _time = 0.0;
    };
} // namespace sw

namespace sw
{
    // ------------------------------------------------------------------------------
    // 2) 컨테이너 · 열거형
    // ------------------------------------------------------------------------------
    ENUM( Count = Count )
    enum class EFixedSampleKind : uint8
    {
        Idle,
        Walk,
        Run,
        Count,
    };

    ENUM( Flags )
    enum class EFixedSampleFlag : uint8
    {
        None   = 0,
        Hidden = 1,
        Frozen = 2,
        Marked = 4,
    };

    REFLECT()
    struct FixedContainerSample
    {
        REFLECT_BODY();
        PROPERTY()
        EFixedSampleKind _kind = EFixedSampleKind::Idle;
        PROPERTY( MaxCount = 4, Min = 0, Max = 1000 )
        vector<int32> _listValue;
        PROPERTY()
        EFixedSampleFlag _flags = EFixedSampleFlag::None;
        PROPERTY( MaxCount = 16 )
        string _name;
        PROPERTY( Min = -8, Max = 8 )
        int32 _arrSlot[3] = {};
        PROPERTY()
        array<float32, 2> _arrWeight = {};
        PROPERTY( Step = 0.01, Min = "-10, 0, -10", Max = "10, 5, 10" )
        float3 _position;
        PROPERTY( MaxCount = 8 )
        vector<EFixedSampleKind> _listKind;
        PROPERTY( MaxCount = 8 )
        vector<bool> _listBit;
    };
} // namespace sw

namespace sw
{
    // ------------------------------------------------------------------------------
    // 3) 중첩 — 같은 헤더의 공개 타입은 펼치고, 기반이 있는 타입은 실행 시간에 그 코덱을 찾는다
    // ------------------------------------------------------------------------------
    REFLECT()
    struct FixedInnerSample
    {
        REFLECT_BODY();
        PROPERTY( Min = 1, Max = 100 )
        float32 _halfExtent = 1.0f;
        PROPERTY( Step = 0.1, Min = "-_halfExtent", Max = "_halfExtent" )
        float32 _x = 0.0f;
        PROPERTY( Min = 0, Max = 255 )
        int32 _id = 0;
    };
} // namespace sw

namespace sw
{
    REFLECT()
    struct FixedInnerBaseSample
    {
        REFLECT_BODY();
        PROPERTY()
        int32 _baseValue = 0;
    };
} // namespace sw

namespace sw
{
    REFLECT()
    struct FixedInnerDerivedSample : public FixedInnerBaseSample
    {
        REFLECT_BODY();
        PROPERTY()
        int32 _derivedValue = 0;
    };
} // namespace sw

namespace sw
{
    /** @brief 범위가 바깥 타입의 필드다 — 혼자서는 코덱이 없고, 펼치는 부모가 이름을 푼다. */
    REFLECT()
    struct FixedPointSample
    {
        REFLECT_BODY();
        PROPERTY( Step = 0.25, Min = "-_arenaHalfSize", Max = "_arenaHalfSize" )
        float32 _y = 0.0f;
    };
} // namespace sw

namespace sw
{
    REFLECT()
    struct FixedNestedSample
    {
        REFLECT_BODY();
        PROPERTY( Min = 1, Max = 1000 )
        float32 _arenaHalfSize = 10.0f;
        PROPERTY()
        FixedInnerSample _inner;
        PROPERTY( MaxCount = 3 )
        vector<FixedInnerSample> _listInner;
        PROPERTY()
        FixedInnerDerivedSample _derived;
        PROPERTY( MaxCount = 4 )
        vector<FixedPointSample> _listPoint;
        PROPERTY( Transient )
        int32 _transientCache = 0;
    };
} // namespace sw

namespace sw
{
    // ------------------------------------------------------------------------------
    // 4) 스키마 변경 거부 — 한 가지만 다른 짝
    // ------------------------------------------------------------------------------
    REFLECT()
    struct FixedOrderASample
    {
        REFLECT_BODY();
        PROPERTY()
        int32 _first = 0;
        PROPERTY()
        int32 _second = 0;
    };
} // namespace sw

namespace sw
{
    REFLECT()
    struct FixedOrderBSample
    {
        REFLECT_BODY();
        PROPERTY()
        int32 _second = 0;
        PROPERTY()
        int32 _first = 0;
    };
} // namespace sw

namespace sw
{
    REFLECT()
    struct FixedNameASample
    {
        REFLECT_BODY();
        PROPERTY()
        int32 _value = 0;
    };
} // namespace sw

namespace sw
{
    REFLECT()
    struct FixedNameBSample
    {
        REFLECT_BODY();
        PROPERTY()
        int32 _amount = 0;
    };
} // namespace sw

namespace sw
{
    ENUM()
    enum class EFixedColorA : uint8
    {
        Red,
        Green,
    };

    ENUM()
    enum class EFixedColorB : uint8
    {
        Red,
        Green,
        Blue,
    };

    REFLECT()
    struct FixedEnumASample
    {
        REFLECT_BODY();
        PROPERTY()
        EFixedColorA _color = EFixedColorA::Red;
    };
} // namespace sw

namespace sw
{
    REFLECT()
    struct FixedEnumBSample
    {
        REFLECT_BODY();
        PROPERTY()
        EFixedColorB _color = EFixedColorB::Red;
    };
} // namespace sw

namespace sw
{
    REFLECT()
    struct FixedStepASample
    {
        REFLECT_BODY();
        PROPERTY( Step = 0.1, Min = 0, Max = 1 )
        float32 _value = 0.0f;
    };
} // namespace sw

namespace sw
{
    REFLECT()
    struct FixedStepBSample
    {
        REFLECT_BODY();
        PROPERTY( Step = 0.2, Min = 0, Max = 1 )
        float32 _value = 0.0f;
    };
} // namespace sw

namespace sw
{
    REFLECT()
    struct FixedMinASample
    {
        REFLECT_BODY();
        PROPERTY( Min = 0, Max = 10 )
        int32 _value = 0;
    };
} // namespace sw

namespace sw
{
    REFLECT()
    struct FixedMinBSample
    {
        REFLECT_BODY();
        PROPERTY( Min = 1, Max = 10 )
        int32 _value = 0;
    };
} // namespace sw

namespace sw
{
    // ------------------------------------------------------------------------------
    // 5) 적격이 아닌 타입 — 코덱 대신 이유가 등록된다
    // ------------------------------------------------------------------------------
    REFLECT()
    struct FixedPointerSample
    {
        REFLECT_BODY();
        PROPERTY()
        int32 _count = 0;
        PROPERTY( MaxCount = 4 )
        vector<FixedInnerBaseSample*> _listOwned; ///< 자기 기술형은 소유 포인터 원소를 싣지만 고정 배치형 표에는 포인터가 없다
    };
} // namespace sw

namespace sw
{
    REFLECT()
    struct FixedUnboundedVectorSample
    {
        REFLECT_BODY();
        PROPERTY()
        vector<int32> _listValue;
    };
} // namespace sw
