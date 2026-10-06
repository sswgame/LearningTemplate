/**
 * @file IntentTrackControllerComponent.h
 * @brief 기록 조종자 — 의도 목록(`.swintent` 트랙 · 시나리오 `<Intent>`)을 틱 순서로 냅니다. AI 조종자와 같은 자리입니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/ComponentHandle.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/String/hashed_string.h"

#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Base/Control/ControlIntent.h"
#include "GameFramework/Base/Control/ControllerComponent.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class ControlIntentHistory;

    /**
     * @class IntentTrackControllerComponent
     * @brief 의도 목록을 틱마다 하나씩 냅니다(조종 회전도 기록 값). 목록이 끝나면 `_bReleaseWhenDone` 이면 다음 틱 첫머리에 원래 조종자에게 폰을 돌려주고,
     *        아니면 멈춘 채(이동 0 · 조종 회전 유지) 쥐고 있습니다.
     * @details 재생은 기록과 같은 씬을 처음부터 같은 고정 프레임 시간으로 돌릴 때만 같은 궤적입니다(`ControlIntentHistory`). 기록 값은 이미 양자화돼 있어
     *          조종 시스템의 `quantize` 를 다시 거쳐도 그대로다. 언리얼 리플레이 스펙테이터 · 유니티 `InputTestFixture` 의 행동 층 판입니다.
     */
    REFLECT( Category = "Control", DisplayName = "Intent Track Controller", Tooltip = "Plays recorded control intents (a .swintent track or a scenario step) tick by tick" )
    class SW_GF_API IntentTrackControllerComponent : public ControllerComponent
    {
    public:
        REFLECT_BODY();

        IntentTrackControllerComponent();
        ~IntentTrackControllerComponent() override = default;

        /** @brief 쥘 폰이 있으면(`Possess At Start`) `Track File` 의 트랙을 싣습니다. */
        void onBeginPlay() override;

        void produceIntent( const ControlFrameContext& context, const PawnComponent& pawn, ControlIntent& outIntent ) final;

        /**
         * @brief @p pawn 을 쥐고 @p listIntent 를 다음 틱부터 냅니다. @p bReturnWhenDone 이면 끝난 뒤 지금 쥔 조종자에게 돌려줍니다
         *        (이미 이 조종자가 쥔 폰이면 처음 돌려줄 조종자를 그대로 둔다).
         */
        void play( PawnComponent& pawn, const vector<ControlIntent>& listIntent, bool bReturnWhenDone );
        /** @brief 목록만 바꿉니다(처음부터). */
        void setTrack( const vector<ControlIntent>& listIntent );
        /** @brief @p history 에서 폰 이름 @p pawnName 의 트랙을 싣습니다(처음부터). 없거나 빈 틱이 있으면 false 이고 목록은 빕니다. */
        [[nodiscard]] bool loadTrack( const ControlIntentHistory& history, const hashed_string& pawnName );
        /** @brief 아직 낼 의도가 남았는가입니다. */
        bool   isPlaying() const { return _cursor < _listIntent.size(); }
        uint32 getPlayedCount() const { return static_cast<uint32>( _cursor ); }
        uint32 getTrackLength() const { return static_cast<uint32>( _listIntent.size() ); }
        bool   isReleaseWhenDone() const { return _bReleaseWhenDone; }
        void   setReleaseWhenDone( bool bReleaseWhenDone ) { _bReleaseWhenDone = bReleaseWhenDone; }

    private:
        PROPERTY( Category = "Track", DisplayName = "Track File", Tooltip = "Intent recording (.swintent) to play when play starts; empty: a script or scenario fills the track" )
        string _trackFile;
        PROPERTY( Category = "Track", DisplayName = "Track Name", Tooltip = "Pawn name of the track to play; empty: the possessed pawn's object name" )
        hashed_string _trackName;
        PROPERTY( Category = "Track", DisplayName = "Release When Done", Tooltip = "Give the pawn back to its previous controller when the track ends" )
        bool _bReleaseWhenDone;

        vector<ControlIntent> _listIntent;
        ComponentHandle       _returnController; ///< 끝나면 폰을 돌려줄 조종자(무효면 놓기만)
        size_t                _cursor;           ///< 다음에 낼 의도
        bool                  _bReturnPending;   ///< 목록이 끝나면 돌려주기를 걸어야 한다(한 번)
    };
} // namespace sw
