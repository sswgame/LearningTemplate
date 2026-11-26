/**
 * @file SkirmishGameModeComponent.h
 * @brief RTS 키트의 교전 한 판 디렉터입니다(지도 · 플레이어 수는 씬 데이터 `SkirmishMap`). 한 판(`SkirmishMatch`), 속도 · 일시정지를 맡고 사람의 명령을 값(`RTSCommand`)으로 받습니다.
 * @details 언리얼의 GameMode/GameState 에 해당합니다. 씬에 하나만 둡니다. 규칙(채취, 생산, 전투, 안개, AI)은 `RTSWorld`, `RTSCommanderAI`, `SkirmishMatch` 가 맡는다.
 *          고르기 · 명령 입력(`RTSCommandInputComponent`)과 모습(`RTSBattlefieldViewComponent` · `RTSUnitViewComponent`)은 클라이언트 모듈(`GF_Client_RealTimeStrategy`)의 것이고,
 *          이 디렉터의 명령 · 읽기 API 로만 판을 본다.
 *
 *          사람의 명령은 모두 `submitCommand` 한 곳으로 들어온다. 이 디렉터는 월드에 바로 적용하고, 2 인 대전 디렉터(게임이 락스텝 키트와 섞은 파생)는 판을 여는 자리(`openPlayers`) ·
 *          걸음(`stepMatch`) · 명령을 덮어 명령을 락스텝 입력에 싣는다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Expected.h"
#include "Core/Foundation/Types.h"
#include "Core/Time/TimeSeconds.h"

#include "Engine/Asset/AssetRef.h"
#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/Serialization/Serializer/CatalogKey.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Genre/Strategy/RealTimeStrategy/Component/RTSGameModeComponent.h"
#include "GameFramework/Kits/Genre/Strategy/RealTimeStrategy/RTSCatalog.h"
#include "GameFramework/Kits/Genre/Strategy/RealTimeStrategy/RTSCommand.h"
#include "GameFramework/Kits/Genre/Strategy/RealTimeStrategy/SkirmishMatch.h"
#include "GameFramework/Runtime/Character/AI/BehaviorTreeAsset.h"
#include "GameFramework/Runtime/Character/AI/IBehaviorTreeDebugSource.h"
#include "GameFramework/Runtime/Foundation/GameMode/BoundAssetRef.h"
#include "GameFramework/Runtime/Foundation/GameMode/GameSpeedControl.h"

namespace sw
{
    class GameObjectManager;

    /**
     * @class SkirmishGameModeComponent
     * @brief 1 대 1 한 판을 진행합니다. 플레이가 시작되면 유닛 데이터를 읽고 판을 엽니다. 사람(파랑) 대 컴퓨터이며, 자동 플레이면 컴퓨터 대 컴퓨터입니다.
     * @details 판(월드, AI 진행, 속도)은 PROPERTY 가 아니라 `writeState` 로 게임 상태 스냅샷의 컴포넌트 섹션에 실려 핫 리로드와 세이브를 넘긴다.
     *          움직이던 유닛은 앞 명령의 경로를 다시 구한다. **고른 유닛 · 부대는 판 상태가 아니다** — 이 화면 사람의 것이라 클라이언트의 명령 입력 컴포넌트가 자기 구간에 싣는다
     *          (락스텝 체크섬 · 서버의 판 상태에 화면 상태가 섞이지 않는다).
     */
    REFLECT( Category = "RealTimeStrategy", DisplayName = "Skirmish Game Mode", Tooltip = "Runs the one-on-one skirmish match and takes the player commands" )
    class SW_KIT_API SkirmishGameModeComponent : public RTSGameModeComponent
    {
    public:
        REFLECT_BODY();
        SW_REGISTERED_COMPONENT( SkirmishGameModeComponent );

        SkirmishGameModeComponent();
        virtual ~SkirmishGameModeComponent() override;

        /** @brief @p manager 의 스커미시 디렉터(씬에 하나)입니다. 없으면 nullptr, 둘 이상이면 오류를 남기고 nullptr 입니다(감시 값 · 개발 명령이 등록 순서로 고르지 않는다). */
        static SkirmishGameModeComponent* findInScene( const GameObjectManager& manager );

        /** @brief 판(월드, AI 진행, 속도, 일시정지)을 씁니다. `ComponentStateCodec::captureObject` 가 부릅니다. */
        void serializeGameState( StateIO& io ) override;

        // ---- 읽기(클라이언트 모듈) ----
        const RTSWorld&      getWorld() const override { return _match.getWorld(); }
        const SkirmishMatch& getMatch() const { return _match; }
        bool                 isLocalPlayerHuman() const override { return _match.isHumanPlayer( getLocalPlayer() ); }
        /** @brief 마지막 걸음(`updateGame`)에 나온 알림입니다. 다음 걸음이 비운다(시간이 멈춘 걸음도 비운다). */
        const vector<RTSEvent>& getStepEvents() const { return _listStepEvent; }
        /**
         * @brief 이 판의 정체입니다 — 새 판을 열 때마다 새로 뽑고(0 은 아직 열지 않음) 세이브를 읽으면 그 판의 것이 된다. 유닛 id 를 따로 저장하는 쪽(고르기)이 같은 판의 것인지 견준다.
         * @details 유닛 id(슬롯 · 세대)는 판마다 처음부터 다시 나가 다른 판의 id 와 겹친다. 락스텝 체크섬(판 상태)에는 들지 않는다 — 두 프로세스가 저마다 뽑는다.
         */
        uint64 getMatchID() const { return _matchID; }
        /** @brief 컴퓨터 상대의 행동 트리 실행기를 행동 트리 창에 내놓습니다(활성 노드 보기). */
        const IBehaviorTreeDebugSource* findBehaviorTreeDebugSource() const override { return &_commanderDebugSource; }

        // ---- 명령(판이 열린 동안만 받는다, 게임 스레드) ----
        /**
         * @brief 사람의 명령 하나를 냅니다. 이 화면의 플레이어로 월드에 바로 적용해 받은 유닛 수를 반환합니다(2 인 대전 파생은 다음 락스텝 입력에 싣고 -1 — 아직 적용 전).
         * @details 개발 명령(`skirmish.order`)과 입력이 같은 길로 들어온다. 판이 열리기 전이면 0 입니다.
         */
        virtual int32 submitCommand( RTSCommand&& command );
        /** @brief 속도 · 일시정지 입력을 넘깁니다(눌린 프레임에만 참). 판이 열리기 전이면 거절하고 false 입니다(2 인 대전 파생도 거절한다 — 두 프로세스가 같은 걸음이어야 한다). */
        virtual bool submitTimeInput( bool bPauseTriggered, bool bFasterTriggered, bool bSlowerTriggered );

    protected:
        /** @brief 유닛 데이터를 읽고 판을 엽니다. 사람(파랑) 대 컴퓨터이며, 자동 플레이면 컴퓨터 대 컴퓨터입니다. */
        [[nodiscard]] bool startGame() override;
        void               onStateRestored( bool bRestored ) override;
        void               updateGame( GameSeconds frameTime ) override;

        /**
         * @brief 새 판에서 사람이 맡을 플레이어 깃발(`SkirmishMatch::kHumanPlayer0` …)입니다. 여기서는 0 번, 자동 플레이면 없음(0)입니다.
         * @details 2 인 대전 파생이 덮어 대전 링크를 열고 두 플레이어를 낸다. 없음(nullopt)이면 판을 열지 않는다 — 까닭은 덮은 쪽이 남긴다.
         */
        [[nodiscard]] virtual optional<uint8> openPlayers( bool bAutoPlay );
        /** @brief 한 프레임만큼 판을 진행합니다. 여기서는 일시정지가 아니면 배속대로 한 번이고, 2 인 대전 파생은 모인 락스텝 틱만큼이다. */
        virtual void   stepMatch( GameSeconds frameTime );
        SkirmishMatch& getMutableMatch() { return _match; }
        /** @brief 씬이 적은 지도입니다(플레이어 수 — 판을 열기 전에도 읽는다). */
        const SkirmishMap& getMap() const { return _map; }

    private:
        /** @brief 컴퓨터 상대(`SkirmishMatch::findAI`)마다 실행기 하나를 내놓는 어댑터입니다. 리플렉션 컴포넌트가 인터페이스를 직접 상속하지 않게 멤버로 둔다. */
        class CommanderDebugSource final : public IBehaviorTreeDebugSource
        {
        public:
            explicit CommanderDebugSource( const SkirmishGameModeComponent& owner )
                : _owner{ owner }
            {
            }
            void collectBehaviorTreeRunners( vector<BehaviorTreeDebugRunner>& outList ) const override;

        private:
            const SkirmishGameModeComponent& _owner;
        };

    private:
        PROPERTY( Category = "Data", DisplayName = "Commander Tree", Tooltip = "Behavior tree (.bt.xml) of the computer opponent; its task names are the rts.* tasks of the RTS kit" )
        AssetRef<BehaviorTreeAsset> _commanderTreePath;
        PROPERTY( Category = "Map", DisplayName = "Map", Tooltip = "Ground, player starts and first units of the match; required" )
        SkirmishMap _map;
        PROPERTY( Required, Category = "Rules", DisplayName = "Match", Tooltip = "Rules, AI units and AI styles of the match; every value is required" )
        SkirmishMatchSettings _matchSettings;
        PROPERTY( Category = "Time", DisplayName = "Speed Range", Tooltip = "Slowest and fastest time scale and the factor of one step; required" )
        GameSpeedRange _speedRange;

        BoundAssetRef<BehaviorTreeAsset>  _pCommanderTree;
        SW_TRANSIENT CommanderDebugSource _commanderDebugSource;
        SW_CODEC_STATE SkirmishMatch      _match;
        SW_CODEC_STATE vector<RTSEvent> _listStepEvent;
        SW_CODEC_STATE GameSpeedControl _speed;                       ///< 배속 · 일시정지(2 인 대전에서는 쓰지 않는다)
        SW_CODEC_STATE uint64           _matchID;                     ///< `getMatchID`
        SW_TRANSIENT uint8              _bAutoPlayChangeReported : 1; ///< 판이 열린 뒤 자동 플레이가 바뀐 것을 한 번 알렸다
        [[maybe_unused]] uint8          _reserved                : 7;
    };
} // namespace sw
