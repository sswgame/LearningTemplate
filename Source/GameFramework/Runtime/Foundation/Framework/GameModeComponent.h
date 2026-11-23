/**
 * @file GameModeComponent.h
 * @brief 게임 한 판의 규칙을 돌리고 런타임 오브젝트(프리팹)를 스폰하는 디렉터 컴포넌트의 베이스입니다.
 * @details 언리얼의 `AGameModeBase` 와 `AGameStateBase` 에 해당한다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Delegate/Delegate.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/Math.h"
#include "Core/Memory/Memory.h"
#include "Core/Name/hashed_string.h"

#include "Engine/Asset/Cache/SharedAssetRef.h"
#include "Engine/Foundation/GameObjectHandle.h"
#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/Serialization/Format/StateIO.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Runtime/Foundation/Framework/GameService.h"
#include "GameFramework/Runtime/Foundation/Framework/PrefabPool.h"
#include "GameFramework/Runtime/Foundation/Framework/Presentation/GameSound.h"

namespace sw
{
    class Archive;
    class GameObject;
    class GameObjectManager;
    class IBehaviorTreeDebugSource;
    struct MinigameResultValue;
    template <typename AssetType>
    class SharedAssetCache;

    /**
     * @class GameModeComponent
     * @brief 씬에 하나 두는 디렉터의 공통 골격입니다.
     * @details 파생 클래스는 규칙(시작, 틱, 상태 바이트)과 무엇을 스폰하는지만 적는다. 베이스는 다음을 맡는다.
     *          - **수명**: `onBeginPlay` 가 틱 그룹을 `PrePhysics` 로 두고(뷰와 컨트롤러는 뒤 그룹에서 읽는다) `openGame` 이 상태 칸을 기본값으로 되돌린 뒤 `startGame` 을 부른다.
     *            그다음 보관하던 복원 바이트를 적용하고 첫 플러시를 예약한다. 시작하지 못하면 틱도 돌지 않는다.
     *          - **상태 바이트**: `restoreState` 는 시작 전이면 바이트를 보관했다가 시작할 때 적용하고, 시작 뒤면 바로 `readState` 로 적용한다.
     *            적용한 뒤에는 뷰 오브젝트를 정리해 다시 스폰하게 한다.
     *            `writeState` 와 `restoreState` 는 `ComponentStateStore` 가 이름으로 부르는 계약이다.
     *            게임 인스턴스는 `GameStateSnapshot::registerGameMode<T>()`(게임 인스턴스의 `getStateSnapshot()`) 한 줄로 이 디렉터를 상태 스냅샷에 등록한다.
     *          - **틱 뒤 플러시**: 틱(워커) 안에서는 구조를 바꾸지 못하므로 `scheduleFlush` 가 `executeOrDeferPostTick` 한 번으로 `onFlush` 를 게임 스레드에서 부른다.
     *            이때 쌓인 소리(`getSoundQueue`)도 낸다.
     *            뷰 오브젝트가 정리돼 있으면 다음 틱이 스스로 플러시를 예약하고, `onFlush` 가 다시 스폰한다.
     *          - **스폰한 오브젝트**: `spawnPrefab` 이 핸들을 보관하고, `despawnViews` 가 모두 정리한다(상태 저장 전과 플레이 끝). 스스로 사라지는 오브젝트의 핸들은 플러시가 가끔 정리한다.
     *            풀 정책(`PrefabPoolPolicy`, 데이터)을 받은 스폰은 프리팹 경로마다 풀 하나에서 꺼내고 `releaseSpawned` 로 돌려받는다(Lyra · Boss Room 의 풀).
     *          - **자동 플레이**: PROPERTY `_bAutoPlay` 로 켠다. 개발 구성에서는 게임이 `Dev/` 에서 `SW_GAME_AUTOPLAY` 로 등록한 전역 변수도 켠다(런타임 칸 `IGameAutoplayQuery` 로 읽는다 — 배포본에는 없다).
     *          - **다른 오브젝트 디렉터 뒤에 돌리기**: 키트 디렉터 사이의 순서는 보통 한 오브젝트에 붙인 순서이다(`GameStateComponent`).
     *            씬을 나눠 디렉터를 다른 오브젝트에 두면 PROPERTY `_tickAfter` 로 그 오브젝트의 첫 디렉터 뒤에 규칙을 돌린다.
     *            주 틱은 선행 조건을 받지 못하므로 그때만 규칙을 서브틱(`kRuleSubTick`)으로 옮기고, 그 디렉터의 규칙 틱(`getRuleTickHandle`)을 선행 조건으로 등록한다.
     *            걸지 않은 디렉터는 주 틱 그대로이다.
     *          - **플레이어 상태**: 지갑 · 가방 · 평판 · 퀘스트 일지는 조종자 오브젝트에 있다(`PlayerComponentUtil`). 그것을 여는 디렉터는 PROPERTY `_playerState` 로 그 오브젝트를 가리킨다
     *            (`requirePlayerState` — 없으면 판을 열지 않는다). 디렉터 오브젝트에 두지 않는다.
     *          - **다시 읽힌 카탈로그**: 규칙 객체는 카탈로그를 날 포인터로 빌린다. `acquireRequired` · `acquireBoundCatalog` 로 받은 에셋은 베이스가 연 판부터 쥐어 끊기지 않고,
     *            다시 읽히면 규칙 틱 앞에서 상태를 왕복(`writeState` → `readState`)시켜 규칙 객체를 새 판에 다시 묶는다(세이브를 읽는 길 하나 — 항목은 id 로 다시 찾는다).
     *            언리얼 DataTable 행 핸들이 다시 읽힌 표에서 행 이름으로 찾는 자리이다. 왕복이 지면(새 판에 없는 항목) 디렉터의 복원 실패 처리(`onStateRestored( false )`)를 따르고 알린다.
     *
     *          - **디렉터 계약 셋**(모든 미니게임 · 허브 디렉터가 같은 셋만 적는다 — 키트 규칙 객체에는 계약을 더하지 않는다):
     *            ① 걸음 `updateGame`(틱 뒤 게임 스레드) — 베이스가 프로파일러 구간(`kStepScopeName`)으로 감싸 감시 값 `GameMode.StepMs` · `GameMode.StepPeakMs` 가 그 구간을 읽는다(줄 추가 없이 모든 미니게임이 값을 낸다).
     *            ② 세이브 `serializeGameState`(리플렉션 상태 칸 뒤에 잇는 손 본문 하나).
     *            ③ 결과 `finishMinigame` → `collectMinigameResult` — 한 판 임시 상태(미니게임 지갑 · 창고 · 점수)는 디렉터가 소유하고
     *            바탕 판(플레이어 진행 · 월드 상태)에 직접 쓰지 않으며 `MinigameResult` 의 값 목록으로만 내보낸다(`MinigameFlow::requestLeave`).
     *
     *          디렉터의 시뮬레이션은 키트의 보통 클래스이고 PROPERTY 가 아니다.
     *          모듈 정적 변수나 컴포넌트 PROPERTY 가 아닌 곳에 둔 상태는 핫 리로드에서 사라지므로, 상태는 `writeState` 로만 넘겨야 한다.
     *          Lyra 는 게임 상태를 GameState 컴포넌트에 붙이는데, 여기서는 디렉터 하나가 그 역할을 한다.
     */
    REFLECT( Abstract, Category = "Framework", DisplayName = "Game Mode", Tooltip = "Base of the components that run a game's rules and spawn its runtime objects" )
    class SW_GF_API GameModeComponent : public Component
    {
    public:
        REFLECT_BODY();

        /** @brief 디렉터의 판 상태는 병렬 틱 중 남이 읽어도 됩니다 — 규칙은 틱 뒤 게임 스레드에서만 돈다(`deferRules`). 틱이 쓰는 것은 자기 시간 누적뿐이다. */
        static constexpr bool   kTickReadSafe = true;
        static constexpr uint32 kRuleSubTick  = 0x52554C45u; ///< 규칙을 도는 서브틱 id(`_tickAfter` 를 걸었을 때만)
        /** @brief 걸음(`updateGame`)을 감싸는 프로파일러 구간 이름입니다 — 감시 값 `GameMode.StepMs` · `GameMode.StepPeakMs` 의 재료(디렉터는 시간 칸 · 읽기 함수를 들지 않는다). */
        static constexpr const utf8* kStepScopeName = "GT.GameMode.updateGame";

        GameModeComponent();
        virtual ~GameModeComponent() override;

        /** @brief 틱 그룹을 정하고 판을 시작합니다(`openGame`). 보관하던 복원 바이트를 적용하고 첫 플러시를 예약합니다. */
        void onBeginPlay() override;
        /** @brief 스폰한 오브젝트를 정리하고 판을 끝냅니다. */
        void onEndPlay() override;
        /** @brief 씬의 컴포넌트 레지스트리에 오릅니다 — 감시 값 `GameMode.StepMs` 가 씬을 훑지 않고 디렉터를 찾는다. */
        void onRegister( GameObjectManager& manager ) override;
        /** @brief 레지스트리에서 내립니다. */
        void onUnregister( GameObjectManager& manager ) override;
        /** @brief 규칙이 주 틱에서 돌면 `runRules` 를 틱 뒤로 겁니다(`deferRules`). `_tickAfter` 를 걸었으면 서브틱이 겁니다. */
        void onTick( TickContext& context ) override;
        /** @brief 규칙 서브틱(`kRuleSubTick`)이면 `runRules` 를 틱 뒤로 겁니다. */
        void onSubTick( uint32 subTickID, TickContext& context ) override;
        /** @brief 다른 오브젝트의 디렉터 뒤에 규칙을 돌게 합니다. 플레이를 시작하기 전에 불러야 하며, 씬 데이터는 PROPERTY `_tickAfter` 로 적습니다. */
        void setTickAfter( GameObjectHandle gameMode ) { _tickAfter = gameMode; }
        /** @brief 이 디렉터의 규칙이 도는 틱입니다. `_tickAfter` 를 걸었으면 규칙 서브틱이고, 아니면 주 틱입니다. 다른 디렉터가 선행 조건으로 겁니다. */
        SubTickHandle getRuleTickHandle() const;
        /**
         * @brief 컴포넌트에 안 붙은 행동 트리 실행기(지휘관 AI 등)를 쥔 디렉터가 재정의해 자기 자신을 돌려줍니다. 행동 트리 창이 선택한 오브젝트에서 찾는다. 없으면 nullptr 입니다.
         * @details 런타임 타입 정보(RTTI)를 끄고 빌드해 컴포넌트를 인터페이스로 캐스트할 수 없어 가상 함수로 묻는다.
         */
        virtual const IBehaviorTreeDebugSource* findBehaviorTreeDebugSource() const { return nullptr; }
        /**
         * @brief 판에 하루의 시각이 있는 디렉터가 재정의해 지금 시각(0~24)과 강수를 답합니다. 시각이 없는 판은 false 입니다.
         * @details 방향광을 해처럼 움직이는 `DaylightComponent` 가 묻는다 — 시각을 쥔 쪽은 디렉터라(게임마다 달력이 다르다) 같은 RTTI 없는 가상 함수로 묻는다.
         */
        [[nodiscard]] virtual bool readDaylight( float32& outHourOfDay, bool& outbPrecipitating ) const
        {
            (void)outHourOfDay;
            (void)outbPrecipitating;
            return false;
        }
        /**
         * @brief 카메라가 따라갈 초점을 답합니다. @p outFocusObject 는 따라갈 오브젝트(비면 자리만), @p outFocusPosition 은 디렉터가 아는 자리(오브젝트가 없을 때 쓴다)입니다.
         * @details `FollowCameraComponent` 가 묻는다. 초점이 없는 판은 false 입니다.
         */
        [[nodiscard]] virtual bool readCameraFocus( GameObjectHandle& outFocusObject, float3& outFocusPosition ) const
        {
            (void)outFocusObject;
            (void)outFocusPosition;
            return false;
        }

        /** @brief 판의 상태를 씁니다. `ComponentStateStore::capture` 가 부릅니다(`serializeState`). */
        void writeState( Archive& outArchive ) const { StateIO::writeObject( *this, outArchive ); }
        /**
         * @brief 디렉터 상태의 입구입니다 — 리플렉션 상태 칸(`PROPERTY( RuntimeState )`, 동적 형식 기준 `StateIO::reflectedAs`)을 먼저 놓고 파생의 `serializeGameState` 를 잇습니다.
         * @details 상태 칸은 씬 스냅숏도 옮기지만 그 뒤 `onBeginPlay` 의 `openGame` 이 기본값으로 되돌리므로, 판을 연 뒤 적용하는 이 바이트가 싣는다(언리얼 `UPROPERTY( SaveGame )` 자리).
         *          파생은 칸을 다시 적지 않는다 — 한 칸을 두 곳이 알지 않는다. 읽기는 사본 없이 돌아, 칸을 덮기 전에 지금 칸을 따로 써 두고 손 본문이 거절하면 되돌린다.
         */
        void serializeState( StateIO& io );
        /**
         * @brief `writeState` 의 바이트로 판을 복원합니다. `ComponentStateStore::restore` 가 다시 만든 디렉터에 부릅니다.
         * @details 시작 전이면 보관했다가 `onBeginPlay` 가 판을 시작한 뒤 적용한다. 읽지 못하면 `onStateRestored( false )` 가 알리고 새 판으로 간다.
         */
        void restoreState( vector<uint8>&& bytes );
        /** @brief 시작 전에 받아 보관한 상태입니다(`ComponentStateStore` 계약 — 있으면 그것이 곧 상태다). */
        const vector<uint8>& getPendingStateBytes() const { return _pendingStateBytes; }
        /** @brief 스폰한 런타임 오브젝트를 모두 지웁니다. 상태를 저장하기 전에 부르며, 판은 그대로이고 다음 틱이 그 상태대로 다시 스폰합니다. */
        void despawnViews();

        /**
         * @brief 디렉터 계약 ③ — 이 디렉터가 도는 미니게임을 결과 @p resultName(예: `win` · `lose`)으로 끝냅니다. 넘길 값은 `collectMinigameResult` 가 모은다.
         * @details 나가기는 다음 게임 인스턴스 갱신(틱 밖)이 한다(`MinigameFlow::requestLeave`) — 하위 레벨 나가기가 이 디렉터를 지우므로 규칙 안에서 바로 나가지 않는다.
         * @return 미니게임 흐름이 없거나 켜진 미니게임이 없으면 오류를 남기고 false 입니다(허브 판에서 부르면 계약 위반이다).
         */
        [[nodiscard]] bool finishMinigame( const hashed_string& resultName );
        /** @brief 판이 열렸으면(`startGame` 이 성공) true 입니다. */
        bool isStarted() const { return _bStarted == SW_TRUE; }
        /** @brief 플레이어 상태 오브젝트(`_playerState` — 조종자)입니다. 가리키는 것이 없으면 nullptr 입니다. */
        const GameObject* findPlayerState() const { return findGameObject( _playerState ); }
        /** @brief 게임이 스스로 돌면 true 입니다(`_bAutoPlay`, 또는 개발 구성에서 게임의 `-gv_<게임>AutoPlay=1`). */
        bool isAutoPlayOn() const;

        /**
         * @brief 핸들의 오브젝트에 붙은 @p TDirector 입니다. 없으면 nullptr 입니다.
         * @details 뷰와 컨트롤러는 이것을 매 프레임 부르고 포인터를 보관하지 않는다.
         *          매니저 조회는 잠그지 않으므로 틱 중 여러 워커가 불러도 된다.
         *          매니저 타입을 템플릿으로 받아서, 이 헤더는 오브젝트 매니저를 include 하지 않는다. 부르는 쪽은 이미 include 했다.
         */
        template <typename TDirector, typename TManager>
        static const TDirector* resolve( const TManager& manager, GameObjectHandle gameMode )
        {
            // 오브젝트 타입도 매니저에서 얻어 의존 이름으로 둔다. 이 헤더에서 GameObject 는 앞 선언뿐이다.
            const auto* pObject = manager.resolveGameObject( gameMode );
            // 디렉터는 틱 뒤에만 쓰는 형식이라 병렬 틱 안에서도 남 읽기의 길로 찾는다(`kTickReadSafe`).
            return pObject != nullptr ? pObject->template getTickSafeComponent<TDirector>() : nullptr;
        }

    protected:
        /**
         * @brief 리플렉션 밖의 디렉터 상태(키트 시뮬레이션 · 포인터 ↔ id · `Countdown` · 난수)를 놓는 본문 하나입니다(`StateIO`). 첫 값은 `io.header` 의 표준 머리입니다.
         * @details 상태 칸은 이미 읽혀 있어 칸 사이 검사에 쓸 수 있다. 읽기는 사본 없이 돌므로(컴포넌트는 복사하지 않는다) 지역 변수에 읽어 끝까지 맞으면 대입합니다.
         */
        virtual void serializeGameState( StateIO& io ) = 0;
        /**
         * @brief 데이터를 읽고 새 판을 엽니다. 열지 못하면 false 를 반환하며, 무엇이 왜 실패했는지는 경로를 아는 파생 클래스가 알립니다. 실패한 뒤 다시 부를 수 있습니다.
         * @details `openGame` 만 부른다. 들어올 때 상태 칸(`PROPERTY( RuntimeState )`)은 기본값이라 앞 판의 값을 되돌리는 줄을 적지 않는다 — 기본값과 다른 시작 값(씬 데이터)만 적는다.
         */
        [[nodiscard]] virtual bool startGame() = 0;
        /** @brief 상태 칸을 모두 기본값으로 되돌리고 `startGame` 으로 새 판을 엽니다. 판을 여는 유일한 입구입니다(플레이 시작, 복원에 실패한 판을 새로 열 때). */
        [[nodiscard]] bool openGame();
        /** @brief `writeState` 의 바이트를 읽어 한 번에 바꿉니다. 끝까지 맞지 않으면 false 입니다. */
        [[nodiscard]] bool readState( Archive& archive ) { return StateIO::readInPlace( *this, archive ); }
        /** @brief 복원 바이트를 적용한 뒤에 불립니다. 결과를 알리고, 실패했으면 반쯤 바뀐 판을 새 판으로 되돌립니다. */
        virtual void onStateRestored( bool bRestored ) { (void)bRestored; }
        /**
         * @brief `finishMinigame` 이 결과에 실을 값(번 돈 · 얻은 물건 · 점수)을 @p outListValue 에 더합니다. 한 판 임시 상태를 쥔 미니게임 디렉터가 덮는다.
         * @details 바탕 판에 쓰는 것은 결과를 받는 쪽(`MinigameFlow::onFinished` 구독 — 허브 디렉터)이다. 미니게임 디렉터가 플레이어 진행을 직접 고치면
         *          씬 전환 복원(들어갈 때 찍은 스냅숏)이 그 값을 되감는다.
         */
        virtual void collectMinigameResult( vector<MinigameResultValue>& outListValue ) const { (void)outListValue; }
        /** @brief 판이 열리고(복원과 첫 플러시 뒤) 한 번 불립니다. 조작 안내, 첫 상태 로그, 카메라 맞추기에 씁니다. */
        virtual void onGameStarted() {}
        /** @brief 판이 열린 뒤 매 프레임 불립니다 — PrePhysics 틱이 건 틱 뒤 작업이라 게임 스레드(틱 밖)에서 돈다(`deferRules`). 시간이 멈춘 프레임(@p deltaTime 0)에도 불립니다. */
        virtual void updateGame( float32 deltaTime ) = 0;
        /**
         * @brief 틱 뒤 게임 스레드에서 쌓인 요청을 세웁니다. @p bRespawnViews 면 처음(또는 걷은 뒤)이라 지금 상태의 모습 전부를 세웁니다.
         * @details 쌓인 소리는 이 함수 뒤에 베이스가 냅니다.
         */
        virtual void onFlush( GameObjectManager& manager, bool bRespawnViews ) = 0;
        /** @brief `despawnViews` 가 스폰한 오브젝트를 지운 뒤에 불립니다. 파생 클래스가 보관한 핸들 목록과 대기 요청을 비웁니다. */
        virtual void onViewsDespawned() {}
        /** @brief 세울 요청이 쌓여 있으면 true 입니다. 틱 끝에 플러시를 잡습니다. */
        virtual bool hasPendingSpawn() const { return false; }

        /** @brief 쌓인 스폰과 소리를 틱 뒤 한 번으로 미룹니다. 틱 밖이면 바로 처리합니다. */
        void scheduleFlush();
        /** @brief 프리팹을 스폰하고 정리할 목록에 넣습니다. 게임 스레드에서 부르며, 프리팹을 읽지 못하면 nullptr 를 반환합니다. */
        GameObject* spawnPrefab( GameObjectManager& manager, string_view prefabPath, const utf8* pName );
        /**
         * @brief 풀 정책 @p pool(데이터)대로 프리팹을 꺼냅니다. `_maxIdleCount` 가 0 이면 위 `spawnPrefab` 과 같고, 아니면 그 프리팹 경로의 풀(`PrefabPool`)에서 꺼낸다.
         * @details 풀에서 꺼낸 인스턴스는 스폰 상태로 되돌려 둔 것이다 — 자리 · 시작 호출은 꺼낸 쪽이 정한다. 돌려줄 때는 `releaseSpawned` 를 부른다.
         *          풀 계약을 어긴 프리팹(되돌리기를 모르는 컴포넌트)이면 오류를 남기고 nullptr 다. 풀은 `despawnViews` 가 모두 지운다(상태 저장 전 · 플레이 끝).
         */
        GameObject* spawnPrefab( GameObjectManager& manager, string_view prefabPath, const utf8* pName, const PrefabPoolPolicy& pool );
        /** @brief `spawnPrefab( …, pool )` 로 꺼낸 오브젝트를 돌려주고 @p inoutHandle 을 비웁니다. 그 경로의 풀이 없으면(정책 0) 지웁니다. */
        void releaseSpawned( GameObjectManager& manager, string_view prefabPath, GameObjectHandle& inoutHandle );
        /** @brief 프리팹이 아닌 경로로 만든 오브젝트(코드로 만든 것)를 정리할 목록에 넣습니다. */
        void trackSpawned( const GameObject& object );
        /** @brief 스폰한 오브젝트 하나를 지우고 @p inoutHandle 을 비웁니다. 이미 없으면 핸들만 비웁니다. */
        static void destroySpawned( GameObjectManager& manager, GameObjectHandle& inoutHandle );
        /** @brief 스폰한 오브젝트 목록을 모두 지우고 비웁니다(모습을 통째로 다시 세울 때). */
        static void destroySpawned( GameObjectManager& manager, vector<GameObjectHandle>& inoutListHandle );
        /** @brief 씬 칸이 가리키는 오브젝트입니다. 매니저나 오브젝트가 없으면 nullptr 입니다. */
        /** @details const 판은 읽기 전용 오브젝트를 돌려준다 — const 함수(틱에서 읽는 자리)가 쓰기 가능한 오브젝트를 꺼내지 못하게(`getObjectManager` 와 같은 쌍). */
        const GameObject* findGameObject( GameObjectHandle handle ) const;
        GameObject*       findGameObject( GameObjectHandle handle );
        /** @brief 씬 칸이 가리키는 오브젝트의 @p TComponent 입니다. 없으면 nullptr 입니다. 오브젝트 형식을 의존 이름으로 받아 이 헤더가 오브젝트를 include 하지 않는다. */
        template <typename TComponent, typename TObject = GameObject>
        const TComponent* findOn( GameObjectHandle handle ) const
        {
            const TObject* pObject = findGameObject( handle );
            return pObject != nullptr ? pObject->template getComponent<TComponent>() : nullptr;
        }
        template <typename TComponent, typename TObject = GameObject>
        TComponent* findOn( GameObjectHandle handle )
        {
            TObject* pObject = findGameObject( handle );
            return pObject != nullptr ? pObject->template getComponent<TComponent>() : nullptr;
        }
        /** @brief 플레이어 상태 오브젝트입니다. 판을 열 디렉터가 반드시 쓰는 것이라 없으면 오류(`GameMode.PlayerStateMissing`)를 남기고 nullptr 입니다. */
        [[nodiscard]] const GameObject* requirePlayerState() const;
        /** @brief 플레이어 상태 오브젝트의 @p TComponent 입니다. 없으면 nullptr 입니다. */
        template <typename TComponent>
        TComponent* findOnPlayerState()
        {
            return findOn<TComponent>( _playerState );
        }
        /**
         * @brief 판을 여는 데 꼭 필요한 에셋을 공유 캐시에서 받습니다. 경로가 비었거나 읽지 못하면 오류를 남기고 false 입니다.
         * @details `GameInstance::loadRequiredCatalog` 의 디렉터판이다. 받는 쪽이 `SharedAssetCache` 헤더를 include 한다(이 헤더는 앞 선언만 한다).
         */
        template <typename TAsset>
        [[nodiscard]] bool acquireRequiredSnapshot( string_view path, shared_ptr<const TAsset>& outAsset ) const
        {
            outAsset = path.empty() ? nullptr : SharedAssetCache<TAsset>::acquire( path ).snapshot();
            if ( outAsset != nullptr )
                return true;
            reportMissingAsset( path );
            return false;
        }
        /** @brief `acquireRequired` 와 같고 멤버로 쥐어 다시 읽힌 판을 따라갑니다(`SharedAssetRef`). 받은 때의 판을 일부러 쥘 종류(번호를 지어 둔 떼어 내는 종류)는 `acquireRequiredSnapshot` 이다. */
        template <typename TAsset>
        [[nodiscard]] bool acquireRequired( string_view path, SharedAssetRef<TAsset>& outAsset )
        {
            outAsset = path.empty() ? SharedAssetRef<TAsset>{} : SharedAssetCache<TAsset>::acquire( path );
            if ( outAsset != nullptr )
            {
                followAsset( outAsset );
                return true;
            }
            reportMissingAsset( path );
            return false;
        }
        /**
         * @brief 게임 기능 범위가 건 카탈로그(`GameFeatureScope::loadCatalog`)를 같이 쥡니다. 걸려 있지 않으면 오류를 남기고 false 입니다 — 경로 칸으로 따로 읽지 않는다.
         * @details 씬을 나가는 동안(범위를 끈 뒤 새 씬이 서기 전) 디렉터가 더 돈다 — 날 포인터 대신 참조로 쥐어 그동안 끊기지 않는다.
         */
        template <typename TCatalog>
        [[nodiscard]] bool acquireBoundCatalog( SharedAssetRef<TCatalog>& outCatalog )
        {
            outCatalog = SharedAssetRef<TCatalog>::shareLive( game::findSubsystem<const TCatalog>() );
            if ( outCatalog != nullptr )
            {
                followAsset( outCatalog );
                return true;
            }
            reportMissingCatalog( TCatalog::kDataKind );
            return false;
        }
        /** @brief 입력 맵의 동작이 눌려 있으면 true 입니다. 위젯이 받은 입력은 게임으로 새지 않습니다(`UIInputMapView`). */
        [[nodiscard]] bool isActionDown( const hashed_string& action ) const;
        /** @brief 입력 맵의 동작이 이번 프레임에 발동했으면 true 입니다(`UIInputMapView`). */
        [[nodiscard]] bool wasActionTriggered( const hashed_string& action ) const;
        /** @brief 입력 맵의 2D 축 동작(`Move`)입니다. 위젯이 받은 입력은 게임으로 새지 않습니다(`UIInputMapView`). 입력 서비스가 없으면 0 입니다. */
        [[nodiscard]] float2 getActionVector2D( const hashed_string& action ) const;
        /** @brief 틱 뒤에 낼 소리를 쌓는 큐입니다. */
        GameSoundQueue& getSoundQueue() { return _soundQueue; }
        /** @brief 지금 상태의 모습이 서 있으면 true 입니다(걷으면 다음 플러시까지 false). */
        bool areViewsSpawned() const { return _bViewsSpawned == SW_TRUE; }
        /** @brief 이 게임 모드가 사는 씬의 읽기 매니저입니다. const 함수(틱에서 읽는 자리)는 이쪽만 받는다 — 쓰기는 `executeOrDeferPostTickOnScene` 으로 미룬다. */
        const GameObjectManager* getObjectManager() const;
        /** @brief 쓰기 가능한 매니저입니다. 틱 밖(플러시 · 틱 뒤 일)에서만 부른다 — 틱 중이면 `GameObject::getMutableManager` 가 단언한다. */
        GameObjectManager* getObjectManager();

    private:
        /** @brief 판을 열며 받은 에셋 하나입니다. 다시 읽힌 판을 알아채고, 규칙 객체가 날 포인터로 빌린 판들을 다음 판을 열 때까지 쥔다. */
        struct FollowedAsset
        {
            Delegate<shared_ptr<const void>()> _takeLatest{}; ///< 지금 최신 판
            vector<shared_ptr<const void>>     _listHeld{};   ///< 연 판부터 지금 판까지(뒤가 지금 판)
        };

        /** @brief 판을 열며 받은 @p asset 을 쥐고 다시 읽히는지 봅니다. */
        template <typename TAsset>
        void followAsset( const SharedAssetRef<TAsset>& asset )
        {
            FollowedAsset followed;
            followed._takeLatest = SW_DELEGATE_LAMBDA( Delegate<shared_ptr<const void>()>, [asset]()
            { return shared_ptr<const void>( asset.snapshot() ); } );
            followed._listHeld.push_back( asset.snapshot() );
            _listFollowedAsset.push_back( std::move( followed ) );
        }
        /** @brief 받은 에셋이 다시 읽혔으면 상태를 왕복시켜 규칙 객체를 새 판에 다시 묶습니다(게임 스레드, 규칙 틱 앞). */
        void applyReloadedAssets();
        /** @brief 상태 칸(`PROPERTY( RuntimeState )`)을 이 형식의 기본 생성자 값으로 되돌립니다. 되돌리지 못하는 칸이면 프로세스를 멈춥니다. */
        void resetRuntimeState();
        /** @brief 필수 에셋을 받지 못했다는 오류를 남깁니다(`GameMode.RequiredAssetMissing`). 템플릿 밖에 두어 헤더가 로그를 끌어오지 않게 한다. */
        static void reportMissingAsset( string_view path );
        /** @brief 게임 범위에 카탈로그가 걸려 있지 않다는 오류를 남깁니다(`GameMode.BoundCatalogMissing`). */
        static void reportMissingCatalog( string_view kind );
        /** @brief 걷혀 있으면 다시 세울 플러시를 잡고 `tickGame` 을 부릅니다. 끝에 쌓인 스폰이나 소리가 있으면 플러시를 잡습니다. */
        void runRules( float32 deltaTime );
        /** @brief 틱은 시간만 쌓고 규칙(`runRules`)은 틱 뒤에 돌게 겁니다 — `updateGame` 은 틱 밖(게임 스레드)에서 돈다. */
        void deferRules( TickContext& context );
        /** @brief 쌓인 시간으로 규칙을 돕니다(틱 뒤 작업 몸체). */
        void runPendingRules();
        /** @brief `_tickAfter` 가 풀리면 규칙을 서브틱으로 옮기고, 그 디렉터의 규칙 틱을 선행 조건으로 겁니다. */
        void hookTickAfter();
        /** @brief 보관하던 복원 바이트를 적용하고 뷰 오브젝트를 다시 스폰하게 합니다. */
        void applyPendingState();
        /** @brief 쌓인 요청을 세웁니다. 틱 밖(게임 스레드)에서만 부릅니다. */
        void flushPending();
        /** @brief 목록이 지난번의 두 배가 되면 이미 사라진 오브젝트의 핸들을 덜어 냅니다. */
        void compactSpawned( const GameObjectManager& manager );

    private:
        PROPERTY( Category = "Game Mode", DisplayName = "Auto Play", Tooltip = "The game drives itself (the game's -gv_<game>AutoPlay=1 also turns it on)" )
        bool _bAutoPlay;

        PROPERTY( Category = "Game Mode", DisplayName = "Tick After",
                  Tooltip = "Run this game mode's rules after the game mode on that object (cross-object order; same-object order is the component order)" )
        GameObjectHandle _tickAfter;

        PROPERTY( Category = "Game Mode", DisplayName = "Player State",
                  Tooltip = "Object that holds the player's wallet, inventory, reputation and quest log (the player controller object); game modes that open one of them do not start without it" )
        GameObjectHandle _playerState;

        vector<GameObjectHandle> _listSpawned;
        vector<PrefabPool>       _listPool;             ///< 풀 정책으로 꺼낸 프리팹의 풀(경로마다 하나). 꺼낸 것은 `_listSpawned` 밖이라 `despawnViews` 가 풀째 지운다
        vector<FollowedAsset>    _listFollowedAsset;    ///< 이 판을 열며 받은 에셋(`followAsset`)
        vector<uint8>            _pendingStateBytes;    ///< 플레이 시작 전에 받은 복원 바이트(`restoreState`)
        GameSoundQueue           _soundQueue;           ///< 낼 소리. 오디오는 게임 스레드에서 다루므로 틱 뒤에 낸다
        uint32                   _compactThreshold;     ///< 이 수를 넘으면 사라진 핸들을 덜어 낸다
        float32                  _pendingRuleDeltaTime; ///< 틱이 쌓고 틱 뒤 `runPendingRules` 가 쓰는 시간
        uint8                    _bStarted        : 1;
        uint8                    _bViewsSpawned   : 1; ///< 지금 상태의 뷰 오브젝트가 스폰돼 있다(정리하면 다음 틱이 다시 스폰한다)
        uint8                    _bFlushScheduled : 1;
        uint8                    _bRuleOnSubTick  : 1; ///< 규칙이 서브틱에서 돈다(`_tickAfter`)
        [[maybe_unused]] uint8   _reserved        : 4;
    };
} // namespace sw
