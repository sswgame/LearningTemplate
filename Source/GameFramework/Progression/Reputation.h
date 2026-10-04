/**
 * @file Reputation.h
 * @brief 평판 · 호감도 — 세력(또는 NPC)마다 값 · 범위 · 단계, 연결된 세력(돕는 쪽의 적은 미워한다), 하루마다 기준값으로 돌아가기입니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/String/hashed_string.h"

#include "GameFramework/Data/GameCatalog.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class XmlNode;

    /** @brief 단계 하나 — 값이 `_minValue` 이상이면 이 단계입니다. */
    struct ReputationTier
    {
        hashed_string _name{};
        int32         _minValue{ 0 };
    };
} // namespace sw

namespace sw
{
    /** @brief 다른 세력에 번지는 몫입니다. */
    struct ReputationLink
    {
        hashed_string _factionId{};
        float32       _ratio{ 0.0f }; ///< 이 세력 변화 × ratio 가 그 세력에(적대면 음수)
    };
} // namespace sw

namespace sw
{
    /** @brief 세력 · NPC 하나입니다. */
    struct FactionDef
    {
        hashed_string          _id{};
        string                 _name{};
        vector<ReputationTier> _listTier{}; ///< 낮은 값부터
        vector<ReputationLink> _listLink{};
        int32                  _minValue{ -1000 };
        int32                  _maxValue{ 1000 };
        int32                  _startValue{ 0 };
        int32                  _dailyDecay{ 0 }; ///< 하루마다 시작값 쪽으로(호감도가 식는다)
    };
} // namespace sw

namespace sw
{
    /**
     * @class ReputationCatalog
     * @brief `<ReputationCatalog><Faction id="town" min="-1000" max="1000" start="0" decay="0"><Tier name="Hated" min="-1000"/>
     *        <Tier name="Neutral" min="-100"/><Link faction="bandits" ratio="-0.5"/></Faction></ReputationCatalog>` 를 읽습니다.
     */
    class SW_GF_API ReputationCatalog
    {
    public:
        [[nodiscard]] bool loadFromResource( string_view path );
        [[nodiscard]] bool loadFromXmlText( string_view xmlText, string_view sourceName = {} );
        void               addFaction( const FactionDef& faction ) { (void)_catalog.add( faction ); }

        const FactionDef*         findFaction( const hashed_string& id ) const { return _catalog.find( id ); }
        const vector<FactionDef>& getFactions() const { return _catalog.getAll(); }

    private:
        uint32 loadRoot( const XmlNode& root, string_view sourceName );

        GameCatalog<FactionDef> _catalog{};
    };
} // namespace sw

namespace sw
{
    /** @brief 단계가 바뀐 일입니다. */
    struct ReputationEvent
    {
        hashed_string _factionId{};
        hashed_string _oldTier{};
        hashed_string _newTier{};
        int32         _value{ 0 };
    };
} // namespace sw

namespace sw
{
    /**
     * @class ReputationState
     * @brief 한 플레이어의 세력별 값입니다. 카탈로그에 없는 id 도 받습니다(범위 0..1000, 단계 없음 — 이름 없는 NPC 의 호감도).
     */
    class SW_GF_API ReputationState
    {
    public:
        ReputationState();

        void initialize( const ReputationCatalog* pCatalog );
        /** @brief 값을 바꾸고 연결된 세력에도 번지게 합니다(한 단계만 — 서로 연결돼도 되풀이되지 않는다). 실제로 바뀐 값입니다. */
        int32 changeValue( const hashed_string& factionId, int32 delta );
        void  setValue( const hashed_string& factionId, int32 value );
        /** @brief 하루가 지났습니다 — 식는 세력을 시작값 쪽으로. */
        void advanceDay();

        int32         getValue( const hashed_string& factionId ) const;
        hashed_string getTierName( const hashed_string& factionId ) const;
        /**
         * @brief 단계 번호(0 = 가장 낮은)입니다. 단계가 없으면 −1 입니다.
         * @details 값이 가장 낮은 단계의 `min` 보다 작아도 0 입니다 — 단계는 바닥 없이 가장 낮은 것으로 떨어진다("단계 밖" 은 없다).
         */
        int32 getTierIndex( const hashed_string& factionId ) const;
        void  drainEvents( vector<ReputationEvent>& outListEvent );

    private:
        struct Entry
        {
            hashed_string _factionId{};
            int32         _value{ 0 };
        };

        Entry& acquireEntry( const hashed_string& factionId );
        int32  applyDelta( const hashed_string& factionId, int32 delta );
        /** @brief `getTierIndex` 의 계산 — 값 이하인 마지막 단계, 그런 단계가 없으면 0(가장 낮은 단계), 단계가 없으면 −1. */
        static int32 computeTierIndex( const FactionDef* pFaction, int32 value );

        vector<Entry>            _listEntry;
        vector<ReputationEvent>  _listEvent;
        const ReputationCatalog* _pCatalog;
    };
} // namespace sw
