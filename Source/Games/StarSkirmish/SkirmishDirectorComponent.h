/**
 * @file SkirmishDirectorComponent.h
 * @brief StarSkirmish 의 판을 돌리는 컴포넌트입니다. 한 판(`SkirmishMatch`), 선택, 명령, 생산, 건설, 부대 입력, 속도, 알림을 맡고 절벽과 유닛(프리팹) 스폰을 지시합니다.
 * @details 언리얼의 GameMode/GameState 에 해당합니다. 씬에 하나만 둡니다. 규칙(채취, 생산, 전투, 안개, AI)은 키트의 `RTSWorld`, `RTSCommanderAI`,
 *          `SkirmishMatch` 가 맡고, 이 컴포넌트는 사람의 입력과 무엇을 어디에 스폰할지를 안다. 유닛 모습은 뷰(`SkirmishUnitComponent`)가, 끌어서 선택하는 상자는
 *          `SkirmishDragComponent` 가 이 컴포넌트를 **읽기만** 해서 맞춘다.
 *
 *          틱 규칙은 다음과 같다. 디렉터는 `TickGroup::PrePhysics` 에서 상태를 쓰고, 뷰와 카메라 리그는 `PostUpdate` 에서 읽는다(그룹은 차례로 돈다).
 *          스폰은 틱 안에서 할 수 없으므로 보이기 시작한 유닛과 사라진 유닛의 슬롯을 쌓아 두었다가 `executeOrDeferPostTick` 한 번으로 틱이 끝난 뒤에 스폰하고 지운다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/GameObjectHandle.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Math/Math.h"
#include "Core/Memory/Memory.h"
#include "Core/String/hashed_string.h"

#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/Resource/AssetHandle.h"

#include "GameFramework/Base/Actor/AI/BehaviorTreeAsset.h"
#include "GameFramework/Base/Foundation/Framework/GameDirectorComponent.h"
#include "GameFramework/Base/Foundation/Framework/Presentation/MaterialTintCache.h"
#include "GameFramework/Kits/Genre/Strategy/RealTimeStrategy/RTSCatalog.h"
#include "GameFramework/Kits/Genre/Strategy/RealTimeStrategy/RTSSelection.h"

#include "Games/StarSkirmish/SkirmishMatch.h"

namespace sw
{
    class Archive;
    class CameraDirectorComponent;
    class GameObject;
    class GameObjectManager;
    class InputManager;
    class Material;
    class MaterialInstance;

    /**
     * @class SkirmishDirectorComponent
     * @brief 1 대 1 한 판을 진행합니다. 플레이가 시작되면 유닛 데이터를 읽고 판을 엽니다. 사람(파랑) 대 컴퓨터이며, 자동 플레이면 컴퓨터 대 컴퓨터입니다.
     * @details 판(월드, AI 진행, 선택, 속도)은 PROPERTY 가 아니라 `writeState` 로 게임 상태 스냅샷의 컴포넌트 섹션에 실려 핫 리로드와 세이브를
     *          넘긴다(`StarSkirmishGame`). 움직이던 유닛은 앞 명령의 경로를 다시 구한다. 스폰한 오브젝트는 핸들로 보관하고, 상태를 저장하기 전에 정리한다.
     */
    REFLECT( Category = "RealTimeStrategy", DisplayName = "Skirmish Director", Tooltip = "Runs the skirmish match, the human commands and spawns the unit views" )
    class SkirmishDirectorComponent : public GameDirectorComponent
    {
    public:
        REFLECT_BODY();

        SkirmishDirectorComponent();
        virtual ~SkirmishDirectorComponent() override;

        /** @brief 판을 열고 패드 가상 커서를 켭니다(왼쪽 스틱 = 커서, A/B = 마우스 왼쪽/오른쪽, 오른쪽 스틱 = 카메라 이동). 패드만으로 고르기 · 명령을 씁니다. */
        void onBeginPlay() override;
        /** @brief 가상 커서를 끄고 판을 닫습니다. */
        void onEndPlay() override;

        /** @brief 판(월드, AI 진행, 선택, 속도, 일시정지)을 씁니다. `ComponentStateStore::capture` 가 부릅니다. */
        void writeState( Archive& outArchive ) const override;

        // ---- 뷰가 읽는 것(PostUpdate. 디렉터가 쓰지 않는 그룹) ----
        const RTSWorld&      getWorld() const { return _match.getWorld(); }
        const SkirmishMatch& getMatch() const { return _match; }
        bool                 isSelected( RTSUnitID unitID ) const { return _selection.isSelected( unitID ); }
        /** @brief 유닛 주인(또는 자원 종류)과 선택 여부를 반환합니다. 아직 없으면 비어 있습니다. */
        const shared_ptr<MaterialInstance>& findUnitLook( const RTSUnit& unit, bool bSelected ) const;
        /** @brief 끌어서 선택하는 중이면 true 를 반환하고, 땅 위 상자의 중심과 크기를 채웁니다. */
        bool findDragBox( float3& outCenter, float3& outScale ) const;

    protected:
        /** @brief 유닛 데이터를 읽고 판을 엽니다. 사람(파랑) 대 컴퓨터이며, 자동 플레이면 컴퓨터 대 컴퓨터입니다. */
        [[nodiscard]] bool startGame() override;
        /** @brief `writeState` 의 바이트를 읽습니다. 끝까지 맞지 않으면 false 를 반환합니다(판이 반쯤 바뀌었을 수 있으므로 `onStateRestored` 가 새 판으로 되돌립니다). */
        [[nodiscard]] bool readState( Archive& archive ) override;
        void               onStateRestored( bool bRestored ) override;
        void               onGameStarted() override;
        void               updateGame( float32 deltaTime ) override;
        void               onFlush( GameObjectManager& manager, bool bRespawnViews ) override;
        void               onViewsDespawned() override;
        bool               hasPendingSpawn() const override { return _listPendingUnit.empty() == false; }

    private:
        /** @brief 유닛 ID 슬롯 하나의 모습입니다. 틱이 보일 유닛을 정하고, 틱이 끝난 뒤 오브젝트를 맞춥니다. */
        struct UnitSlot
        {
            RTSUnitID        _shownID{}; ///< 스폰했거나 스폰할 유닛(무효면 없다).
            GameObjectHandle _object{};
            uint32           _stamp{ 0 };
        };

    private:
        void spawnCliffs( GameObjectManager& manager );
        void spawnUnit( GameObjectManager& manager, int32 slotIndex );
        /** @brief 유닛 모습 인스턴스(팀 또는 자원과 선택 여부 조합)를 만듭니다(뷰가 워커에서 고릅니다). */
        void prepareUnitLooks( Material* pMaterial );

        /** @brief 보일 유닛(안개, 죽음)과 스폰한 모습을 비교해 바뀐 슬롯을 쌓습니다(PrePhysics). */
        void collectUnitChanges();
        void updateInput( const InputManager& input );
        void updateHumanCommands( const InputManager& input );
        void updateDrag( const InputManager& input, const float3& point, bool bPointValid );
        /** @brief 명령 액션(`_orderAction`, 오른쪽 클릭)입니다. 선택한 유닛에 커서 위치로 상황에 맞는 명령(이동, 공격, 채취, 집결)을 냅니다. */
        void issueOrder( const float3& point, bool bQueue );
        void orderBuild( const hashed_string& buildingID, const float3& point );
        /**
         * @brief 씬 데이터가 준 이름(입력 액션 · AI 가 쓰는 유닛 id · 건설 단축키 표)이 입력 맵과 유닛 카탈로그에 모두 있는지 봅니다.
         * @details 빠진 것은 모두 오류로 남기고 false 다 — 판을 열지 않는다. 입력 관리자가 없으면(전용 서버 · 헤드리스) 액션은 보지 않는다.
         */
        [[nodiscard]] bool hasValidNames() const;
        void               trainFromPrimary( int32 productIndex );
        void               handleEvents();
        /** @brief 포인터가 가리키는 땅(y = 0) 위치를 반환합니다(`getMousePositionNormalized`). 맵 밖이면 false 입니다. */
        [[nodiscard]] bool       findGroundPoint( const InputManager& input, float3& outPoint ) const;
        CameraDirectorComponent* findCameraRig() const;

    private:
        PROPERTY( Category = "Data", DisplayName = "Unit Data", AssetPath, Tooltip = "Unit catalog XML" )
        string _unitDataPath;
        PROPERTY( Category = "Data", DisplayName = "Commander Tree", AssetPath, Tooltip = "Behavior tree (.bt.xml) of the computer opponent; its task names are the rts.* tasks of the RTS kit" )
        string _commanderTreePath;
        PROPERTY( Category = "Prefabs", AssetPath, AssetType = "Prefab" )
        string _cliffPrefab;
        PROPERTY( Category = "Prefabs", AssetPath, AssetType = "Prefab" )
        string _unitPrefab;
        PROPERTY( Category = "Scene", DisplayName = "Camera Rig", Tooltip = "Object with the CameraDirectorComponent the mouse is picked through" )
        GameObjectHandle _cameraRig;
        PROPERTY( Category = "Scene", DisplayName = "Watch Focus", Tooltip = "Camera focus when both players are AI (the whole map); the height is the Watch Camera Preset", Units = m )
        float3 _watchFocus;
        PROPERTY( Category = "Scene", DisplayName = "Watch Camera Preset", Tooltip = "Camera preset id (camera rig catalog) the match starts with when both players are AI; required" )
        hashed_string _watchCameraPreset;
        PROPERTY( Category = "Rules", DisplayName = "Hunt Interval", Tooltip = "Seconds between sending idle army at the enemy start to the nearest enemy building", Min = 0.1 )
        float32 _huntIntervalSeconds;
        PROPERTY( Category = "Rules", DisplayName = "Arrived Radius", Tooltip = "Army within this distance of the enemy start counts as arrived", Min = 0.0, Units = m )
        float32 _arrivedRadius;
        PROPERTY( Category = "Rules", DisplayName = "Start Minerals", Min = 0 )
        int32 _startMinerals;
        PROPERTY( Category = "Debug", DisplayName = "Status Log Interval", Tooltip = "Seconds between match status log lines", Min = 0.1, Units = s )
        float32 _statusLogInterval;
        PROPERTY( Category = "Input", DisplayName = "Click Slop", Tooltip = "A drag shorter than this counts as a click", Min = 0.0, Units = m )
        float32 _clickSlop;
        PROPERTY( Category = "Input", DisplayName = "Max Selection", Tooltip = "Most units one selection holds", Min = 1 )
        int32 _maxSelectionCount;
        // ---- 입력 액션 — 이름은 입력 맵(data/skirmish.input.xml)의 액션이다. 판을 열 때 모두 있는지 본다(hasValidNames) ----
        PROPERTY( Category = "Input Actions", DisplayName = "Select", Tooltip = "Picks a unit or starts a drag box" )
        hashed_string _selectAction;
        PROPERTY( Category = "Input Actions", DisplayName = "Order", Tooltip = "Context order at the cursor (move, attack, gather, rally)" )
        hashed_string _orderAction;
        PROPERTY( Category = "Input Actions", DisplayName = "Add To Selection", Tooltip = "Held: select adds, order queues, group number adds" )
        hashed_string _addToSelectionAction;
        PROPERTY( Category = "Input Actions", DisplayName = "Group Modifier", Tooltip = "Held: a group number assigns the selection" )
        hashed_string _groupModifierAction;
        PROPERTY( Category = "Input Actions", DisplayName = "Attack Move" )
        hashed_string _attackMoveAction;
        PROPERTY( Category = "Input Actions", DisplayName = "Hold" )
        hashed_string _holdAction;
        PROPERTY( Category = "Input Actions", DisplayName = "Stop" )
        hashed_string _stopAction;
        PROPERTY( Category = "Input Actions", DisplayName = "Jump To Selection", Tooltip = "Moves the camera to the primary selected unit" )
        hashed_string _jumpToSelectionAction;
        PROPERTY( Category = "Input Actions", DisplayName = "Faster" )
        hashed_string _fasterAction;
        PROPERTY( Category = "Input Actions", DisplayName = "Slower" )
        hashed_string _slowerAction;
        PROPERTY( Category = "Input Actions", DisplayName = "Pause" )
        hashed_string _pauseAction;
        PROPERTY( Category = "Input Actions", DisplayName = "Status", Tooltip = "Logs the match status" )
        hashed_string _statusAction;
        PROPERTY( Category = "Input Actions", DisplayName = "Player Pan", Tooltip = "Camera pan while a human plays (letter keys are commands)" )
        hashed_string _playerPanAction;
        PROPERTY( Category = "Input Actions", DisplayName = "Spectator Pan", Tooltip = "Camera pan while both players are AI" )
        hashed_string _spectatorPanAction;
        PROPERTY( Category = "Input Actions", DisplayName = "Train Actions", Tooltip = "One action per product of the primary selected building, in catalog order" )
        vector<hashed_string> _listTrainAction;
        PROPERTY( Category = "Input Actions", DisplayName = "Group Actions", Tooltip = "One action per control group (0..9)" )
        vector<hashed_string> _listGroupAction;
        PROPERTY( Category = "Input Actions", DisplayName = "Build Actions", Tooltip = "Build hotkey actions; Build Units holds the building each one places" )
        vector<hashed_string> _listBuildAction;
        PROPERTY( Category = "Input Actions", DisplayName = "Build Units", Tooltip = "Unit catalog id placed by the build action at the same index" )
        vector<hashed_string> _listBuildUnitID;
        // ---- AI 가 쓰는 유닛 — 유닛 카탈로그(data/units.xml)의 id 다 ----
        PROPERTY( Category = "AI Units", DisplayName = "Worker" )
        hashed_string _workerUnitID;
        PROPERTY( Category = "AI Units", DisplayName = "Depot", Tooltip = "Resource drop-off building" )
        hashed_string _depotUnitID;
        PROPERTY( Category = "AI Units", DisplayName = "Supply", Tooltip = "Supply building" )
        hashed_string _supplyUnitID;
        PROPERTY( Category = "AI Units", DisplayName = "Production", Tooltip = "Building that trains the army unit" )
        hashed_string _productionUnitID;
        PROPERTY( Category = "AI Units", DisplayName = "Army", Tooltip = "Unit the AI trains and attacks with" )
        hashed_string _armyUnitID;
        // ---- 컴퓨터 상대의 성향 — 셋 다 씬 데이터에 적는다(적지 않으면 판을 열지 않는다) ----
        PROPERTY( Category = "AI Styles", DisplayName = "Rush AI", Tooltip = "Player 0 when both players are AI: few workers, early small attacks" )
        SkirmishAIStyle _rushAI;
        PROPERTY( Category = "AI Styles", DisplayName = "Macro AI", Tooltip = "Player 1 when both players are AI: more workers and production, later big attacks" )
        SkirmishAIStyle _macroAI;
        PROPERTY( Category = "AI Styles", DisplayName = "Opponent AI", Tooltip = "The computer player against a human" )
        SkirmishAIStyle _opponentAI;
        PROPERTY( Category = "Look", DisplayName = "Player 0 Color", Tooltip = "Tint of player 0's units (multiplies the kit's white body)" )
        float4 _playerZeroColor;
        PROPERTY( Category = "Look", DisplayName = "Player 1 Color" )
        float4 _playerOneColor;
        PROPERTY( Category = "Look", DisplayName = "Neutral Color", Tooltip = "Tint of units no player owns" )
        float4 _neutralColor;
        PROPERTY( Category = "Look", DisplayName = "Mineral Color" )
        float4 _mineralColor;
        PROPERTY( Category = "Look", DisplayName = "Gas Color" )
        float4 _gasColor;

        RTSCatalog                           _catalog;
        BehaviorTreeAsset                    _commanderTree;
        SkirmishMatch                        _match;
        RTSSelection                         _selection;
        vector<RTSEvent>                     _listEvent;
        vector<UnitSlot>                     _listUnitSlot;
        vector<int32>                        _listPendingUnit; ///< 모습을 맞출 유닛 슬롯(틱이 끝난 뒤 처리).
        MaterialTintCache                    _tintCache;       ///< 유닛 색(같은 색은 나눠 쓴다).
        vector<shared_ptr<MaterialInstance>> _listUnitLook;    ///< (팀 또는 자원 종류) x 2 + 선택 여부로 만든 유닛 모습 인스턴스.
        AssetReadySet                        _unitModels;      ///< 모든 유닛 모델 — 판을 열 때 워커가 읽어 둔다(처음 짓는 건물 · 유닛이 게임 스레드에서 디스크를 읽지 않게). 집합이 핸들을 쥐어 약한 참조 캐시에 남긴다.
        float3                               _dragStart;
        float3                               _dragPoint;
        float32                              _timeScale;
        uint32                               _frameStamp;
        uint8                                _bHuman             : 1;
        uint8                                _bDragging          : 1;
        uint8                                _bDragPointValid    : 1;
        uint8                                _bAttackMovePending : 1;
        uint8                                _bPaused            : 1;
        uint8                                _reserved           : 3;
    };
} // namespace sw
