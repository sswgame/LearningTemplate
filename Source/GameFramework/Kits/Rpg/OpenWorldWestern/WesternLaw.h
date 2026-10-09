/**
 * @file WesternLaw.h
 * @brief 법 집행 — 범죄 목격(시야 콜백) · 신고까지의 시간 · 목격자 처치/위협 · 지역별 현상금과 수배 단계 · 시간과 변장에 따른 감쇠 · 현상금 지불 · 보안관 추적 단계입니다.
 */
#pragma once
#include "Core/Common/FourCcUtil.h"
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/Math/Math.h"
#include "Core/String/hashed_string.h"

#include "GameFramework/Base/Actor/AI/AiPerception.h"
#include "GameFramework/Base/Foundation/Utility/EventBuffer.h"
#include "GameFramework/Base/Foundation/Utility/Time/Countdown.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    struct WesternPursuitDef;

    class Archive;
    class NavGrid;
    class Wallet;
    class WesternCatalog;

    /** @brief 범죄를 볼 수 있는 사람 하나입니다(게임이 범죄 때마다 근처 후보를 넘긴다). */
    struct WesternWitness
    {
        float3 _position{};
        float3 _forward{ 0.0f, 0.0f, 1.0f };
        uint64 _id{ 0 };
        uint8  _bLawman{ SW_FALSE }; ///< 보안관 — 보면 그 자리에서 신고된 것과 같다
    };
} // namespace sw

namespace sw
{
    /**
     * @class IWesternWitnessSight
     * @brief 목격자가 범죄 자리를 보았는가를 게임이 답합니다(시야각 · 가림 · 밤 · 안개).
     */
    class SW_GF_API IWesternWitnessSight
    {
    public:
        IWesternWitnessSight()          = default;
        virtual ~IWesternWitnessSight() = default;

        IWesternWitnessSight( const IWesternWitnessSight& )            = default;
        IWesternWitnessSight& operator=( const IWesternWitnessSight& ) = default;

        virtual bool canWitnessSee( const WesternWitness& witness, const float3& crimePosition ) const = 0;
    };
} // namespace sw

namespace sw
{
    /**
     * @class WesternPerceptionSight
     * @brief 기반 `AiPerception` 의 시야(거리 · 시야각 · 격자 가림)로 답하는 기본 시야입니다.
     */
    class SW_GF_API WesternPerceptionSight : public IWesternWitnessSight
    {
    public:
        WesternPerceptionSight( const AiPerceptionSettings& settings, const NavGrid* pGrid );

        bool canWitnessSee( const WesternWitness& witness, const float3& crimePosition ) const override;

    private:
        AiPerception   _perception;
        const NavGrid* _pGrid;
    };
} // namespace sw

namespace sw
{
    /** @brief 현상금 지불 결과입니다. */
    enum class WesternPayResult : uint8
    {
        Ok = 0,
        UnknownRegion,
        NoBounty,
        ActivelyWanted, ///< 쫓기는 중에는 우체국에서 낼 수 없다
        NotEnoughMoney
    };

    SW_GF_API const utf8* toString( WesternPayResult result );

    /** @brief 법 쪽에 생긴 일입니다. */
    struct WesternLawEvent
    {
        enum class Kind : uint8
        {
            Witnessed = 0,   ///< 누가 보았다(신고 대기 — 시민)
            Unwitnessed,     ///< 아무도 못 보았다
            Reported,        ///< 신고됐다 — 현상금 · 수배가 오른다(_value = 지금 현상금)
            ReportPrevented, ///< 목격자가 모두 처치 · 위협돼 신고되지 않는다
            WantedRaised,    ///< _value = 수배 단계
            WantedLowered,   ///< 법을 따돌렸다(_value = 수배 단계)
            BountyPaid,      ///< _value = 낸 돈
            PursuitChanged   ///< 추적 단계가 바뀌었다(_value = 수배 단계)
        };
        hashed_string _regionId{};
        hashed_string _crimeId{};
        uint64        _witnessId{ 0 };
        uint32        _incidentId{ 0 };
        int32         _value{ 0 };
        Kind          _kind{ Kind::Witnessed };
    };
} // namespace sw

namespace sw
{
    /**
     * @class WesternLawState
     * @brief 플레이어 한 명의 법적 처지입니다. 시간은 `update`(초) · `advanceDay`(날) 로만 흐릅니다(결정적).
     * @details 범죄 하나가 사건 하나입니다. 보안관이 보았으면 바로 신고, 시민이 보았으면 그 범죄의 신고 시간 뒤에 신고됩니다(목격자가 여럿이면
     *          가장 먼저 다다른 한 명이 신고하고 나머지는 지운다). 신고 전에 목격자를 처치 · 위협하면(`silenceWitness`) 그 목격자의 신고는 지워지고,
     *          사건의 목격자가 모두 사라지면 그 범죄는 없던 일입니다. 수배 단계는 법에 보이지 않는 동안(`setSeenByLaw( false )`) 지역의 식는 시간마다
     *          하나씩 내려가고, 변장하면 그만큼 빨리 식습니다.
     */
    class SW_GF_API WesternLawState
    {
    public:
        static constexpr uint32 kStateTag     = FourCcUtil::make( "WLAW" );
        static constexpr uint32 kStateVersion = 1;

        WesternLawState();

        void initialize( const WesternCatalog* pCatalog );
        /**
         * @brief 범죄를 저지릅니다. 후보 중 @p sight 가 보았다고 한 사람만 목격자입니다.
         * @return 사건 번호(1 부터). 범죄 · 지역이 없으면 0 입니다. 목격자가 없어도 번호는 붙습니다(명예는 게임이 바꾼다).
         */
        uint32 commitCrime( const hashed_string& crimeId, const hashed_string& regionId, const float3& position, const vector<WesternWitness>& listCandidate,
                            const IWesternWitnessSight& sight, bool bMasked );
        /** @brief 목격자를 처치 · 위협했습니다. 지운 신고 대기가 있으면 true 입니다(이미 신고했으면 늦었다). */
        bool silenceWitness( uint64 witnessId );
        /** @brief 법이 지금 플레이어를 보는가입니다(보이는 동안 수배가 식지 않는다). */
        void setSeenByLaw( const hashed_string& regionId, bool bSeen );
        /** @brief 옷을 갈아입었습니다(변장 — 수배가 빨리 식는다). 새 범죄가 신고되면 풀립니다. */
        void setDisguised( bool bDisguised ) { _bDisguised = bDisguised ? SW_TRUE : SW_FALSE; }
        void update( float32 deltaTime );
        /** @brief 하루가 지났습니다 — 현상금이 지역마다 줄어듭니다. */
        void advanceDay();
        /** @brief 현상금을 내고 그 지역의 기록을 지웁니다. */
        WesternPayResult payBounty( const hashed_string& regionId, Wallet& inoutWallet );

        int32 getBounty( const hashed_string& regionId ) const;
        int32 getWantedLevel( const hashed_string& regionId ) const;
        /** @brief 지금 그 지역의 보안관 추적입니다(수배가 없으면 nullptr). */
        const WesternPursuitDef* findPursuit( const hashed_string& regionId ) const;
        /** @brief 신고를 기다리는 목격자 수입니다. */
        int32 countPendingReports() const { return static_cast<int32>( _listPending.size() ); }
        bool  isDisguised() const { return _bDisguised != SW_FALSE; }
        void  drainEvents( vector<WesternLawEvent>& outListEvent );
        /** @brief 지역 기록(현상금 · 수배 · 안 보인 시간 · 법이 보는지) · 신고 대기(남은 시간까지) · 다음 사건 번호 · 변장을 씁니다. 카탈로그는 싣지 않는다. */
        void writeState( Archive& outArchive ) const;
        /** @brief `writeState` 의 바이트로 바꿉니다. 모르는 지역 · 범죄거나 깨졌으면 false 이고 그대로입니다. */
        [[nodiscard]] bool readState( Archive& archive );

    private:
        struct RegionRecord
        {
            hashed_string _regionId{};
            float32       _unseenTime{ 0.0f };
            int32         _bounty{ 0 };
            int32         _wantedLevel{ 0 };
            uint8         _bSeenByLaw{ SW_FALSE };
        };

        struct PendingReport
        {
            hashed_string _crimeId{};
            hashed_string _regionId{};
            uint64        _witnessId{ 0 };
            uint32        _incidentId{ 0 };
            Countdown     _remaining{};
            uint8         _bMasked{ SW_FALSE };
        };

        RegionRecord*       findRecordMutable( const hashed_string& regionId );
        const RegionRecord* findRecord( const hashed_string& regionId ) const;
        RegionRecord&       acquireRecord( const hashed_string& regionId );
        void                applyReport( const PendingReport& report );
        void                setWantedLevel( RegionRecord& record, int32 wantedLevel );
        void                pushEvent( WesternLawEvent::Kind kind, const hashed_string& regionId, int32 value, uint32 incidentId = 0,
                                       const hashed_string& crimeId = hashed_string{}, uint64 witnessId = 0 );

        vector<RegionRecord>         _listRecord;
        vector<PendingReport>        _listPending;
        EventBuffer<WesternLawEvent> _eventBuffer;
        const WesternCatalog*        _pCatalog;
        uint32                       _nextIncidentId;
        uint8                        _bDisguised;
    };
} // namespace sw
