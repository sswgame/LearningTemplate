/**
 * @file SurvivalHorrorStage.h
 * @brief 생존 공포 한 채의 배치(데이터)입니다. 시작 · 목표 방, 어두운 구역, 줍기 · 문서 · 퍼즐 · 타자기 · 아이템 상자 · 괴물의 자리를 정합니다.
 * @details 자리는 절대 자리(월드 원점 0 기준)의 XZ 평면이고 방은 방 그래프(`AreaGraph`)의 것이다. 무엇을 하는지는 카탈로그 id 와 종류가 정한다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/Math.h"
#include "Core/Name/hashed_string.h"

#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class AreaGraph;
    class SurvivalHorrorCatalog;

    /** @brief 쓸 수 있는 자리의 종류입니다. */
    ENUM()
    enum class SurvivalHorrorSpotKind : uint8
    {
        Pickup = 0,   ///< 아이템 `_id` 를 `_count` 개 줍는다
        Document,     ///< 문서 `_id` 를 읽는다
        SequenceStep, ///< 순서 퍼즐 `_id` 의 걸음 `_answer` 를 누른다
        DialLock,     ///< 다이얼 자물쇠 `_id` — 단서 `_clue` 가 있어야 번호를 안다
        Deduction,    ///< 추리 `_id` 를 `_answer` 로 낸다(가진 단서는 이어 준다)
        SavePoint,    ///< 세이브한다
        ItemBox,      ///< 비었으면 가방의 첫 물건을 넣고, 들었으면 첫 물건을 꺼낸다
        Monster       ///< 괴물 `_id` — 방에 들면 목격하고 쓰면 싸운다
    };

    /** @brief 쓸 수 있는 자리 하나입니다. */
    REFLECT()
    struct SW_KIT_API SurvivalHorrorSpot
    {
        REFLECT_BODY();

        PROPERTY()
        hashed_string _id{};
        PROPERTY()
        hashed_string _area{};
        PROPERTY()
        hashed_string _answer{}; ///< 순서 퍼즐의 걸음 · 추리의 답
        PROPERTY()
        hashed_string _clue{}; ///< 다이얼 번호를 아는 단서(비면 늘 안다)
        PROPERTY( Units = m )
        float2 _position{ 0.0f, 0.0f };
        PROPERTY( Min = 1 )
        int32 _count{ 1 };
        PROPERTY()
        SurvivalHorrorSpotKind _kind{ SurvivalHorrorSpotKind::Pickup };
    };
} // namespace sw

namespace sw
{
    /** @brief 집 한 채입니다. */
    REFLECT()
    struct SW_KIT_API SurvivalHorrorStage
    {
        REFLECT_BODY();

        PROPERTY()
        hashed_string _startArea{};
        PROPERTY()
        hashed_string _goalArea{}; ///< 이 방에 들면 빠져나간 것이다
        PROPERTY()
        hashed_string _darkRegion{}; ///< 방 그래프에서 어두운 지역(`Area region`)
        PROPERTY()
        vector<SurvivalHorrorSpot> _listSpot{};

        /** @brief 방과 카탈로그 id 가 모두 있는지 봅니다. 틀린 것을 모두 알리고 하나라도 있으면 false 입니다. */
        [[nodiscard]] bool validate( const SurvivalHorrorCatalog& catalog, const AreaGraph& graph ) const;
    };
} // namespace sw
