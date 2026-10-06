/**
 * @file GameDirectorComponent.h
 * @brief 게임 한 판의 규칙을 돌리고 런타임 오브젝트(프리팹)를 세우는 디렉터 컴포넌트의 베이스입니다 — 언리얼 `AGameModeBase` · `AGameStateBase` 의 자리.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/GameObjectHandle.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Base/Framework/GameSound.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class Archive;
    class GameObject;
    class GameObjectManager;

    /**
     * @class GameDirectorComponent
     * @brief 씬에 하나 두는 디렉터의 공통 골격입니다. 파생은 규칙(시작 · 틱 · 상태 바이트)과 무엇을 세우는지만 적습니다.
     * @details 베이스가 맡는 것:
     *          - **수명** — `onBeginPlay` 가 틱 그룹을 `PrePhysics` 로 두고(뷰 · 컨트롤러는 뒤 그룹에서 읽는다) `startGame` 을 부른 뒤, 들고 있던 복원 바이트를
     *            적용하고 첫 플러시를 잡습니다. 시작하지 못하면 틱도 돌지 않습니다.
     *          - **상태 바이트** — `restoreState` 는 시작 전이면 들고 있다가 시작할 때, 시작 뒤면 바로 `readState` 로 적용하고 모습을 걷어 다시 세우게
     *            합니다. `writeState` · `restoreState` 는 `ComponentStateStore` 가 이름으로 부르는 계약이고, 게임 인스턴스는
     *            `GameInstanceBase::registerDirector<T>()` 한 줄로 이 디렉터를 상태 스냅샷에 올립니다.
     *          - **틱 뒤 플러시** — 틱(워커) 안에서는 구조를 바꾸지 못하므로 `scheduleFlush` 가 `executeOrDeferPostTick` 한 번으로 `onFlush` 를 게임
     *            스레드에서 부르고, 쌓인 소리(`getSoundQueue`)를 냅니다. 모습이 걷혀 있으면 다음 틱이 스스로 플러시를 잡고 `onFlush` 가 다시 세운다.
     *          - **세운 것** — `spawnPrefab` 이 핸들을 들고 `despawnViews` 가 모두 걷습니다(상태 저장 전 · 플레이 끝). 스스로 사라지는 것의 핸들은 플러시가
     *            가끔 덜어 냅니다.
     *          - **자동 플레이** — PROPERTY `_bAutoPlay` 또는 게임이 `SW_GAME_AUTOPLAY` 로 등록한 전역 변수.
     *          - **다른 오브젝트 디렉터 뒤에** — 키트 디렉터 사이 순서는 보통 한 오브젝트에 붙인 순서다(`GameStateComponent`). 씬을 나눠 디렉터를 다른 오브젝트에
     *            두면 PROPERTY `_tickAfter` 로 그 오브젝트의 첫 디렉터 뒤에 규칙을 돌린다 — 주 틱은 선행 조건을 받지 못하므로 그때만 규칙을 서브틱
     *            (`kRuleSubTick`)으로 옮겨 그 디렉터의 규칙 틱(`getRuleTickHandle`)을 선행 조건으로 건다. 걸지 않은 디렉터는 주 틱 그대로다.
     *
     *          디렉터의 시뮬레이션은 키트의 보통 클래스이고 PROPERTY 가 아니다 — 모듈 정적이나 컴포넌트 PROPERTY 가 아닌 곳에 둔 상태는 핫 리로드에서
     *          사라지므로 `writeState` 로만 넘긴다(Lyra 는 게임 상태를 GameState 컴포넌트에 붙인다 — 여기는 디렉터 하나가 그 자리).
     */
    REFLECT( Abstract, Category = "Framework", DisplayName = "Game Director", Tooltip = "Base of the components that run a game's rules and spawn its runtime objects" )
    class SW_GF_API GameDirectorComponent : public Component
    {
    public:
        REFLECT_BODY();

        static constexpr uint32 kRuleSubTick = 0x52554C45u; ///< 규칙을 도는 서브틱 id(`_tickAfter` 를 걸었을 때만)

        GameDirectorComponent();
        virtual ~GameDirectorComponent() override;

        /** @brief 틱 그룹을 두고 판을 엽니다(`startGame`) — 들고 있던 복원 바이트를 적용하고 첫 플러시를 잡습니다. */
        void onBeginPlay() override;
        /** @brief 세운 것을 걷고 판을 닫습니다. */
        void onEndPlay() override;
        /** @brief 규칙을 주 틱에서 돌면 `runRules` 를 부릅니다(`_tickAfter` 를 걸었으면 서브틱이 부른다). */
        void onTick( float32 deltaTime ) override;
        /** @brief 규칙 서브틱(`kRuleSubTick`)이면 `runRules` 를 부릅니다. */
        void onSubTick( uint32 subTickId, float32 deltaTime ) override;
        /** @brief 다른 오브젝트의 디렉터 뒤에 규칙을 돌게 합니다(플레이 시작 전 — 씬 데이터는 PROPERTY `_tickAfter`). */
        void setTickAfter( GameObjectHandle director ) { _tickAfter = director; }
        /** @brief 이 디렉터의 규칙이 도는 틱입니다 — `_tickAfter` 를 걸었으면 규칙 서브틱, 아니면 주 틱. 다른 디렉터가 선행 조건으로 겁니다. */
        SubTickHandle getRuleTickHandle() const;

        /** @brief 판의 상태를 씁니다 — `ComponentStateStore::capture` 가 부릅니다. 첫 값은 `StateArchiveUtil::writeHeader` 의 표 · 버전입니다. */
        virtual void writeState( Archive& outArchive ) const = 0;
        /**
         * @brief `writeState` 의 바이트로 판을 되살립니다 — `ComponentStateStore::restore` 가 다시 만든 디렉터에 부릅니다.
         * @details 시작 전이면 들고 있다가 `onBeginPlay` 가 판을 연 뒤 적용합니다. 읽지 못하면 `onStateRestored( false )` 가 알리고 새 판으로 갑니다.
         */
        void restoreState( vector<uint8>&& bytes );
        /** @brief 세운 런타임 오브젝트를 모두 지웁니다(상태 저장 전). 판은 그대로이고 다음 틱이 그 상태대로 다시 세운다. */
        void despawnViews();

        /** @brief 판이 열렸으면(`startGame` 이 성공) true 입니다. */
        bool isStarted() const { return _bStarted == SW_TRUE; }
        /** @brief 게임이 스스로 돌면 true 입니다(`_bAutoPlay` 또는 게임의 `-gv_<게임>AutoPlay=1`). */
        bool isAutoPlayOn() const;

        /**
         * @brief 핸들의 오브젝트에 붙은 @p TDirector 입니다. 없으면 nullptr 입니다.
         * @details 뷰 · 컨트롤러는 이것을 매 프레임 부르고 포인터를 들지 않습니다. 매니저 조회는 잠그지 않습니다(틱 중 여러 워커가 불러도 된다).
         *          매니저 타입을 템플릿으로 받아 이 헤더가 오브젝트 매니저를 include 하지 않습니다(부르는 쪽은 이미 include 했다).
         */
        template <typename TDirector, typename TManager>
        static const TDirector* resolve( const TManager& manager, GameObjectHandle director )
        {
            // 오브젝트 타입도 매니저에서 얻어 의존 이름으로 둔다 — 이 헤더에서 GameObject 는 앞 선언뿐이다.
            const auto* pObject = manager.resolveGameObject( director );
            return pObject != nullptr ? pObject->template getComponent<TDirector>() : nullptr;
        }

    protected:
        /** @brief 데이터를 읽고 새 판을 엽니다. 열지 못하면 false 이고 무엇이 왜인지는 파생이 알립니다(경로를 아는 쪽). 실패 뒤 다시 부를 수 있습니다. */
        [[nodiscard]] virtual bool startGame() = 0;
        /** @brief `writeState` 의 바이트를 읽어 한 번에 바꿉니다. 끝까지 맞지 않으면 false 입니다. */
        [[nodiscard]] virtual bool readState( Archive& archive ) = 0;
        /** @brief 복원 바이트를 적용한 뒤입니다 — 결과를 알리고, 실패면 반쯤 바뀐 판을 새 판으로 되돌립니다. */
        virtual void onStateRestored( bool bRestored ) { (void)bRestored; }
        /** @brief 판이 열리고(복원 · 첫 플러시 뒤) 한 번 — 조작 안내 · 첫 상태 로그 · 카메라 맞추기. */
        virtual void onGameStarted() {}
        /** @brief 판이 열린 뒤 매 틱입니다(PrePhysics · 워커). 시간이 멈춘 프레임(@p deltaTime 0)에도 불립니다. */
        virtual void tickGame( float32 deltaTime ) = 0;
        /**
         * @brief 틱 뒤 게임 스레드에서 쌓인 요청을 세웁니다. @p bRespawnViews 면 처음(또는 걷은 뒤)이라 지금 상태의 모습 전부를 세웁니다.
         * @details 쌓인 소리는 이 함수 뒤에 베이스가 냅니다.
         */
        virtual void onFlush( GameObjectManager& manager, bool bRespawnViews ) = 0;
        /** @brief `despawnViews` 가 세운 오브젝트를 지운 뒤입니다 — 파생이 든 핸들 목록 · 대기 요청을 비웁니다. */
        virtual void onViewsDespawned() {}
        /** @brief 세울 요청이 쌓여 있으면 true 입니다 — 틱 끝에 플러시를 잡습니다. */
        virtual bool hasPendingSpawn() const { return false; }

        /** @brief 쌓인 스폰 · 소리를 틱 뒤 한 번으로 미룹니다(틱 밖이면 바로). */
        void scheduleFlush();
        /** @brief 프리팹을 세우고 걷을 목록에 듭니다(게임 스레드). 읽지 못하면 nullptr 입니다. */
        GameObject* spawnPrefab( GameObjectManager& manager, const string& prefabPath, const utf8* pName );
        /** @brief 프리팹이 아닌 길로 세운 오브젝트(코드로 지은 것)를 걷을 목록에 듭니다. */
        void trackSpawned( const GameObject& object );
        /** @brief 세운 오브젝트 하나를 지우고 @p inoutHandle 을 비웁니다. 이미 없으면 핸들만 비웁니다. */
        static void destroySpawned( GameObjectManager& manager, GameObjectHandle& inoutHandle );
        /** @brief 틱 뒤에 낼 소리입니다. */
        GameSoundQueue& getSoundQueue() { return _soundQueue; }
        /** @brief 지금 상태의 모습이 서 있으면 true 입니다(걷으면 다음 플러시까지 false). */
        bool               areViewsSpawned() const { return _bViewsSpawned == SW_TRUE; }
        GameObjectManager* getObjectManager() const;

    private:
        /** @brief 걷혀 있으면 다시 세울 플러시를 잡고 `tickGame` 을 부릅니다. 끝에 쌓인 스폰 · 소리가 있으면 플러시를 잡습니다. */
        void runRules( float32 deltaTime );
        /** @brief `_tickAfter` 가 풀리면 규칙을 서브틱으로 옮기고 그 디렉터의 규칙 틱을 선행 조건으로 겁니다. */
        void hookTickAfter();
        /** @brief 들고 있던 복원 바이트를 적용하고 모습을 다시 세우게 합니다. */
        void applyPendingState();
        /** @brief 쌓인 요청을 세웁니다. 틱 밖(게임 스레드)에서만 불린다. */
        void flushPending();
        /** @brief 목록이 지난번의 두 배가 되면 이미 사라진 오브젝트의 핸들을 덜어 냅니다. */
        void compactSpawned( const GameObjectManager& manager );

    private:
        PROPERTY( Category = "Director", DisplayName = "Auto Play", Tooltip = "The game drives itself (the game's -gv_<game>AutoPlay=1 also turns it on)" )
        bool _bAutoPlay;

        PROPERTY( Category = "Director", DisplayName = "Tick After",
                  Tooltip = "Run this director's rules after the director on that object (cross-object order; same-object order is the component order)" )
        GameObjectHandle _tickAfter;

        vector<GameObjectHandle> _listSpawned;
        vector<uint8>            _pendingStateBytes; ///< 플레이 시작 전에 받은 복원 바이트(`restoreState`)
        GameSoundQueue           _soundQueue;        ///< 낼 소리(틱 뒤 — 오디오는 게임 스레드에서)
        uint32                   _compactThreshold;  ///< 이 수를 넘으면 사라진 핸들을 덜어 낸다
        uint8                    _bStarted        : 1;
        uint8                    _bViewsSpawned   : 1; ///< 지금 상태의 모습이 서 있다(걷으면 다음 틱이 다시 세운다)
        uint8                    _bFlushScheduled : 1;
        uint8                    _bRuleOnSubTick  : 1; ///< 규칙이 서브틱에서 돈다(`_tickAfter`)
        uint8                    _reserved        : 4;
    };
} // namespace sw
