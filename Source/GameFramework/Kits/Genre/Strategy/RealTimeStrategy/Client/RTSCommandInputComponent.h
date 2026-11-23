/**
 * @file RTSCommandInputComponent.h
 * @brief RTS 키트의 클라이언트 쪽 입력 — 이 화면의 사람이 유닛을 고르고(끌기 · 클릭 · 부대) 명령 · 생산 · 건설 · 속도 입력을 디렉터(`SkirmishGameModeComponent`)의 명령으로 넘기며, 판 알림을 소리 · 로그로 냅니다.
 * @details 키트 세 갈래의 클라이언트 몫이다(`GF_Client_RealTimeStrategy`, 전용 서버는 짓지 않는다). 디렉터의 명령(`submitCommand` · `submitTimeInput`)과 읽기 API 만 쓰고 월드(규칙)를 직접 고치지 않는다.
 *          디렉터는 같은 오브젝트에 먼저 붙인다 — 등록(`onRegister`)에서 찾아 핸들로 든다. 언리얼의 PlayerController(선택 · 명령) 자리다.
 *          명령은 틱 뒤 게임 스레드에서 낸다. 이 컴포넌트는 `PostUpdate` 라 디렉터 걸음 뒤에 돌아, 넘긴 명령은 다음 걸음이 쓴다.
 *
 *          **저장 계약**: 고른 유닛 · 부대는 이 화면 사람의 것이라 판 상태(디렉터)가 아니라 이 컴포넌트의 구간이다 — 세이브 · 핫 리로드는 넘기고(스타크래프트 II 의 세이브가 부대 지정을 지키는 것과 같다),
 *          락스텝 체크섬과 전용 서버의 판 상태에는 없다. 유닛 id 는 같은 세이브가 되살린 월드의 것이고, 죽은 id 는 다음 프레임이 걸러 낸다. 판을 새로 열면 비운다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Encoding/FourCCUtil.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/Math.h"
#include "Core/Name/hashed_string.h"

#include "Engine/Foundation/ComponentHandle.h"
#include "Engine/Foundation/GameObjectHandle.h"
#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/Serialization/Format/StateIO.h"
#include "Engine/Serialization/Serializer/CatalogKey.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Genre/Strategy/RealTimeStrategy/Client/RTSSelection.h"
#include "GameFramework/Kits/Genre/Strategy/RealTimeStrategy/RTSCatalog.h"

namespace sw
{
    class CameraDirectorComponent;
    struct CameraPresetDef;
    class GameInputView;
    class GameObjectManager;
    class InputManager;
    class SkirmishGameModeComponent;

    /**
     * @class RTSCommandInputComponent
     * @brief 이 화면의 고르기와 명령 입력입니다. 구경(모두 컴퓨터)이면 카메라 이동 · 속도 · 현황만 받습니다.
     */
    REFLECT( Category = "RealTimeStrategy", DisplayName = "RTS Command Input",
             Tooltip = "Client side of a skirmish: selects units and sends orders, training, building and speed input to the Skirmish Game Mode on the same object" )
    class SW_KIT_CLIENT_API RTSCommandInputComponent : public Component
    {
    public:
        REFLECT_BODY();

        /** @brief 고르기 · 끌기는 틱 뒤 게임 스레드에서만 바뀝니다 — 유닛 뷰 · 끌기 상자가 병렬 틱에서 읽어도 됩니다(자기 틱이 쓰는 것은 따로 둔 `_bInputLive` 뿐이다). */
        static constexpr bool   kTickReadSafe = true;
        static constexpr uint32 kStateTag     = FourCCUtil::make( "RTSI" );
        static constexpr uint32 kStateVersion = 1;

        RTSCommandInputComponent();
        virtual ~RTSCommandInputComponent() override;

        /** @brief 같은 오브젝트의 디렉터를 찾아 핸들로 듭니다. 없으면 오류를 남기고 아무것도 하지 않습니다. */
        void onRegister( GameObjectManager& manager ) override;
        /** @brief 틱 그룹은 뷰의 `PostUpdate` 입니다. 고르기는 지우지 않습니다(복원이 먼저 올 수 있다). */
        void onBeginPlay() override;
        /** @brief 알림 · 입력 처리를 틱 뒤 게임 스레드로 미룹니다. */
        void onTick( TickContext& context ) override;

        /** @brief 고른 유닛 · 부대를 씁니다(`ComponentStateStore::capture`). 읽기는 사본에 받아 끝까지 맞으면 옮깁니다. */
        SW_DECLARE_STATE_IO_IN_PLACE();
        /** @brief `writeState` 가 쓴 바이트로 고르기를 되돌립니다. 읽지 못하면 알리고 고르기를 그대로 둡니다. */
        void restoreState( vector<uint8>&& bytes );

        // ---- 뷰 · 감시 값이 읽는 것 ----
        bool isSelected( RTSUnitID unitID ) const { return _selection.isSelected( unitID ); }
        /** @brief 끌어서 선택하는 중이면 true 를 반환하고, 땅 위 상자의 중심과 크기를 채웁니다. */
        [[nodiscard]] bool  findDragBox( float3& outCenter, float3& outScale ) const;
        const RTSSelection& getSelection() const { return _selection; }
        /** @brief 같은 오브젝트의 디렉터입니다(감시 값). 없으면 nullptr 입니다. 틱 밖에서만 부릅니다. */
        const SkirmishGameModeComponent* findGameMode() const;

    private:
        /** @brief 틱 뒤 게임 스레드: 새 판이면 맞추고, 알림을 내고, 시간이 흐르는 동안 입력을 명령으로 바꿉니다. */
        void updateInput( GameObjectManager& manager );
        /** @brief 새로 열린 판에 맞춥니다 — 앞 판의 고르기를 버리고 플레이어 · 상한을 정하고 데이터 이름을 보고 카메라를 맞춘 뒤 조작을 안내합니다. */
        void applyMatch( GameObjectManager& manager, const SkirmishGameModeComponent& director );
        /** @brief 씬 데이터가 준 이름(입력 액션 · 건설 단축키 표 · 관전 프리셋)이 입력 맵과 유닛 카탈로그에 모두 있는지 봅니다. 빠진 것은 모두 오류로 남기고 false 다 — 입력을 읽지 않는다. */
        [[nodiscard]] bool hasValidNames( const SkirmishGameModeComponent& director ) const;
        /** @brief 디렉터의 이번 걸음 알림을 소리 · 로그로 냅니다. 유닛이 부서지는 소리는 구경에서도 나고, 나머지는 이 화면 사람의 것만 알린다. */
        void playStepEvents( const SkirmishGameModeComponent& director ) const;
        void updateHumanCommands( GameObjectManager& manager, SkirmishGameModeComponent& director, const InputManager& input, const GameInputView& inputMap );
        void updateDrag( SkirmishGameModeComponent& director, const GameInputView& inputMap, const float3& point, bool bPointValid );
        /** @brief 명령 액션(`_orderAction`, 오른쪽 클릭)입니다. 선택한 유닛에 커서 위치로 상황에 맞는 명령(이동, 공격, 채취, 집결)을 냅니다. */
        void issueOrder( SkirmishGameModeComponent& director, const float3& point, bool bQueue );
        void orderBuild( SkirmishGameModeComponent& director, const hashed_string& buildingID, const float3& point );
        void trainFromPrimary( SkirmishGameModeComponent& director, int32 productIndex );
        /** @brief 포인터가 가리키는 땅(y = 0) 위치를 반환합니다(`getMousePositionNormalized`). 맵 밖이면 false 입니다. */
        [[nodiscard]] bool       findGroundPoint( GameObjectManager& manager, const SkirmishGameModeComponent& director, const InputManager& input, float3& outPoint ) const;
        CameraDirectorComponent* findCameraRig( GameObjectManager& manager ) const;

    private:
        PROPERTY( Category = "Scene", DisplayName = "Camera Rig", Tooltip = "Object with the CameraDirectorComponent the mouse is picked through" )
        GameObjectHandle _cameraRig;
        PROPERTY( Category = "Scene", DisplayName = "Watch Focus", Tooltip = "Camera focus when both players are AI (the whole map); the height is the Watch Camera Preset", Units = m )
        float3 _watchFocus;
        PROPERTY( Category = "Scene", DisplayName = "Watch Camera Preset", Tooltip = "Camera preset id (camera rig catalog) the match starts with when both players are AI; required" )
        CatalogKey<CameraPresetDef> _watchCameraPreset;
        PROPERTY( Category = "Input", DisplayName = "Click Slop", Tooltip = "A drag shorter than this counts as a click", Min = 0.0, Units = m )
        float32 _clickDragThreshold;
        PROPERTY( Category = "Input", DisplayName = "Max Selection", Tooltip = "Most units one selection holds", Min = 1 )
        int32 _maxSelectionCount;
        // ---- 입력 액션 — 이름은 입력 맵(팩의 묶음)의 액션이다. 판이 열리면 모두 있는지 본다(hasValidNames) ----
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
        vector<CatalogKey<RTSUnitDef>> _listBuildUnitID;

        TComponentHandle<SkirmishGameModeComponent> _director;
        RTSSelection                                _selection;
        float3                                      _dragStart;
        float3                                      _dragPoint;
        uint32                                      _matchRevision; ///< 맞춘 판(`SkirmishGameModeComponent::getMatchRevision`, 0 이면 아직)
        uint8                                       _bInputLive;    ///< 이번 틱의 시간이 흘렀다(멈춤 중에는 입력을 넘기지 않는다). 틱이 쓰므로 뷰가 읽는 비트와 나눈다
        uint8                                       _bNamesValid        : 1;
        uint8                                       _bDragging          : 1;
        uint8                                       _bDragPointValid    : 1;
        uint8                                       _bAttackMovePending : 1;
        [[maybe_unused]] uint8                      _reserved           : 4;
    };
} // namespace sw
