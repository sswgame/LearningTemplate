/**
 * @file ServerBusTestTypes.h
 * @brief 서버 버스 혼합 빌드 테스트의 본문 타입입니다(EngineTest 리플렉션 단계가 코드젠합니다).
 * @details A 스트림에는 타입 FQN 이 없고 필드를 이름 해시로 짝지으므로, 필드 이름이 겹치는 두 타입으로 쓰고 읽으면 옛 빌드와 새 빌드 사이를 흉내 냅니다
 *          (docs/plans/ServerBusFormat.md 6절). 중첩 타입은 두 빌드가 같은 것을 씁니다(기록 타입 해시가 그 이름입니다).
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    /** @brief 모르는 열거자 테스트의 열거형입니다. */
    ENUM()
    enum class BusTestColor : uint8
    {
        Red = 0,
        Green,
        Blue
    };
} // namespace sw

namespace sw
{
    /** @brief 두 빌드가 함께 쓰는 중첩 원소입니다. */
    REFLECT()
    struct BusTestItem
    {
        REFLECT_BODY();
        PROPERTY()
        int32 _id{ 0 };
    };
} // namespace sw

namespace sw
{
    /** @brief 옛 빌드의 본문입니다. */
    REFLECT()
    struct BusTestNoticeOld
    {
        REFLECT_BODY();
        static constexpr uint32 kBusVersion = 1;
        PROPERTY( MaxCount = 16 )
        string _label{};
        PROPERTY( MaxCount = 4 )
        vector<BusTestItem> _listItem{};
        PROPERTY()
        uint64 _accountID{ 0 };
        PROPERTY()
        BusTestColor _color{ BusTestColor::Red };
    };
} // namespace sw

namespace sw
{
    /** @brief 새 빌드의 본문입니다. 필드 `_extra` 가 하나 더 있습니다(의미 버전은 같습니다). */
    REFLECT()
    struct BusTestNoticeNew
    {
        REFLECT_BODY();
        static constexpr uint32 kBusVersion = 1;
        PROPERTY( MaxCount = 16 )
        string _label{};
        PROPERTY( MaxCount = 4 )
        vector<BusTestItem> _listItem{};
        PROPERTY()
        uint64 _accountID{ 0 };
        PROPERTY()
        BusTestColor _color{ BusTestColor::Red };
        PROPERTY()
        int32 _extra{ 0 };
    };
} // namespace sw

namespace sw
{
    /** @brief 뜻이 바뀌어 의미 버전을 올린 빌드의 본문입니다. 필드는 옛 빌드와 같습니다. */
    REFLECT()
    struct BusTestNoticeNextMeaning
    {
        REFLECT_BODY();
        static constexpr uint32 kBusVersion = 2;
        PROPERTY( MaxCount = 16 )
        string _label{};
        PROPERTY( MaxCount = 4 )
        vector<BusTestItem> _listItem{};
        PROPERTY()
        uint64 _accountID{ 0 };
        PROPERTY()
        BusTestColor _color{ BusTestColor::Red };
    };
} // namespace sw

namespace sw
{
    /** @brief `MaxCount` 를 넓힌 빌드의 본문입니다. 옛 빌드는 16 자를 넘는 글을 범위 밖으로 거절합니다. */
    REFLECT()
    struct BusTestNoticeWideLabel
    {
        REFLECT_BODY();
        static constexpr uint32 kBusVersion = 1;
        PROPERTY( MaxCount = 64 )
        string _label{};
        PROPERTY()
        uint64 _accountID{ 0 };
    };
} // namespace sw
