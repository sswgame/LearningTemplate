/**
 * @file GameFlags.h
 * @brief 월드 상태 플래그 — 이름 → 정수(bool 은 0/1)와 그 위의 작은 조건식(`hasKey && !doorOpen`, `count>=3`)입니다.
 * @details 지역 잠금(`AreaGraph`) · 대화 분기 · 퀘스트 조건이 같은 조건식을 읽습니다. 세이브는 이름 순 목록으로 내보내 늘 같은 순서로 적힙니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/String/hashed_string.h"

#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    /** @brief 세이브용 플래그 하나입니다. 세이브 타입이 `vector<GameFlagEntry>` PROPERTY 로 싣습니다(`GameFlags::fillEntries` 가 이름 순으로 채운다). */
    REFLECT()
    struct SW_GF_API GameFlagEntry
    {
        REFLECT_BODY();

        PROPERTY()
        hashed_string _name{};
        PROPERTY()
        int32 _value{ 0 };
    };
} // namespace sw

namespace sw
{
    /**
     * @class GameFlags
     * @brief 이름 → 정수 플래그입니다. 값이 0 인 플래그는 없는 것과 같습니다(0 을 넣으면 지운다).
     * @details 조건식 문법(우선순위 낮은 것부터): `a || b`, `a && b`, `!a`, 비교 `count>=3` (`== != < <= > >=`, 오른쪽은 정수나 다른 플래그),
     *          괄호, 플래그 이름 하나(0 이 아니면 참), 정수 하나. 빈 식은 참입니다(조건 없음). 잘못된 식은 경고하고 거짓입니다.
     *          리비전은 값이 실제로 바뀔 때마다 오릅니다 — 조건 결과를 캐시하는 쪽이 다시 셀 때를 압니다.
     */
    class SW_GF_API GameFlags
    {
    public:
        static constexpr int32 kMaxConditionDepth = 32; ///< 괄호 · `!` 중첩 상한(잘못된 식이 스택을 다 쓰지 않게)

        GameFlags();

        /** @brief 값을 정합니다. 0 이면 지웁니다. */
        void setFlag( const hashed_string& name, int32 value = 1 );
        /** @brief 값을 더하고 새 값을 돌려줍니다. */
        int32 addFlag( const hashed_string& name, int32 delta );
        int32 getFlag( const hashed_string& name, int32 fallback = 0 ) const;
        /** @brief 0 이 아닌 값이 있는가입니다. */
        bool hasFlag( const hashed_string& name ) const;
        /** @brief 지웁니다. 있었으면 true 입니다. */
        bool clearFlag( const hashed_string& name );
        void clear();

        /** @brief 조건식을 이 플래그로 평가합니다. 잘못된 식은 경고하고 false 입니다. */
        bool evaluate( string_view expression ) const;
        /**
         * @brief 조건식을 읽고 @p flags 로 평가합니다.
         * @return 식이 문법에 맞으면 true(결과는 @p outResult). 틀리면 경고하고 false, @p outResult 도 false 입니다.
         */
        [[nodiscard]] static bool parseCondition( string_view expression, const GameFlags& flags, bool& outResult );

        /** @brief 세이브용 — 이름 순(대소문자 무시 사전순) 목록입니다. */
        void fillEntries( vector<GameFlagEntry>& outListEntry ) const;
        /** @brief 세이브에서 되살립니다(지금 값은 모두 지운다). */
        void restoreEntries( const vector<GameFlagEntry>& listEntry );

        uint32 getRevision() const { return _revision; }
        size_t getCount() const { return _mapFlag.size(); }

    private:
        unordered_map<hashed_string, int32> _mapFlag;
        uint32                              _revision;
    };
} // namespace sw
