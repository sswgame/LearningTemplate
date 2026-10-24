/**
 * @file VoxelPlayerComponent.h
 * @brief 복셀 월드의 1인칭 몸과 손(폰 쪽)입니다. 걷기, 점프, 헤엄(몸 충돌), 바라보는 블록 부수기(단단할수록 오래 걸립니다), 블록 놓기, 핫바를 맡습니다. 같은 오브젝트에 있는 폰의 의도만 읽습니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/GameObjectHandle.h"
#include "Core/Container/vector.h"
#include "Core/Math/Math.h"
#include "Core/String/hashed_string.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Base/Foundation/Framework/Presentation/GameSound.h"
#include "GameFramework/Base/Foundation/Utility/Time/Countdown.h"
#include "GameFramework/Kits/Feature/World/Voxel/Rule/VoxelBody.h"
#include "GameFramework/Kits/Feature/World/Voxel/Rule/VoxelHotbar.h"
#include "GameFramework/Kits/Feature/World/Voxel/Rule/VoxelRaycast.h"

namespace sw
{
    class Archive;
    class FirstPersonCameraComponent;
    class PawnComponent;
    class VoxelBlockCatalog;
    class VoxelDirectorComponent;

    /**
     * @class VoxelPlayerComponent
     * @brief 플레이어 오브젝트의 게임 규칙을 맡습니다. 플레이어 오브젝트는 카메라(`FirstPersonCameraComponent`)와 같은 오브젝트입니다.
     * @details 기본 틱 그룹(`DuringPhysics`)에서 실행합니다. 같은 오브젝트의 1인칭 카메라가 `PrePhysics` 에서 폰의 조종 회전으로 시점을 정해 두므로,
     *          이 컴포넌트는 그 시점으로 걷고 블록을 겨눈 다음 `setEyePosition` 으로 눈 위치를 넘깁니다.
     *          **입력은 직접 읽지 않습니다.** 같은 오브젝트의 `PawnComponent` 에 있는 의도만 읽습니다.
     *          의도는 이동, 버튼(`Voxel.Jump`, `Voxel.Sprint`, `Voxel.Break`, `Voxel.Place`, `Voxel.Slot1..9`), 아날로그(`Voxel.HotbarScroll`)입니다.
     *          이 의도를 내는 쪽은 플레이어 조종자(입력 맵)이거나, 디렉터가 빙의시킨 자동 플레이 AI(`VoxelAutoPlayControllerComponent`)입니다.
     *          월드는 디렉터가 가진 것을 읽기만 합니다. 블록 바꾸기와 효과음은 모아 두었다가 틱이 끝난 뒤 게임 스레드에서 디렉터에게 넘기고,
     *          디렉터가 월드를 바꾸고 청크의 다시 메싱을 요청합니다. 블록 카탈로그는 게임 서비스(`VoxelBlockCatalog`)로 읽습니다.
     */
    REFLECT( Category = "VoxelCraft", DisplayName = "Voxel Player", Tooltip = "First-person voxel body, block breaking and placing, and the hotbar" )
    class VoxelPlayerComponent : public Component
    {
    public:
        REFLECT_BODY();

        VoxelPlayerComponent();
        virtual ~VoxelPlayerComponent() override;

        void onBeginPlay() override;
        void onTick( float32 deltaTime ) override;

        // ---- 다른 컴포넌트가 읽는 값입니다(이 컴포넌트가 쓰지 않는 그룹에서 읽습니다) ----
        const VoxelBody&   getBody() const { return _body; }
        const VoxelHotbar& getHotbar() const { return _hotbar; }
        /** @brief 폰 의도의 버튼 이름입니다. 씬 데이터에서 읽으며, 자동 플레이 AI 도 같은 이름의 버튼을 누릅니다. */
        const hashed_string& getJumpButton() const { return _jumpButton; }
        const hashed_string& getBreakButton() const { return _breakButton; }
        const hashed_string& getPlaceButton() const { return _placeButton; }
        /** @brief 바라보는 블록입니다. 없으면 false 입니다. */
        bool findTarget( VoxelCoord& outBlock ) const;
        /** @brief 몸의 위치, 핫바, 부순 블록 수와 놓은 블록 수를 기록합니다. `ComponentStateStore::capture` 가 부릅니다. */
        void writeState( Archive& outArchive ) const;
        /** @brief `writeState` 가 기록한 바이트로 복원합니다. 플레이 시작 전이면 보관했다가 `onBeginPlay` 끝에 적용합니다. 읽지 못하면 처음 위치에서 시작합니다. */
        void    restoreState( vector<uint8>&& bytes );
        float32 getBreakProgress() const { return _breakProgress; }
        uint32  getBrokenCount() const { return _brokenCount; }
        uint32  getPlacedCount() const { return _placedCount; }
        /** @brief 몸을 처음 놓은 위치입니다. 테스트와 탐침이 걸은 거리를 측정할 때 기준으로 씁니다. */
        const float3&           getStartPosition() const { return _startPosition; }
        const GameObjectHandle& getDirector() const { return _director; }

    private:
        /** @brief 바꿀 블록 하나입니다. 틱이 끝난 뒤 디렉터가 월드에 반영합니다. */
        struct BlockEdit
        {
            VoxelCoord      _coord{};
            VoxelBlockIndex _block{ kVoxelAirBlock };
        };

        /** @brief 폰 스키마 안의 슬롯 번호입니다. 플레이 시작 때 이름으로 한 번 찾아 둡니다(없으면 −1). */
        struct IntentSlots
        {
            int32 _jump{ -1 };
            int32 _sprint{ -1 };
            int32 _break{ -1 };
            int32 _place{ -1 };
            int32 _arrSlot[VoxelHotbar::kSlotCount]{ -1, -1, -1, -1, -1, -1, -1, -1, -1 };
            int32 _hotbarScroll{ -1 };
        };

    private:
        void initializeBody( const VoxelDirectorComponent& director );
        /** @brief 폰의 의도 이름(PROPERTY)을 슬롯 번호로 바꿉니다. 폰 스키마에 없는 이름은 모두 오류로 남기고 false 를 반환합니다. */
        [[nodiscard]] bool resolveIntentSlots( const PawnComponent& pawn );
        /** @brief 블록 이름(PROPERTY: 물, 처음 핫바)이 카탈로그에 모두 있으면 핫바와 몸에 넣고 true 를 반환합니다. 하나라도 없으면 오류를 남기고 false 를 반환합니다. */
        [[nodiscard]] bool applyBlockNames( const VoxelBlockCatalog& catalog );
        /** @brief 의도에서 이번 틱의 이동과 손동작을 읽고 핫바 슬롯을 고릅니다. */
        void readIntent( const PawnComponent& pawn, const VoxelBlockCatalog& catalog, float3& outWish, bool& outJump, bool& outSprint, bool& outBreak, bool& outPlace );
        void updateTarget( const VoxelDirectorComponent& director, const FirstPersonCameraComponent& camera );
        void updateBreaking( float32 deltaTime, const VoxelBlockCatalog& catalog, bool bBreakHeld );
        void placeBlock( const VoxelDirectorComponent& director, const VoxelBlockCatalog& catalog );
        void scheduleFlush();
        void flushPending();
        /** @brief 보관해 둔 복원 바이트를 적용합니다. */
        void applyPendingState();

    private:
        PROPERTY( Category = "Player", DisplayName = "Director", Tooltip = "Object with the VoxelDirectorComponent" )
        GameObjectHandle _director;
        PROPERTY( Category = "Player", DisplayName = "Reach", Tooltip = "Blocks within this distance can be broken or built on", Min = 0.0, Units = m )
        float32 _reachDistance;
        PROPERTY( Category = "Player", DisplayName = "Place Interval", Tooltip = "Seconds between placements while the button is held", Min = 0.0, Units = s )
        float32 _placeInterval;
        PROPERTY( Category = "Player", DisplayName = "Start Yaw", Units = rad )
        float32 _startYaw;
        PROPERTY( Category = "Player", DisplayName = "Start Pitch", Units = rad )
        float32 _startPitch;
        // ---- 폰 의도 이름입니다. 같은 오브젝트의 PawnComponent 스키마(_listButton, _listAnalog)에 있어야 합니다 ----
        PROPERTY( Category = "Intent", DisplayName = "Jump Button" )
        hashed_string _jumpButton;
        PROPERTY( Category = "Intent", DisplayName = "Sprint Button" )
        hashed_string _sprintButton;
        PROPERTY( Category = "Intent", DisplayName = "Break Button" )
        hashed_string _breakButton;
        PROPERTY( Category = "Intent", DisplayName = "Place Button" )
        hashed_string _placeButton;
        PROPERTY( Category = "Intent", DisplayName = "Hotbar Scroll Analog" )
        hashed_string _hotbarScrollAnalog;
        PROPERTY( Category = "Intent", DisplayName = "Slot Buttons", Tooltip = "One button per hotbar slot" )
        vector<hashed_string> _listSlotButton;
        // ---- 블록 이름입니다. 블록 카탈로그(data/blocks.xml)의 id 입니다 ----
        PROPERTY( Category = "Blocks", DisplayName = "Water Block", Tooltip = "Block the body swims in" )
        hashed_string _waterBlock;
        PROPERTY( Category = "Blocks", DisplayName = "Start Blocks", Tooltip = "Blocks in the first hotbar (64 each)" )
        vector<hashed_string> _listStartBlock;

        VoxelBody         _body;
        VoxelHotbar       _hotbar;
        VoxelRayHit       _target;
        vector<BlockEdit> _listPendingEdit;
        GameSoundQueue    _soundQueue;        ///< 재생할 소리입니다. 오디오는 게임 스레드에서 틱이 끝난 뒤 재생합니다.
        vector<uint8>     _pendingStateBytes; ///< 플레이 시작 전에 받은 복원 바이트입니다(`restoreState`).
        IntentSlots       _intentSlots;
        float3            _startPosition; ///< 몸을 처음 놓은 위치
        float32           _breakProgress;
        Countdown         _placeCooldown; ///< 버튼을 누르고 있을 때의 놓기 간격입니다(늦은 만큼 이어서 셉니다).
        uint32            _brokenCount;
        uint32            _placedCount;
        uint8             _bHasTarget      : 1;
        uint8             _bBodyPlaced     : 1; ///< 월드가 생성된 뒤 첫 땅에 몸을 놓았는지입니다.
        uint8             _bFlushScheduled : 1;
        uint8             _reserved        : 5;
    };
} // namespace sw
