/**
 * @file GameFlags.h
 * @brief 월드 상태 플래그와 그 위의 작은 조건식을 정의합니다.
 * @details 플래그는 이름에서 정수로 가는 표이고 bool 은 0 과 1 이다. 조건식은 `hasKey && !doorOpen`, `count>=3` 같은 모양이다.
 *          지역 잠금(`AreaGraph`), 대화 분기, 퀘스트 조건이 같은 조건식을 읽는다.
 *          세이브는 이름 순 목록으로 내보내므로 늘 같은 순서로 적힌다.
 */
#pragma once
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/Encoding/FourCCUtil.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Name/hashed_string.h"

#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/Serialization/Format/StateIO.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class Archive;
    class ConditionExpr;
} // namespace sw

namespace sw
{
    /** @brief 세이브용 플래그 하나입니다. 세이브 타입이 `vector<GameFlagEntry>` PROPERTY 로 싣고, `GameFlags::fillEntries` 가 이름 순으로 채웁니다. */
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
     * @brief 이름에서 정수로 가는 플래그 표입니다. 값이 0 인 플래그는 없는 것과 같으므로 0 을 넣으면 지웁니다.
     * @details 조건식은 `ConditionExpr` 이다. 그냥 이름이 플래그 값을 읽으므로(`flag` 조건) `hasKey && count>=3` 이 플래그 식이다. 읽을 때 컴파일하므로 여기는 글을 받지 않는다.
     *          리비전은 값이 실제로 바뀔 때마다 오른다. 조건 결과를 캐시하는 쪽이 다시 셀 때를 알 수 있다.
     */
    class SW_GF_API GameFlags
    {
    public:
        static constexpr uint32 kStateTag     = FourCCUtil::make( "FLAG" );
        static constexpr uint32 kStateVersion = 1;

        GameFlags();

        /** @brief 값을 정합니다. 0 이면 지웁니다. */
        void setFlag( const hashed_string& name, int32 value = 1 );
        /** @brief 값을 더하고 새 값을 반환합니다. */
        int32 addFlag( const hashed_string& name, int32 delta );
        /** @brief 값입니다. 0 은 "지움" 과 같은 뜻이라 없는 플래그는 0 입니다(값 0 을 따로 저장하지 않는다). */
        int32 getFlag( const hashed_string& name ) const;
        /** @brief 0 이 아닌 값이 있으면 true 입니다. */
        bool hasFlag( const hashed_string& name ) const;
        /** @brief 지웁니다. 있었으면 true 입니다. */
        bool clearFlag( const hashed_string& name );
        void clear();

        /** @brief 컴파일된 조건식을 이 플래그로 평가합니다. 빈 식은 참입니다. */
        bool evaluate( const ConditionExpr& expression ) const;

        /** @brief 세이브용 목록을 이름 순(대소문자 무시 사전순)으로 채웁니다. */
        void fillEntries( vector<GameFlagEntry>& outListEntry ) const;
        /** @brief 세이브에서 복원합니다. 지금 값은 모두 지웁니다. */
        void restoreEntries( const vector<GameFlagEntry>& listEntry );
        /**
         * @brief 플래그를 이름 순으로 씁니다. 같은 플래그면 넣은 순서와 상관없이 같은 바이트가 나옵니다.
         *        `writeState` 의 바이트로 모두 바꿉니다. 깨졌으면 false 를 반환하고 상태는 그대로입니다.
         */
        SW_DECLARE_STATE_IO_IN_PLACE();

        uint32 getRevision() const { return _revision; }
        size_t getCount() const { return _mapFlag.size(); }

    private:
        unordered_map<hashed_string, int32> _mapFlag;
        uint32                              _revision;
    };
} // namespace sw
