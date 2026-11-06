/**
 * @file GameConditions.h
 * @brief 게임플레이 문맥의 칸과, 어느 실행기든 쓸 수 있는 기본 어휘입니다. 조건 `flag` · `tag` · `chance` · `bodyShape` · `bodyType`, 동작 `flag.set` · `flag.add` · `event` 를 등록합니다.
 * @details 행동 트리 · 스케줄 · 대화 · 장착 · 외형이 같은 이름을 쓴다. 칸이 없으면 조건은 거짓(값은 0), 동작은 실패다 — 실행기는 아는 칸만 채운다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/String/FourCcUtil.h"
#include "Core/String/TagID.h"
#include "Core/String/hashed_string.h"

#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/Vocabulary/ActionRegistry.h"
#include "Engine/Vocabulary/ConditionExpr.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class GameFlags;
    class TagContainer;

    /** @brief 조건이 읽는 월드 플래그입니다. */
    struct GameFlagsSlot
    {
        using Type                      = const GameFlags;
        static constexpr uint32 kSlotID = FourCcUtil::make( "FLAG" );
    };

    /** @brief 동작이 쓰는 월드 플래그입니다. 읽기 칸과 나눈 것은 스케줄처럼 읽기만 빌린 실행기가 있어서입니다. */
    struct GameFlagsWriteSlot
    {
        using Type                      = GameFlags;
        static constexpr uint32 kSlotID = FourCcUtil::make( "FLGW" );
    };

    /** @brief 조건을 묻는 주체의 태그입니다(NPC · 캐릭터). */
    struct SubjectTagsSlot
    {
        using Type                      = const TagContainer;
        static constexpr uint32 kSlotID = FourCcUtil::make( "TGSB" );
    };

    /** @brief 세계 태그입니다(축제 중 · 경보). */
    struct WorldTagsSlot
    {
        using Type                      = const TagContainer;
        static constexpr uint32 kSlotID = FourCcUtil::make( "TGWD" );
    };

    /** @brief 주체의 체형입니다(`bodyShape`). */
    struct BodyShapeSlot
    {
        using Type                      = const hashed_string;
        static constexpr uint32 kSlotID = FourCcUtil::make( "BSHP" );
    };

    /** @brief 주체의 몸 종류입니다(`bodyType`). */
    struct BodyTypeSlot
    {
        using Type                      = const hashed_string;
        static constexpr uint32 kSlotID = FourCcUtil::make( "BTYP" );
    };
} // namespace sw

namespace sw
{
    /** @brief 태그 하나를 받는 조건의 칸입니다(`tag(Quest.Done)`). 주체나 세계가 그 태그(하위 태그 포함)를 가지면 참입니다. */
    REFLECT()
    struct SW_GF_API ConditionTagArg
    {
        REFLECT_BODY();

        PROPERTY( Required )
        TagID _tag{};
    };

    // 동작 칸 이름은 행동 트리 노드의 속성(`id` · `name` · `task`)과 겹치지 않게 짓는다 — 칸이 곧 그 노드의 XML 속성이다.

    /** @brief `flag.set` 의 칸입니다. 값 0 은 지우기입니다(`GameFlags` 규칙). */
    REFLECT()
    struct SW_GF_API GameFlagSetParams
    {
        REFLECT_BODY();

        PROPERTY( Required, Tooltip = "Flag name" )
        hashed_string _flag{};
        PROPERTY( Tooltip = "Value; 0 clears the flag" )
        int32 _value{ 1 };
    };

    /** @brief `flag.add` 의 칸입니다. */
    REFLECT()
    struct SW_GF_API GameFlagAddParams
    {
        REFLECT_BODY();

        PROPERTY( Required, Tooltip = "Flag name" )
        hashed_string _flag{};
        PROPERTY( Tooltip = "Amount to add" )
        int32 _delta{ 1 };
    };

    /** @brief `event` 의 칸입니다. 실행기의 알림(대화의 `bindOnDialogueEvent`)으로만 나가고 세계를 바꾸지 않습니다. */
    REFLECT()
    struct SW_GF_API GameEventParams
    {
        REFLECT_BODY();

        PROPERTY( Required, Tooltip = "Name the game listens for" )
        hashed_string _event{};
    };
} // namespace sw

namespace sw
{
    /**
     * @struct GameConditions
     * @brief 실행기가 공통으로 쓰는 도우미입니다.
     */
    struct SW_GF_API GameConditions
    {
        /** @brief 확률 조건의 씨앗을 만듭니다. 같은 두 값이면 같은 씨앗입니다. 스케줄은 (날, 키), 행동 트리 · 대화는 (진행 번호, 노드)를 넘깁니다. */
        [[nodiscard]] static uint32 makeSeed( int32 first, int32 second );
    };
} // namespace sw
