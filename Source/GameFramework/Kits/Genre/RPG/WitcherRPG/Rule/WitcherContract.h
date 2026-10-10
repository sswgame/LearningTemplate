/**
 * @file WitcherContract.h
 * @brief 계약 — 위쳐 감각으로 단서 찾기(조사 지점 · 발견 순서 · 단계의 단서를 모두 찾으면 다음 단계, 기반 `QuestLog` 에 진행 알림)와 보상 흥정(분노 게이지 — 너무 올리면 결렬)입니다.
 */
#pragma once
#include "Core/Common/FourCcUtil.h"
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/Math/Math.h"
#include "Core/String/hashed_string.h"

#include "GameFramework/Base/Foundation/Utility/EventBuffer.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    struct WitcherClueDef;
    struct WitcherContractDef;

    class Archive;
    class QuestLog;
    class WitcherCatalog;

    /** @brief 조사 결과입니다. */
    enum class WitcherClueResult : uint8
    {
        Found = 0,
        UnknownClue, ///< 지금 단계의 단서가 아니다(다른 단계 · 없는 단서)
        AlreadyFound,
        OutOfOrder, ///< 앞 순서의 단서를 먼저 찾아야 한다
        TooFar,
        Solved ///< 계약의 조사가 이미 끝났다
    };

    SW_GF_API const utf8* toString( WitcherClueResult result );

    /** @brief 조사에 생긴 일입니다. */
    struct WitcherInvestigationEvent
    {
        enum class Kind : uint8
        {
            ClueFound = 0,
            StepCompleted, ///< _stepID — 다음 단계로
            Solved         ///< 마지막 단계까지 끝났다
        };
        hashed_string _stepID{};
        hashed_string _clueID{};
        Kind          _kind{ Kind::ClueFound };
    };
} // namespace sw

namespace sw
{
    /**
     * @class WitcherInvestigation
     * @brief 계약 하나의 조사입니다. 단서를 찾으면 일지에 `notify( "Clue", 단서 id )`, 단계를 끝내면 `notify( "Investigate", 단계 id )` 를 보냅니다 —
     *        퀘스트 단계의 목표가 그것을 세게 하면 계약 퀘스트가 넘어갑니다. 일지는 빌려 씁니다(없어도 된다).
     */
    class SW_GF_API WitcherInvestigation
    {
    public:
        static constexpr uint32      kStateTag       = FourCcUtil::make( "WINV" );
        static constexpr uint32      kStateVersion   = 1;
        static constexpr const utf8* kClueNotifyKind = "Clue";
        static constexpr const utf8* kStepNotifyKind = "Investigate";

        WitcherInvestigation();

        /** @brief 계약으로 시작합니다. 계약이 없으면 false 입니다. */
        [[nodiscard]] bool initialize( const WitcherCatalog* pCatalog, const hashed_string& contractID, QuestLog* pQuestLog );
        /** @brief 위쳐 감각 — @p position 에서 @p senseRadius 안의, 지금 찾을 수 있는(순서가 열린) 아직 못 찾은 단서입니다. */
        void senseClues( const float3& position, float32 senseRadius, vector<const WitcherClueDef*>& outListClue ) const;
        /** @brief @p position 에서 단서를 조사합니다. */
        WitcherClueResult investigate( const hashed_string& clueID, const float3& position );

        bool          isClueFound( const hashed_string& clueID ) const;
        bool          isSolved() const;
        int32         getStepIndex() const { return _stepIndex; }
        hashed_string getStepID() const;
        void          drainEvents( vector<WitcherInvestigationEvent>& outListEvent );
        /** @brief 계약 id · 단계 · 지금 단계에서 찾은 단서를 씁니다. 빌린 퀘스트 일지 · 카탈로그는 싣지 않는다. */
        void writeState( Archive& outArchive ) const;
        /** @brief `writeState` 의 바이트로 바꿉니다. 계약이 카탈로그에 없거나 단계가 계약 밖이거나 깨졌으면 false 이고 그대로입니다. */
        [[nodiscard]] bool readState( Archive& archive );

    private:
        /** @brief 지금 단계에서 아직 못 찾은 단서의 가장 작은 순서입니다. 다 찾았으면 −1 입니다. */
        int32 computeOpenOrder() const;
        bool  isFoundInternal( const hashed_string& clueID ) const;

        vector<hashed_string>                  _listFound; ///< 지금 단계에서 찾은 단서
        EventBuffer<WitcherInvestigationEvent> _eventBuffer;
        const WitcherCatalog*                  _pCatalog; ///< 상태 바이트의 계약 id 를 찾는다
        const WitcherContractDef*              _pContract;
        QuestLog*                              _pQuestLog;
        int32                                  _stepIndex;
    };
} // namespace sw

namespace sw
{
    /** @brief 흥정 결과입니다. */
    enum class WitcherHaggleResult : uint8
    {
        Accepted = 0, ///< 받아들였다 — 흥정 끝
        Countered,    ///< 새 제안을 냈다(`getOffer`)
        BrokenOff,    ///< 화가 나 흥정을 끝냈다 — 처음 보상만 준다
        Closed        ///< 이미 끝났다
    };

    SW_GF_API const utf8* toString( WitcherHaggleResult result );

    /**
     * @class WitcherHaggle
     * @brief 의뢰인과의 보상 흥정입니다(결정적). 요구가 지금 제안 이하이거나 의뢰인의 한도(처음 보상 × 한도 배율) 이하이면 받아들입니다. 넘으면
     *        분노가 (한 번마다 몫 + 한도를 넘긴 몫 / 처음 보상 × 분노 배율)만큼 오르고, 최대에 닿으면 결렬 — 처음 보상만 줍니다. 아니면 제안을 요구 쪽으로
     *        반 올립니다(한도까지).
     */
    class SW_GF_API WitcherHaggle
    {
    public:
        static constexpr uint32 kStateTag     = FourCcUtil::make( "WHAG" );
        static constexpr uint32 kStateVersion = 1;

        WitcherHaggle();

        [[nodiscard]] bool  initialize( const WitcherCatalog* pCatalog, const hashed_string& contractID );
        WitcherHaggleResult propose( int32 askReward );
        /** @brief 지금 제안을 그대로 받습니다. */
        int32 acceptOffer();

        int32   getOffer() const { return _offer; }
        float32 getAnger() const { return _anger; }
        /** @brief 끝났으면 받을 보상입니다(끝나지 않았으면 0). */
        int32 getFinalReward() const { return _finalReward; }
        bool  isClosed() const { return _bClosed != SW_FALSE; }
        /** @brief 계약 id · 화 · 지금 제안 · 받을 보상 · 끝났는지를 씁니다. 카탈로그는 싣지 않는다. */
        void writeState( Archive& outArchive ) const;
        /** @brief `writeState` 의 바이트로 바꿉니다. 계약이 카탈로그에 없거나 깨졌으면 false 이고 그대로입니다. */
        [[nodiscard]] bool readState( Archive& archive );

    private:
        const WitcherCatalog*     _pCatalog; ///< 상태 바이트의 계약 id 를 찾는다
        const WitcherContractDef* _pContract;
        float32                   _anger;
        int32                     _offer;
        int32                     _finalReward;
        uint8                     _bClosed;
    };
} // namespace sw
