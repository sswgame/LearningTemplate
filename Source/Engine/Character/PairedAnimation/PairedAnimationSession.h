/**
 * @file PairedAnimationSession.h
 * @brief 페어드 애니메이션 세션입니다. 에셋(`PairedAnimationDef`)의 역할마다 참가자(애니메이터)를 묶어 한 시계로 재생하고, 정렬 · 공유 알림 · 끝 · 중단을 맡습니다.
 * @details 설계는 `docs/plans/PairedAnimation.md`. 요점은 이렇다.
 *          - **시계**: 앵커 역할 플레이어의 시각 하나가 정본이다. 세션은 그것을 읽고, 이번 프레임 평가가 갈 시각(`시각 + dt × 속도`)을 예측해 공유 알림을
 *            그 구간에서 낸다. 다음 프레임에 앵커 시각이 예측과 다르면 오류로 중단한다(조용히 맞추지 않는다).
 *          - **맞춤**: 역할은 같은 프레임 · 속도 1 로 각자 `play` 한다. 매 프레임 역할 시각 = 앵커 시각 − 진입 때 앵커 시각인지 검사한다.
 *          - **정렬**: `Snap` 은 진입 때 트랜스폼을 맞추고, `Warp` 는 같은 오브젝트의 `MotionWarpingComponent` 에 목표 · 창을 넣는다(목표는 매 프레임 앵커에서 다시 구함).
 *          - **스레드**: 시작 · 갱신 · 중단은 모두 게임 스레드, 틱 밖(`PairedAnimationSystem` 의 프레임 시스템 단계)에서 부른다. 애니메이션 평가는 세션을 읽지 않는다.
 *          - **잃음**: 필수 역할(앵커 포함)의 애니메이터가 사라지거나 다른 것을 재생하면 중단, 선택 역할이면 그 역할만 빠진다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/ComponentHandle.h"
#include "Core/Container/vector.h"
#include "Core/Math/Math.h"
#include "Core/Memory/Memory.h"
#include "Core/String/hashed_string.h"

namespace sw
{
    struct PairedAnimationRoleDef;

    class GameObjectManager;
    class IAnimationPlayable;
    class PairedAnimationDef;
    class SkeletalAnimatorComponent;

    /**
     * @enum PairedAnimationStartResult
     * @brief 세션 시작의 결과입니다. `Started` 밖은 아무것도 바꾸지 않은 거절입니다(모두 검사한 뒤에 바꾼다).
     */
    enum class PairedAnimationStartResult : uint8
    {
        Started = 0,
        AssetNotLoaded,      ///< 에셋을 읽지 못했다(읽기 오류는 로더가 남겼다)
        InvalidBinding,      ///< 모르는 역할, 같은 역할 두 번, 같은 애니메이터 두 번(데이터 · 코드 결함, 오류 로그)
        MissingRequiredRole, ///< 필수 역할의 바인딩이 없다(오류 로그)
        ParticipantMissing,  ///< 바인딩한 애니메이터가 없다(이미 사라짐)
        ParticipantBusy,     ///< 참가자가 다른 세션에 있다(`PairedAnimationSystem` 이 판정)
        SkeletonMismatch,    ///< 역할이 요구한 스켈레톤이 아니다(오류 로그)
        ClipNotFound,        ///< 역할 클립을 그 애니메이터의 클립 폴더에서 못 읽었다(오류 로그)
        ClipLengthMismatch,  ///< 앵커 클립 길이가 타임라인 길이와 다르거나, 역할 클립이 타임라인 · 워프 창을 넘는다(오류 로그)
        WarpingUnavailable,  ///< `warp` 역할의 오브젝트에 `MotionWarpingComponent` 가 없다(오류 로그)
        NotifyTargetMissing, ///< 공유 알림을 받을 역할의 알림 표에 그 항목이 없다(오류 로그)
        TooFarToAlign,       ///< 진입 조건 — 정렬할 역할이 목표에서 `maxAlignDistance` 보다 멀다(게임플레이의 정상 거절)
    };

    /** @enum PairedAnimationSessionState @brief 세션의 상태입니다. */
    enum class PairedAnimationSessionState : uint8
    {
        Idle = 0, ///< 아직 시작하지 않았거나 시작이 거절됐다
        Playing,
        Finished, ///< 앵커 클립이 끝까지 갔다
        Aborted,  ///< 필수 역할을 잃었거나, 시각이 어긋났거나, 누가 중단했다
    };
} // namespace sw

namespace sw
{
    /** @brief 역할 하나에 묶을 참가자입니다. 애니메이터 핸들입니다(feat14 의 `TComponentHandle<SkeletalAnimatorComponent>` 가 들어오면 그 형식으로 바꾼다). */
    struct PairedAnimationBinding
    {
        hashed_string   _role{};
        ComponentHandle _animator{};
    };
} // namespace sw

namespace sw
{
    /**
     * @class PairedAnimationSession
     * @brief 세션 하나입니다. 파일 머리말 참고.
     */
    class SW_API PairedAnimationSession
    {
    public:
        /** @brief 시각 비교의 허용치(초)입니다. 같은 dt 의 같은 덧셈이면 0 이고, 늦은 진입의 뺄셈 오차만 덮는다. */
        static constexpr float32 kTimeToleranceSeconds = 1.0e-3f;
        /** @brief `Warp` 창이 닫힐 때 목표에 닿았다고 보는 거리(m)입니다. 넘으면 오류 로그(루트 모션을 뽑지 않는 애니메이터 등)입니다. */
        static constexpr float32 kWarpReachToleranceMeters = 1.0e-2f;

        PairedAnimationSession( uint32 sessionID, shared_ptr<const PairedAnimationDef> def );

        /**
         * @brief 참가자를 묶고 시작합니다(게임 스레드, 틱 밖). 먼저 모두 검사하고, 통과해야 재생 · 정렬을 바꿉니다.
         * @param listBinding 역할 → 애니메이터. 선택 역할은 빠져도 된다.
         */
        [[nodiscard]] PairedAnimationStartResult start( GameObjectManager& manager, const vector<PairedAnimationBinding>& listBinding );
        /** @brief 한 프레임을 갱신합니다(게임 스레드, 애니메이션 평가 앞). @p deltaSeconds 는 이번 프레임 평가에 넘길 그 값입니다. */
        void update( GameObjectManager& manager, float32 deltaSeconds );
        /** @brief 중단합니다. 남은 역할은 종료 상태로 블렌드합니다. 재생 중이 아니면 할 일이 없습니다. */
        void abort( GameObjectManager& manager );

        uint32                      getID() const { return _sessionID; }
        PairedAnimationSessionState getState() const { return _state; }
        bool                        isPlaying() const { return _state == PairedAnimationSessionState::Playing; }
        /** @brief 마지막으로 읽은 공유 타임라인 시각(앵커 시각, 초)입니다. */
        float32                   getTime() const { return _anchorTime; }
        const PairedAnimationDef& getDef() const { return *_def; }
        /** @brief 그 애니메이터가 이 세션에 묶여 아직 나가지 않았으면 true 입니다. */
        bool hasParticipant( ComponentHandle animator ) const;
        /** @brief 지금 재생 중인(들어왔고 나가지 않은) 역할 수입니다. */
        uint32 getPlayingRoleCount() const;
        /** @brief 이름의 역할이 지금 재생 중이면 true 입니다. */
        bool isRolePlaying( const hashed_string& roleName ) const;

    private:
        /** @brief 역할 하나의 실행 단계입니다. */
        enum class RoleStage : uint8
        {
            Unbound = 0, ///< 바인딩이 없는 선택 역할
            Waiting,     ///< 바인딩은 있고 `start` 를 기다린다
            Playing,
            Exited,
        };

        /** @brief 역할 하나의 실행 상태입니다(에셋의 역할과 인덱스가 같다). */
        struct RoleState
        {
            ComponentHandle           _animator{};
            const IAnimationPlayable* _pClip{ nullptr }; ///< 비교에만 쓴다(애니메이터의 클립 표가 소유)
            float32                   _clipSeconds{ 0.0f };
            float32                   _entryAnchorTime{ 0.0f };
            float32                   _previousPlayRate{ 1.0f };
            RoleStage                 _stage{ RoleStage::Unbound };
            bool                      _bWarpOpen{ false };
        };

        /** @brief 역할의 애니메이터를 풉니다. 없으면 nullptr 입니다. */
        SkeletalAnimatorComponent* resolveAnimator( GameObjectManager& manager, const RoleState& role ) const;
        /** @brief 애니메이터가 이 역할의 클립을 재생 중이면(현재 슬롯, 크로스페이드 중이면 다음 슬롯) 그 시각을 쓰고 true 입니다. */
        [[nodiscard]] static bool tryGetClipTime( const SkeletalAnimatorComponent& animator, const RoleState& role, float32& outTime );
        /** @brief 앵커 루트에서 역할의 정렬 목표(자리 · 요)를 구합니다. 앵커 루트가 없으면 false 입니다. */
        [[nodiscard]] bool tryComputeAlignTarget( GameObjectManager& manager, const PairedAnimationRoleDef& roleDef, float3& outPosition, float32& outYaw ) const;
        /** @brief 진입 조건(정렬 거리)을 봅니다. 맞추지 않는 역할이면 늘 true 입니다. */
        bool isWithinAlignDistance( GameObjectManager& manager, uint32 roleIndex ) const;
        /** @brief 역할을 들여보냅니다 — 속도 1, `play`, 정렬. */
        void enterRole( GameObjectManager& manager, uint32 roleIndex, float32 anchorTime );
        /** @brief 역할을 내보냅니다 — 아직 세션 클립을 재생 중이면 종료 상태로 블렌드, 속도 되돌림, 워프 창 닫기. */
        void exitRole( GameObjectManager& manager, uint32 roleIndex );
        /** @brief `Warp` 목표를 앵커에서 다시 구해 넣습니다. */
        void refreshWarpTarget( GameObjectManager& manager, uint32 roleIndex );
        /** @brief `Warp` 창을 닫고 목표에 닿았는지 봅니다. */
        void closeWarpWindow( GameObjectManager& manager, uint32 roleIndex, bool bCheckReached );
        /** @brief 공유 알림 하나를 그 역할 오브젝트의 알림 표로 보냅니다. */
        void fireNotify( GameObjectManager& manager, uint32 notifyIndex );
        /** @brief 남은 역할을 모두 내보내고 상태를 정합니다. */
        void end( GameObjectManager& manager, PairedAnimationSessionState state );
        /** @brief 역할을 잃었습니다. 필수면 세션을 중단하고 true, 선택이면 그 역할만 빼고 false 입니다. */
        bool loseRole( GameObjectManager& manager, uint32 roleIndex, const utf8* pReason );

        shared_ptr<const PairedAnimationDef> _def;
        vector<RoleState>                    _listRole;
        uint32                               _sessionID;
        uint32                               _nextNotifyIndex; ///< 시각 순서 알림 목록에서 아직 내지 않은 첫 알림
        float32                              _anchorTime;
        float32                              _predictedAnchorTime; ///< 지난 갱신이 예측한 이번 앵커 시각
        PairedAnimationSessionState          _state;
        bool                                 _bClockStarted; ///< 첫 갱신에서 앵커 시각을 받았다(그 뒤로 예측과 대조한다)
    };
} // namespace sw
