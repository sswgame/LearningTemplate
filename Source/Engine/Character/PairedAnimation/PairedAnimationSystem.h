/**
 * @file PairedAnimationSystem.h
 * @brief 씬의 페어드 애니메이션 세션을 들고 프레임마다 갱신하는 프레임 시스템입니다(`ISceneFrameSystem`, 키 `PairedAnimation`).
 * @details `FrameSystems` 단계(게임 스레드, 컴포넌트 틱 · 애니메이션 평가 앞)에서 돈다 — 세션은 다른 오브젝트의 애니메이터를 바꾸므로 틱 워커에서
 *          돌 수 없고, 평가보다 먼저 이번 프레임의 결정(진입 · 정렬 · 공유 알림)을 끝내야 한다. 참가자는 한 번에 한 세션에만 있다(`ParticipantBusy`).
 *          `PairedAnimationComponent` 가 플레이 시작에 붙이고(`acquire`), 마지막 컴포넌트가 끝나면 뗀다(엔진 모듈 타입이라 핫 리로드와 상관없다).
 *          끝난 세션(`Finished` · `Aborted`)은 그 프레임 동안 조회되고 다음 갱신 앞에서 빠진다.
 *          네트워크 클라이언트는 서버가 정한 id 와 상태(`PairedAnimationSessionSnapshot`)로 `restoreSession` 을 불러 세션을 이어 받는다.
 */
#pragma once
#include "Core/Container/ComponentHandle.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"

#include "Engine/Character/PairedAnimation/PairedAnimationSession.h"
#include "Engine/Object/GameObject/Tick/ISceneFrameSystem.h"

namespace sw
{
    class GameObjectManager;
    class PairedAnimationDef;

    /**
     * @class PairedAnimationSystem
     * @brief 세션 목록과 참가자 독점을 맡습니다. 파일 머리말 참고.
     */
    class SW_API PairedAnimationSystem final : public ISceneFrameSystem
    {
    public:
        /** @brief 매니저의 프레임 시스템 키입니다. */
        static constexpr const utf8* kFrameSystemKey = "PairedAnimation";

        PairedAnimationSystem();

        /** @brief 매니저에 붙은 시스템입니다. 없으면 nullptr 입니다. */
        static PairedAnimationSystem* find( const GameObjectManager& manager );
        /** @brief 매니저에 붙은 시스템을 반환하고, 없으면 붙입니다(게임 스레드, 틱 밖). */
        static PairedAnimationSystem& getOrCreate( GameObjectManager& manager );

        /** @brief 이번 프레임을 갱신합니다 — 지난 프레임에 끝난 세션을 빼고, 남은 세션을 갱신합니다. */
        void runBeforeTick( GameObjectManager& manager, float32 deltaTime ) override;
        /**
         * @brief 세션 갱신이 하는 일입니다. 참가자 애니메이터의 재생 상태(클립 · 속도 · 워프 · 알림)를 쓰고, 정렬 `Snap` 은 참가자 루트의 월드 자리를 씁니다.
         *        정렬 거리 · 워프 목표를 위해 자리를, 시계를 위해 애니메이터 상태를 읽습니다. 참가자는 핸들로 찾으므로 등록부는 지난 프레임의 것으로 충분합니다.
         */
        SceneFrameAccess getFrameAccess() const override;

        /**
         * @brief 세션을 만들어 시작합니다(게임 스레드, 틱 밖). 거절되면 세션을 남기지 않습니다.
         * @param outSessionID 시작했으면 새 세션 id, 아니면 0 입니다.
         */
        [[nodiscard]] PairedAnimationStartResult startSession( GameObjectManager& manager, shared_ptr<const PairedAnimationDef> def, const vector<PairedAnimationBinding>& listBinding,
                                                               uint32& outSessionID );
        /**
         * @brief 세트 가운데 지금 자리에 맞는 첫 에셋(`PairedAnimationDef::matchesSelect`)으로 세션을 시작합니다(게임 스레드, 틱 밖). 처형 · 잡기 세트 고르기입니다.
         * @details 고르기 조건의 역할과 앵커가 바인딩에 없거나 풀리지 않으면 그 세트는 맞지 않는다. 맞는 세트가 없으면 `NoMatchingSet` 이고, 고른 세트의 시작이
         *          거절되면 다음 세트로 넘어가지 않고 그 결과를 돌려준다(조건을 통과한 세트가 시작하지 못하는 것은 데이터 · 배치 결함이다).
         * @param outSetIndex 고른 세트의 인덱스, 없으면 `invalid_index::kUint32` 입니다.
         */
        [[nodiscard]] PairedAnimationStartResult startMatchingSession( GameObjectManager& manager, const vector<shared_ptr<const PairedAnimationDef>>& listDef,
                                                                       const vector<PairedAnimationBinding>& listBinding, uint32& outSessionID, uint32& outSetIndex );
        /**
         * @brief 서버가 정한 id 의 세션을 스냅샷의 상태로 이어 받아 시작합니다(게임 스레드, 틱 밖). 늦게 들어온 클라이언트의 따라잡기와 롤백 뒤 복원이 씁니다.
         * @details 세션 id 는 `snapshot._sessionID` 입니다(0 이면 `InvalidSnapshot`). 같은 id 의 세션이 재생 중이면 `SessionIDInUse` 이고, 끝났지만 아직 빠지지 않은 세션이면 새 세션이 그 자리를 대신합니다.
         *          롤백은 먼저 `abortSession` 으로 중단한 뒤 부릅니다. 거절되면 아무것도 바꾸지 않습니다. 세션 일련번호는 복원한 id 보다 앞서 가도록 올립니다(클라이언트가 같은 id 를 새로 받지 않게).
         */
        [[nodiscard]] PairedAnimationStartResult restoreSession( GameObjectManager& manager, shared_ptr<const PairedAnimationDef> def, const vector<PairedAnimationBinding>& listBinding,
                                                                 const PairedAnimationSessionSnapshot& snapshot );
        /** @brief 세션을 중단합니다. 없거나 끝났으면 할 일이 없습니다. */
        void abortSession( GameObjectManager& manager, uint32 sessionID );
        /** @brief id 의 세션입니다. 없으면(빠졌으면) nullptr 입니다. */
        const PairedAnimationSession* findSession( uint32 sessionID ) const;
        /** @brief 그 애니메이터가 재생 중인 세션에 묶여 있으면 true 입니다. 같은 오브젝트의 보폭 보정(`LocomotionWarpingComponent`)은 묻지 않고 세션이 쥐어 둔다. */
        bool isParticipant( const TComponentHandle<SkeletalAnimatorComponent>& animator ) const;
        /** @brief 세션 수입니다(끝났지만 아직 빠지지 않은 것 포함). */
        uint32 getSessionCount() const { return static_cast<uint32>( _listSession.size() ); }
        /** @brief 목록의 @p index 번째 세션입니다(`index < getSessionCount()`). 네트워크 복제가 재생 중인 세션을 훑을 때 씁니다(틱 동안 목록은 바뀌지 않는다). */
        const PairedAnimationSession& getSessionAt( uint32 index ) const { return *_listSession[index]; }

        /** @brief 시스템을 쓰는 컴포넌트 수를 하나 올립니다. */
        void registerUser() { ++_userCount; }
        /** @brief 하나 내립니다. 0 이 되면 true — 부르는 쪽이 매니저에서 뗀다. */
        [[nodiscard]] bool unregisterUser();

    private:
        /** @brief 세트의 고르기 조건이 지금 바인딩의 자리에 맞으면 true 입니다. */
        static bool matchesSelect( GameObjectManager& manager, const PairedAnimationDef& def, const vector<PairedAnimationBinding>& listBinding );

        vector<unique_ptr<PairedAnimationSession>> _listSession;
        uint32                                     _nextSessionID;
        uint32                                     _userCount;
    };
} // namespace sw
