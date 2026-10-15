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

#include "Engine/Graphics/Mesh/MeshPreloadSet.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Base/Foundation/Framework/GameDirectorComponent.h"
#include "GameFramework/Base/Foundation/Framework/Presentation/MaterialTintCache.h"
#include "GameFramework/Kits/Genre/Strategy/RealTimeStrategy/RTSCatalog.h"
#include "GameFramework/Kits/Genre/Strategy/RealTimeStrategy/RTSSelection.h"

#include "Games/StarSkirmish/SkirmishMatch.h"

namespace sw
{
    class Archive;
    class GameObject;
    class GameObjectManager;
    class InputManager;
    class Material;
    class MaterialInstance;
    class OrthoCameraRigComponent;

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

        /** @brief 판을 열고 패드 가상 커서를 켭니다(오른쪽 스틱 = 커서, A/B = 마우스 왼쪽/오른쪽). 패드만으로 고르기 · 명령을 씁니다. */
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
        /** @brief `Skirmish.Order`(오른쪽 클릭)입니다. 선택한 유닛에 커서 위치로 상황에 맞는 명령(이동, 공격, 채취, 집결)을 냅니다. */
        void issueOrder( const float3& point, bool bQueue );
        void orderBuild( const utf8* pBuildingID, const float3& point );
        void trainFromPrimary( int32 productIndex );
        void handleEvents();
        /** @brief 포인터가 가리키는 땅(y = 0) 위치를 반환합니다(`getMousePositionNormalized`). 맵 밖이면 false 입니다. */
        [[nodiscard]] bool       findGroundPoint( const InputManager& input, float3& outPoint ) const;
        OrthoCameraRigComponent* findCameraRig() const;

    private:
        PROPERTY( Category = "Data", DisplayName = "Unit Data", AssetPath, Tooltip = "Unit catalog XML" )
        string _unitDataPath;
        PROPERTY( Category = "Prefabs", AssetPath, AssetType = "Prefab" )
        string _cliffPrefab;
        PROPERTY( Category = "Prefabs", AssetPath, AssetType = "Prefab" )
        string _unitPrefab;
        PROPERTY( Category = "Scene", DisplayName = "Camera Rig", Tooltip = "Object with the OrthoCameraRigComponent the mouse is picked through" )
        GameObjectHandle _cameraRig;
        PROPERTY( Category = "Scene", DisplayName = "Watch Focus", Tooltip = "Camera focus when both players are AI (the whole map)", Units = m )
        float3 _watchFocus;
        PROPERTY( Category = "Scene", DisplayName = "Watch Ortho Height", Tooltip = "Camera height when both players are AI", Min = 0.1, Units = m )
        float32 _watchOrthoHeight;
        PROPERTY( Category = "Rules", DisplayName = "Hunt Interval", Tooltip = "Seconds between sending idle army at the enemy start to the nearest enemy building", Min = 0.1 )
        float32 _huntIntervalSeconds;
        PROPERTY( Category = "Rules", DisplayName = "Arrived Radius", Tooltip = "Army within this distance of the enemy start counts as arrived", Min = 0.0, Units = m )
        float32 _arrivedRadius;
        PROPERTY( Category = "Rules", DisplayName = "Start Minerals", Min = 0 )
        int32 _startMinerals;
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
        SkirmishMatch                        _match;
        RTSSelection                         _selection;
        vector<RTSEvent>                     _listEvent;
        vector<UnitSlot>                     _listUnitSlot;
        vector<int32>                        _listPendingUnit; ///< 모습을 맞출 유닛 슬롯(틱이 끝난 뒤 처리).
        MaterialTintCache                    _tintCache;       ///< 유닛 색(같은 색은 나눠 쓴다).
        vector<shared_ptr<MaterialInstance>> _listUnitLook;    ///< (팀 또는 자원 종류) x 2 + 선택 여부로 만든 유닛 모습 인스턴스.
        MeshPreloadSet                       _unitModels;      ///< 모든 유닛 모델 — 판을 열 때 워커가 읽어 둔다(처음 짓는 건물 · 유닛이 게임 스레드에서 디스크를 읽지 않게).
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
