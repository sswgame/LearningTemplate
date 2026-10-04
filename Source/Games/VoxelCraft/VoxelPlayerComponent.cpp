#include "pch.h"

#include "Games/VoxelCraft/VoxelPlayerComponent.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Input/InputManager.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Serialization/Format/Archive.h"

#include "GameFramework/Components/FirstPersonCameraComponent.h"
#include "GameFramework/Framework/GameService.h"
#include "GameFramework/Framework/GameSound.h"
#include "GameFramework/Kits/Simulation/Voxel/VoxelBlock.h"
#include "GameFramework/Utility/StateArchiveUtil.h"

#include "Games/VoxelCraft/VoxelDirectorComponent.h"

namespace sw
{
    SW_LOG_CALLER( "VoxelPlayer" );

    namespace
    {
        struct VoxelPlayerComponentInternal
        {
            static constexpr const utf8* kSoundLand    = "game/voxelcraft/sounds/footstep_grass_000.ogg";
            static constexpr uint32      kStateTag     = 0x52594C50u; ///< 'PLYR'
            static constexpr uint32      kStateVersion = 1;
            static constexpr const utf8* kSoundBreak   = "game/voxelcraft/sounds/impact_soft_medium_000.ogg";
            static constexpr const utf8* kSoundPlace   = "game/voxelcraft/sounds/impact_plank_medium_001.ogg";

            /** @brief 부순 블록에서 얻는 블록입니다(풀 → 흙, 돌 → 조약돌, 기반암 · 물은 없음). */
            static VoxelBlockIndex findDrop( const VoxelBlockCatalog& catalog, VoxelBlockIndex block )
            {
                const VoxelBlockDef* pBlock = catalog.findBlock( block );
                if ( pBlock == nullptr || pBlock->_bBreakable == SW_FALSE )
                    return kVoxelAirBlock;
                if ( pBlock->_id == hashed_string( "grass" ) )
                    return catalog.findBlockIndex( hashed_string( "dirt" ) );
                if ( pBlock->_id == hashed_string( "stone" ) )
                    return catalog.findBlockIndex( hashed_string( "cobblestone" ) );
                return block;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    VoxelPlayerComponent::VoxelPlayerComponent()
        : _director{}
        , _reachDistance{ 6.0f }
        , _placeInterval{ 0.25f }
        , _startYaw{ 0.6f }
        , _startPitch{ -0.2f }
        , _body{}
        , _hotbar{}
        , _target{}
        , _listPendingEdit{}
        , _soundQueue{}
        , _pendingStateBytes{}
        , _breakProgress{ 0.0f }
        , _placeCooldown{ 0.0f }
        , _autoTimer{ 0.0f }
        , _brokenCount{ 0 }
        , _placedCount{ 0 }
        , _bHasTarget{ SW_FALSE }
        , _bBodyPlaced{ SW_FALSE }
        , _bFlushScheduled{ SW_FALSE }
        , _reserved{ 0 }
    {
        setCanEverTick( true );
    }

    VoxelPlayerComponent::~VoxelPlayerComponent() = default;

    void VoxelPlayerComponent::onBeginPlay()
    {
        Component::onBeginPlay();
        _bBodyPlaced = SW_FALSE;
        _hotbar      = VoxelHotbar{};
        // 처음 핫바 — 짓는 블록 몇 가지.
        const VoxelBlockCatalog* pCatalog = game::getService<VoxelBlockCatalog>();
        if ( pCatalog != nullptr )
        {
            _body.setWaterBlock( pCatalog->findBlockIndex( hashed_string( "water" ) ) );
            const utf8* arrStartBlock[] = { "planks", "cobblestone", "brick", "log", "sand", "leaves" };
            for ( const utf8* pId : arrStartBlock )
                (void)_hotbar.addBlock( pCatalog->findBlockIndex( hashed_string( pId ) ), 64 );
        }
        GameObject*                   pOwner    = getOwner();
        GameObjectManager*            pManager  = pOwner != nullptr ? pOwner->getManager() : nullptr;
        FirstPersonCameraComponent*   pCamera   = pOwner != nullptr ? pOwner->getComponent<FirstPersonCameraComponent>() : nullptr;
        const VoxelDirectorComponent* pDirector = pManager != nullptr ? GameDirectorComponent::resolve<VoxelDirectorComponent>( *pManager, _director ) : nullptr;
        if ( pCamera != nullptr )
        {
            // 자동 플레이는 마우스를 잠그지 않는다 — 시점은 AI 가 정한다.
            if ( pDirector != nullptr && pDirector->isAutoPlayOn() )
                pCamera->setMouseLookEnabled( false );
            pCamera->setAngles( _startYaw, _startPitch );
        }
        if ( pDirector != nullptr && pDirector->isStarted() )
            initializeBody( *pDirector );
        if ( _pendingStateBytes.empty() == false )
            applyPendingState();
    }

    void VoxelPlayerComponent::writeState( Archive& outArchive ) const
    {
        StateArchiveUtil::writeHeader( outArchive, VoxelPlayerComponentInternal::kStateTag, VoxelPlayerComponentInternal::kStateVersion );
        outArchive << _body.getPosition();
        _hotbar.writeState( outArchive );
        outArchive << _brokenCount;
        outArchive << _placedCount;
        outArchive << static_cast<uint8>( _bBodyPlaced );
    }

    void VoxelPlayerComponent::restoreState( vector<uint8>&& bytes )
    {
        _pendingStateBytes = std::move( bytes );
        if ( hasBegunPlay() )
            applyPendingState();
    }

    void VoxelPlayerComponent::applyPendingState()
    {
        Archive     archive( _pendingStateBytes.data(), _pendingStateBytes.size() );
        float3      position{};
        VoxelHotbar hotbar;
        uint32      brokenCount = 0;
        uint32      placedCount = 0;
        uint8       bBodyPlaced = SW_FALSE;
        bool        bRead       = StateArchiveUtil::readHeader( archive, VoxelPlayerComponentInternal::kStateTag, VoxelPlayerComponentInternal::kStateVersion );
        archive >> position;
        bRead = bRead && hotbar.readState( archive );
        archive >> brokenCount;
        archive >> placedCount;
        archive >> bBodyPlaced;
        _pendingStateBytes.clear();
        if ( bRead == false || archive.isError() || archive.getRemainingBytes() != 0 )
        {
            SW_LOG_WARNING( "[Voxel] the saved player state does not match this build - starting at the spawn" );
            return;
        }
        _hotbar      = hotbar;
        _brokenCount = brokenCount;
        _placedCount = placedCount;
        if ( bBodyPlaced != SW_FALSE )
        {
            // 몸을 놓기 전에 찍은 것이면 처음 자리(다음 틱)로 둔다.
            _body.setPosition( position );
            _bBodyPlaced = SW_TRUE;
        }
    }

    void VoxelPlayerComponent::onTick( float32 deltaTime )
    {
        Component::onTick( deltaTime );
        GameObject*                   pOwner    = getOwner();
        GameObjectManager*            pManager  = pOwner != nullptr ? pOwner->getManager() : nullptr;
        FirstPersonCameraComponent*   pCamera   = pOwner != nullptr ? pOwner->getComponent<FirstPersonCameraComponent>() : nullptr;
        const VoxelDirectorComponent* pDirector = pManager != nullptr ? GameDirectorComponent::resolve<VoxelDirectorComponent>( *pManager, _director ) : nullptr;
        const VoxelBlockCatalog*      pCatalog  = game::getService<VoxelBlockCatalog>();
        if ( pCamera == nullptr || pDirector == nullptr || pCatalog == nullptr || pDirector->isStarted() == false || deltaTime <= 0.0f )
            return;
        if ( _bBodyPlaced == SW_FALSE )
            initializeBody( *pDirector );
        const float32 step = MathUtil::min( deltaTime, 0.1f );

        float3              wish{ 0.0f, 0.0f, 0.0f };
        bool                bJump   = false;
        bool                bSprint = false;
        bool                bBreak  = false;
        bool                bPlace  = false;
        const InputManager* pInput  = game::getService<InputManager>();
        if ( pDirector->isAutoPlayOn() || pInput == nullptr )
            tickAutoPlay( step, *pDirector, *pCamera, wish, bJump, bBreak, bPlace );
        else
            tickInput( *pInput, *pCatalog, *pCamera, wish, bJump, bSprint, bBreak, bPlace );

        const bool bWasOnGround = _body.isOnGround();
        _body.step( pDirector->getWorld(), wish, bJump, bSprint, step );
        if ( bWasOnGround == false && _body.isOnGround() )
            _soundQueue.queueClip( VoxelPlayerComponentInternal::kSoundLand );
        // 월드 밖으로 떨어지면 처음 자리로.
        if ( _body.getPosition()._y < -10.0f )
            _body.setPosition( pDirector->findSpawnPosition() );
        pCamera->setEyePosition( _body.getEyePosition() );

        updateTarget( *pDirector, *pCamera );
        updateBreaking( step, *pCatalog, bBreak );
        _placeCooldown -= step;
        if ( bPlace && _placeCooldown <= 0.0f )
        {
            placeBlock( *pDirector, *pCatalog );
            _placeCooldown = _placeInterval;
        }
        if ( _listPendingEdit.empty() == false || _soundQueue.isEmpty() == false )
            scheduleFlush();
    }

    bool VoxelPlayerComponent::findTarget( VoxelCoord& outBlock ) const
    {
        if ( _bHasTarget == SW_FALSE )
            return false;
        outBlock = _target._block;
        return true;
    }

    // ------------------------------------------------------------------------------
    // 몸 · 손(DuringPhysics — 자기 오브젝트, 월드는 읽기만)
    // ------------------------------------------------------------------------------
    void VoxelPlayerComponent::initializeBody( const VoxelDirectorComponent& director )
    {
        _body.setPosition( director.findSpawnPosition() );
        _bBodyPlaced                        = SW_TRUE;
        GameObject*                 pOwner  = getOwner();
        FirstPersonCameraComponent* pCamera = pOwner != nullptr ? pOwner->getComponent<FirstPersonCameraComponent>() : nullptr;
        if ( pCamera != nullptr )
            pCamera->setEyePosition( _body.getEyePosition() );
    }

    void VoxelPlayerComponent::tickInput( const InputManager& input, const VoxelBlockCatalog& catalog, const FirstPersonCameraComponent& camera, float3& outWish,
                                          bool& outJump, bool& outSprint, bool& outBreak, bool& outPlace )
    {
        // 시점은 같은 오브젝트의 1인칭 카메라가 앞 그룹에서 마우스로 돌렸다(잠금 · Esc 도 거기서).
        const FirstPersonLook& look    = camera.getLook();
        const float3           forward = look.getFlatForward();
        const float3           right   = look.getFlatRight();
        if ( input.isKeyDown( Key::W ) )
            outWish = outWish + forward;
        if ( input.isKeyDown( Key::S ) )
            outWish = outWish - forward;
        if ( input.isKeyDown( Key::D ) )
            outWish = outWish + right;
        if ( input.isKeyDown( Key::A ) )
            outWish = outWish - right;
        outJump   = input.isKeyDown( Key::Space );
        outSprint = input.isKeyDown( Key::LeftShift );
        outBreak  = input.isMouseButtonDown( MouseButton::Left );
        outPlace  = input.wasMouseButtonPressed( MouseButton::Right ) || ( input.isMouseButtonDown( MouseButton::Right ) && _placeCooldown <= 0.0f );

        constexpr Key kArrSlotKey[VoxelHotbar::kSlotCount] = { Key::Digit1, Key::Digit2, Key::Digit3, Key::Digit4, Key::Digit5,
                                                               Key::Digit6, Key::Digit7, Key::Digit8, Key::Digit9 };
        const int32   previousSlot                         = _hotbar.getSelectedIndex();
        for ( int32 slotIndex = 0; slotIndex < VoxelHotbar::kSlotCount; ++slotIndex )
        {
            if ( input.wasKeyPressed( kArrSlotKey[slotIndex] ) )
                _hotbar.select( slotIndex );
        }
        const float32 wheel = input.getMouseWheel();
        if ( wheel != 0.0f )
            _hotbar.selectRelative( wheel > 0.0f ? -1 : 1 );
        if ( previousSlot != _hotbar.getSelectedIndex() )
        {
            [[maybe_unused]] const VoxelBlockDef* pBlock = catalog.findBlock( _hotbar.getSelectedSlot()._block );
            SW_LOG_INFO( "[Voxel] slot %#: %# x%#", _hotbar.getSelectedIndex() + 1, pBlock != nullptr ? pBlock->_name.c_str() : "empty",
                         _hotbar.getSelectedSlot()._count );
        }
    }

    void VoxelPlayerComponent::tickAutoPlay( float32 deltaTime, const VoxelDirectorComponent& director, FirstPersonCameraComponent& camera, float3& outWish,
                                             bool& outJump, bool& outBreak, bool& outPlace )
    {
        // 앞으로 걷다가 막히면 뛰고, 아래를 조금 보며 몇 초마다 앞 블록을 부수고 놓는다. 가끔 방향을 튼다.
        _autoTimer += deltaTime;
        const float32 cycle = MathUtil::fmod( _autoTimer, 12.0f );
        camera.setAngles( camera.getLook().getYaw() + deltaTime * ( cycle < 6.0f ? 0.25f : -0.15f ), -0.35f );
        outWish = camera.getLook().getFlatForward();

        const VoxelWorld& world = director.getWorld();
        const float3      ahead = _body.getPosition() + outWish * 0.6f;
        outJump                 = world.isSolid( static_cast<int32>( MathUtil::floor( ahead._x ) ), static_cast<int32>( MathUtil::floor( _body.getPosition()._y + 0.5f ) ),
                                                 static_cast<int32>( MathUtil::floor( ahead._z ) ) ) ||
                  _body.isInWater();
        outBreak = cycle > 3.0f && cycle < 6.5f;
        outPlace = cycle > 9.0f && cycle < 9.0f + deltaTime * 1.5f;
    }

    void VoxelPlayerComponent::updateTarget( const VoxelDirectorComponent& director, const FirstPersonCameraComponent& camera )
    {
        VoxelRayHit hit;
        const bool  bHit = VoxelRaycast::raycast( director.getWorld(), _body.getEyePosition(), camera.getLook().getForward(), _reachDistance, hit );
        if ( bHit == false || hit._block != _target._block || _bHasTarget == SW_FALSE )
            _breakProgress = 0.0f; // 다른 블록을 보면 처음부터
        _bHasTarget = bHit ? SW_TRUE : SW_FALSE;
        if ( bHit )
            _target = hit;
    }

    void VoxelPlayerComponent::updateBreaking( float32 deltaTime, const VoxelBlockCatalog& catalog, bool bBreakHeld )
    {
        if ( bBreakHeld == false || _bHasTarget == SW_FALSE )
        {
            _breakProgress = 0.0f;
            return;
        }
        const VoxelBlockDef* pBlock = catalog.findBlock( _target._blockIndex );
        if ( pBlock == nullptr || pBlock->_bBreakable == SW_FALSE )
            return;
        _breakProgress += deltaTime / MathUtil::max( 0.05f, pBlock->_hardness );
        if ( _breakProgress < 1.0f )
            return;

        // 월드는 틱 뒤에 바뀐다(디렉터) — 여기서는 손에 든 것과 셈만 바로 바꾼다.
        const VoxelBlockIndex drop = VoxelPlayerComponentInternal::findDrop( catalog, _target._blockIndex );
        _listPendingEdit.push_back( BlockEdit{ _target._block, kVoxelAirBlock } );
        _soundQueue.queueClip( VoxelPlayerComponentInternal::kSoundBreak );
        ++_brokenCount;
        _breakProgress = 0.0f;
        if ( drop != kVoxelAirBlock && _hotbar.addBlock( drop, 1 ) > 0 )
            SW_LOG_INFO( "[Voxel] hotbar is full - the %# is lost", pBlock->_name.c_str() );
        _bHasTarget = SW_FALSE;
    }

    void VoxelPlayerComponent::placeBlock( const VoxelDirectorComponent& director, const VoxelBlockCatalog& catalog )
    {
        if ( _bHasTarget == SW_FALSE )
            return;
        const VoxelWorld& world = director.getWorld();
        const VoxelCoord  cell  = _target._previous;
        if ( _body.overlapsBlock( cell ) || world.isInside( cell._x, cell._y, cell._z ) == false )
            return; // 몸 안 · 월드 밖에는 놓지 않는다
        const VoxelBlockIndex existing = world.getBlock( cell );
        if ( existing != kVoxelAirBlock && catalog.isSolid( existing ) )
            return;
        VoxelBlockIndex block = kVoxelAirBlock;
        if ( _hotbar.consumeSelected( block ) == false )
            return;
        _listPendingEdit.push_back( BlockEdit{ cell, block } );
        _soundQueue.queueClip( VoxelPlayerComponentInternal::kSoundPlace );
        ++_placedCount;
    }

    // ------------------------------------------------------------------------------
    // 틱 뒤(게임 스레드)
    // ------------------------------------------------------------------------------
    void VoxelPlayerComponent::scheduleFlush()
    {
        GameObject*        pOwner   = getOwner();
        GameObjectManager* pManager = pOwner != nullptr ? pOwner->getManager() : nullptr;
        if ( pManager == nullptr || _bFlushScheduled == SW_TRUE )
            return;
        _bFlushScheduled           = SW_TRUE;
        const ComponentHandle self = getHandle();
        pManager->executeOrDeferPostTick( [pManager, self]()
        {
            VoxelPlayerComponent* pPlayer = static_cast<VoxelPlayerComponent*>( pManager->resolveComponent( self ) );
            if ( pPlayer != nullptr )
                pPlayer->flushPending();
        } );
    }

    void VoxelPlayerComponent::flushPending()
    {
        _bFlushScheduled                  = SW_FALSE;
        GameObject*             pOwner    = getOwner();
        GameObjectManager*      pManager  = pOwner != nullptr ? pOwner->getManager() : nullptr;
        const GameObject*       pObject   = pManager != nullptr ? pManager->resolveGameObject( _director ) : nullptr;
        VoxelDirectorComponent* pDirector = pObject != nullptr ? pObject->getComponent<VoxelDirectorComponent>() : nullptr;
        for ( const BlockEdit& edit : _listPendingEdit )
        {
            if ( pDirector != nullptr )
                (void)pDirector->applyBlockEdit( edit._coord, edit._block ); // 월드 밖은 이미 걸렀다
        }
        _listPendingEdit.clear();
        _soundQueue.playAll();
    }
} // namespace sw
