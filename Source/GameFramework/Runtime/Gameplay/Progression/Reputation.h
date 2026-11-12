/**
 * @file Reputation.h
 * @brief 평판과 호감도입니다. 세력(또는 NPC)마다 값, 범위, 단계를 가지며, 연결된 세력(돕는 쪽의 적은 미워함)이 있고, 하루마다 기준값으로 돌아갑니다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Encoding/FourCCUtil.h"
#include "Core/Event/EventBuffer.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Name/hashed_string.h"

#include "Engine/Asset/Cache/SharedAssetCache.h"
#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/Serialization/Format/StateIO.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Runtime/Foundation/GameData/GameCatalog.h"
#include "GameFramework/Runtime/Foundation/GameData/XMLCatalog.h"

namespace sw
{
    class Archive;
    class XMLNode;

    /** @brief 단계 하나입니다. 값이 `_minValue` 이상이면 이 단계입니다. */
    REFLECT()
    struct SW_GF_API ReputationTier
    {
        REFLECT_BODY();

        PROPERTY( Required )
        hashed_string _name{};
        PROPERTY( Name = "min" )
        int32 _minValue{ 0 }; ///< 적지 않으면 세력의 하한(읽기가 채운 뒤 읽는다) — 세력 범위 안인지는 읽기가 본다
    };
} // namespace sw

namespace sw
{
    /** @brief 다른 세력으로 번지는 비율입니다. */
    REFLECT()
    struct SW_GF_API ReputationLink
    {
        REFLECT_BODY();

        PROPERTY( Name = "faction", Required )
        hashed_string _factionID{};
        PROPERTY( Required )
        float32 _ratio{ 0.0f }; ///< 이 세력의 변화에 ratio 를 곱한 값이 그 세력에 적용된다. 적대 관계이면 음수이다. 빠진 비율을 0 으로 두지 않는다
    };
} // namespace sw

namespace sw
{
    /** @brief 세력이나 NPC 하나입니다. */
    REFLECT()
    struct SW_GF_API FactionDef
    {
        REFLECT_BODY();

        PROPERTY( Required )
        hashed_string _id{};
        PROPERTY()
        string                 _name{};     ///< 적지 않으면 id
        vector<ReputationTier> _listTier{}; ///< 낮은 값부터 나열한다(`<Tier>`)
        vector<ReputationLink> _listLink{}; ///< `<Link>`
        PROPERTY( Name = "min" )
        int32 _minValue{ -1000 };
        PROPERTY( Name = "max", Min = "_minValue" )
        int32 _maxValue{ 1000 };
        PROPERTY( Name = "start", Min = "_minValue", Max = "_maxValue" )
        int32 _startValue{ 0 };
        PROPERTY( Name = "decay", Min = 0 )
        int32 _dailyDecay{ 0 }; ///< 하루마다 시작값 쪽으로 돌아가는 양. 호감도가 식는다
    };
} // namespace sw

namespace sw
{
    /**
     * @class ReputationCatalog
     * @brief `<ReputationCatalog><Faction id="town" min="-1000" max="1000" start="0" decay="0"><Tier name="Hated" min="-1000"/>
     *        <Tier name="Neutral" min="-100"/><Link faction="bandits" ratio="-0.5"/></Faction></ReputationCatalog>` 를 읽습니다.
     */
    class SW_GF_API ReputationCatalog : public GameCatalog<FactionDef, XMLCatalog<ReputationCatalog>>
    {
        friend class XMLCatalog<ReputationCatalog>;

    public:
        /** @brief 데이터 종류 이름입니다(`SW_ASSET_TYPE` 등록 줄과 팩 경로 `GamePackDataUtil::tryMakeKindPath` 가 이것을 쓴다). */
        static constexpr const utf8* kDataKind = "friendship";

    private:
        static constexpr const utf8* kXMLRootName = "ReputationCatalog"; ///< 루트 원소(`XMLCatalog`)
        uint32                       loadRoot( const XMLCatalogRoot& root, string_view sourceName );
        [[nodiscard]] bool           loadFaction( const XMLNode& node, string_view sourceName );
    };

    /** @brief 공유 캐시는 이 카탈로그의 .cpp 가 인스턴스화합니다. 쓰는 쪽은 사본을 읽지 않고 `SharedAssetCache<ReputationCatalog>::acquire` 로 한 벌을 받습니다. */
    SW_DECLARE_SHARED_ASSET_CACHE( SW_GF_API_EXTERN_TEMPLATE, ReputationCatalog );
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
        static constexpr uint32 kStateTag     = FourCCUtil::make( "REPU" );
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
        void  pollEvents( vector<ReputationEvent>& outListEvent );
        /**
         * @brief 세력마다 id 와 값을 처음 만난 순서로 씁니다.
         *        `writeState` 가 쓴 바이트로 모두 복원합니다. 알림은 없습니다. 세력 id 가 비어 있으면 false 를 반환하고 상태는 바뀌지 않습니다.
         */
        SW_DECLARE_STATE_IO();

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

    private:
        vector<Entry>                _listEntry;
        EventBuffer<ReputationEvent> _eventBuffer;
        const ReputationCatalog*     _pCatalog;
    };
} // namespace sw
