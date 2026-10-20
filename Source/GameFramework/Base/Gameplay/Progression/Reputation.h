/**
 * @file Reputation.h
 * @brief 평판과 호감도입니다. 세력(또는 NPC)마다 값, 범위, 단계를 가지며, 연결된 세력(돕는 쪽의 적은 미워함)이 있고, 하루마다 기준값으로 돌아갑니다.
 */
#pragma once
#include "Core/Common/FourCcUtil.h"
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/EventBuffer.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/String/hashed_string.h"

#include "GameFramework/Base/Foundation/Data/GameCatalog.h"
#include "GameFramework/Base/Foundation/Data/XMLCatalog.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class Archive;
    class XMLNode;

    /** @brief 단계 하나입니다. 값이 `_minValue` 이상이면 이 단계입니다. */
    struct ReputationTier
    {
        hashed_string _name{};
        int32         _minValue{ 0 };
    };
} // namespace sw

namespace sw
{
    /** @brief 다른 세력으로 번지는 비율입니다. */
    struct ReputationLink
    {
        hashed_string _factionID{};
        float32       _ratio{ 0.0f }; ///< 이 세력의 변화에 ratio 를 곱한 값이 그 세력에 적용된다. 적대 관계이면 음수이다
    };
} // namespace sw

namespace sw
{
    /** @brief 세력이나 NPC 하나입니다. */
    struct FactionDef
    {
        hashed_string          _id{};
        string                 _name{};
        vector<ReputationTier> _listTier{}; ///< 낮은 값부터 나열한다
        vector<ReputationLink> _listLink{};
        int32                  _minValue{ -1000 };
        int32                  _maxValue{ 1000 };
        int32                  _startValue{ 0 };
        int32                  _dailyDecay{ 0 }; ///< 하루마다 시작값 쪽으로 돌아가는 양. 호감도가 식는다
    };
} // namespace sw

namespace sw
{
    /**
     * @class ReputationCatalog
     * @brief `<ReputationCatalog><Faction id="town" min="-1000" max="1000" start="0" decay="0"><Tier name="Hated" min="-1000"/>
     *        <Tier name="Neutral" min="-100"/><Link faction="bandits" ratio="-0.5"/></Faction></ReputationCatalog>` 를 읽습니다.
     */
    class SW_GF_API ReputationCatalog : public XMLCatalog<ReputationCatalog>, public GameCatalog<FactionDef>
    {
        friend class XMLCatalog<ReputationCatalog>;

    public:
        /** @brief 데이터 종류 이름입니다(`SW_RESOURCE_DATA_KIND` 등록 줄과 팩 경로 `GamePackDataUtil::tryMakeKindPath` 가 이것을 쓴다). */
        static constexpr const utf8* kDataKind = "friendship";

    private:
        static constexpr const utf8* kXMLRootName = "ReputationCatalog"; ///< 루트 원소(`XMLCatalog`)
        uint32                       loadRoot( const XMLNode& root, string_view sourceName );
    };
} // namespace sw

namespace sw
{
    /** @brief 단계가 바뀐 변화입니다. */
    struct ReputationEvent
    {
        hashed_string _factionID{};
        hashed_string _oldTier{};
        hashed_string _newTier{};
        int32         _value{ 0 };
    };
} // namespace sw

namespace sw
{
    /**
     * @class ReputationState
     * @brief 한 플레이어의 세력별 값입니다. 카탈로그에 없는 id 도 받으며, 이때 범위는 0..1000 이고 단계는 없습니다. 이름 없는 NPC 의 호감도에 씁니다.
     */
    class SW_GF_API ReputationState
    {
    public:
        static constexpr uint32 kStateTag     = FourCcUtil::make( "REPU" );
        static constexpr uint32 kStateVersion = 1;

        ReputationState();

        void initialize( const ReputationCatalog* pCatalog );
        /** @brief 값을 바꾸고 연결된 세력에도 번지게 합니다. 번짐은 한 단계만 일어나므로 서로 연결되어 있어도 반복되지 않습니다. 실제로 바뀐 값을 반환합니다. */
        int32 changeValue( const hashed_string& factionID, int32 delta );
        void  setValue( const hashed_string& factionID, int32 value );
        /** @brief 하루가 지났을 때 부릅니다. 식는 세력을 시작값 쪽으로 되돌립니다. */
        void advanceDay();

        int32         getValue( const hashed_string& factionID ) const;
        hashed_string getTierName( const hashed_string& factionID ) const;
        /**
         * @brief 단계 번호입니다. 0 이 가장 낮은 단계이고, 단계가 없으면 -1 입니다.
         * @details 값이 가장 낮은 단계의 `min` 보다 작아도 0 이다. 단계에는 하한이 없어서 가장 낮은 단계로 떨어지며, "단계 밖" 은 없다.
         */
        int32 getTierIndex( const hashed_string& factionID ) const;
        void  drainEvents( vector<ReputationEvent>& outListEvent );
        /** @brief 세력마다 id 와 값을 처음 만난 순서로 씁니다. */
        void writeState( Archive& outArchive ) const;
        /** @brief `writeState` 가 쓴 바이트로 모두 복원합니다. 알림은 없습니다. 세력 id 가 비어 있으면 false 를 반환하고 상태는 바뀌지 않습니다. */
        [[nodiscard]] bool readState( Archive& archive );

    private:
        struct Entry
        {
            hashed_string _factionID{};
            int32         _value{ 0 };
        };

        Entry& acquireEntry( const hashed_string& factionID );
        int32  applyDelta( const hashed_string& factionID, int32 delta );
        /** @brief `getTierIndex` 의 계산입니다. 값 이하인 마지막 단계를 고르고, 그런 단계가 없으면 0(가장 낮은 단계)이며, 단계가 아예 없으면 -1 입니다. */
        static int32 computeTierIndex( const FactionDef* pFaction, int32 value );

        vector<Entry>                _listEntry;
        EventBuffer<ReputationEvent> _eventBuffer;
        const ReputationCatalog*     _pCatalog;
    };
} // namespace sw
