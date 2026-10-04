/**
 * @file VoxelPlayerComponent.h
 * @brief 복셀 월드의 1인칭 몸 · 손 — 걷기 · 점프 · 헤엄(몸 충돌), 바라보는 블록 부수기(단단함만큼 걸린다) · 놓기, 핫바입니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/GameObjectHandle.h"
#include "Core/Container/vector.h"
#include "Core/Math/Math.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Framework/GameSound.h"
#include "GameFramework/Kits/Simulation/Voxel/VoxelBody.h"
#include "GameFramework/Kits/Simulation/Voxel/VoxelHotbar.h"
#include "GameFramework/Kits/Simulation/Voxel/VoxelRaycast.h"

namespace sw
{
    class Archive;
    class FirstPersonCameraComponent;
    class InputManager;
    class VoxelBlockCatalog;
    class VoxelDirectorComponent;

    /**
     * @class VoxelPlayerComponent
     * @brief 플레이어 오브젝트(카메라 · `FirstPersonCameraComponent` 와 같은 오브젝트)의 게임 규칙입니다.
     * @details 기본 틱 그룹(`DuringPhysics`)에서 돕니다 — 같은 오브젝트의 1인칭 카메라가 `PrePhysics` 에서 마우스로 시점을 돌린 뒤라, 그 시점으로 걷고 겨눈 뒤
     *          `setEyePosition` 으로 눈 자리를 넣습니다. 월드는 디렉터의 것을 읽기만 하고, 블록 바꾸기 · 효과음은 쌓아 두고 틱 뒤 게임 스레드에서 디렉터에
     *          건넨다(디렉터가 월드를 바꾸고 청크 다시 짓기를 맡긴다). 블록 카탈로그는 게임 서비스(`VoxelBlockCatalog`)입니다.
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

        // ---- 다른 컴포넌트가 읽는 것(이 컴포넌트가 쓰지 않는 그룹) ----
        const VoxelBody&   getBody() const { return _body; }
        const VoxelHotbar& getHotbar() const { return _hotbar; }
        /** @brief 바라보는 블록입니다. 없으면 false 입니다. */
        bool findTarget( VoxelCoord& outBlock ) const;
        /** @brief 몸 자리 · 핫바 · 부순/놓은 수를 씁니다 — `ComponentStateStore::capture` 가 부릅니다. */
        void writeState( Archive& outArchive ) const;
        /** @brief `writeState` 의 바이트로 되살립니다. 플레이 시작 전이면 들고 있다가 `onBeginPlay` 끝에 적용합니다. 읽지 못하면 처음 자리에서 시작합니다. */
        void    restoreState( vector<uint8>&& bytes );
        float32 getBreakProgress() const { return _breakProgress; }
        uint32  getBrokenCount() const { return _brokenCount; }
        uint32  getPlacedCount() const { return _placedCount; }

    private:
        /** @brief 바꿀 블록 하나 — 틱 뒤에 디렉터가 월드에 쓴다. */
        struct BlockEdit
        {
            VoxelCoord      _coord{};
            VoxelBlockIndex _block{ kVoxelAirBlock };
        };

    private:
        void initializeBody( const VoxelDirectorComponent& director );
        void tickInput( const InputManager& input, const VoxelBlockCatalog& catalog, const FirstPersonCameraComponent& camera, float3& outWish, bool& outJump,
                        bool& outSprint, bool& outBreak, bool& outPlace );
        void tickAutoPlay( float32 deltaTime, const VoxelDirectorComponent& director, FirstPersonCameraComponent& camera, float3& outWish, bool& outJump,
                           bool& outBreak, bool& outPlace );
        void updateTarget( const VoxelDirectorComponent& director, const FirstPersonCameraComponent& camera );
        void updateBreaking( float32 deltaTime, const VoxelBlockCatalog& catalog, bool bBreakHeld );
        void placeBlock( const VoxelDirectorComponent& director, const VoxelBlockCatalog& catalog );
        void scheduleFlush();
        void flushPending();
        /** @brief 들고 있던 복원 바이트를 적용합니다. */
        void applyPendingState();

    private:
        PROPERTY( Category = "Player", DisplayName = "Director", Tooltip = "Object with the VoxelDirectorComponent" )
        GameObjectHandle _director;
        PROPERTY( Category = "Player", DisplayName = "Reach", Tooltip = "Blocks within this distance can be broken or built on", Min = 0.0, Meta = "Units=m" )
        float32 _reachDistance;
        PROPERTY( Category = "Player", DisplayName = "Place Interval", Tooltip = "Seconds between placements while the button is held", Min = 0.0, Meta = "Units=s" )
        float32 _placeInterval;
        PROPERTY( Category = "Player", DisplayName = "Start Yaw", Meta = "Units=rad" )
        float32 _startYaw;
        PROPERTY( Category = "Player", DisplayName = "Start Pitch", Meta = "Units=rad" )
        float32 _startPitch;

        VoxelBody         _body;
        VoxelHotbar       _hotbar;
        VoxelRayHit       _target;
        vector<BlockEdit> _listPendingEdit;
        GameSoundQueue    _soundQueue;        ///< 낼 소리(틱 뒤 — 오디오는 게임 스레드에서)
        vector<uint8>     _pendingStateBytes; ///< 플레이 시작 전에 받은 복원 바이트(`restoreState`)
        float32           _breakProgress;
        float32           _placeCooldown;
        float32           _autoTimer;
        uint32            _brokenCount;
        uint32            _placedCount;
        uint8             _bHasTarget      : 1;
        uint8             _bBodyPlaced     : 1; ///< 월드가 선 뒤 첫 땅에 몸을 두었다
        uint8             _bFlushScheduled : 1;
        uint8             _reserved        : 5;
    };
} // namespace sw
