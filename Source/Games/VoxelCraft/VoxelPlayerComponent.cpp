#include "pch.h"

#include "Games/VoxelCraft/VoxelPlayerComponent.h"

#include "Core/Common/FourCcUtil.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Automation/AutomationProbe.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Serialization/Format/Archive.h"

#include "GameFramework/Base/Actor/Control/Controller/PlayerControllerComponent.h"
#include "GameFramework/Base/Actor/Control/FirstPersonCameraComponent.h"
#include "GameFramework/Base/Actor/Control/Intent/ControlIntent.h"
#include "GameFramework/Base/Actor/Control/Pawn/PawnComponent.h"
#include "GameFramework/Base/Foundation/Framework/GameService.h"
#include "GameFramework/Base/Foundation/Framework/Presentation/GameSound.h"
#include "GameFramework/Base/Foundation/Utility/StateArchiveUtil.h"
#include "GameFramework/Kits/Feature/World/Voxel/Catalog/VoxelBlock.h"

#include "Games/VoxelCraft/VoxelAutoPlayControllerComponent.h"
#include "Games/VoxelCraft/VoxelDirectorComponent.h"

namespace sw
{
    SW_LOG_CALLER( "VoxelPlayer" );

    namespace
    {
        struct VoxelPlayerComponentInternal
        {
            static constexpr const utf8* kSoundLand    = "game/voxelcraft/sounds/footstep_grass_000.ogg";
            static constexpr uint32      kStateTag     = FourCcUtil::make( "PLYR" );
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

            /** @brief 씬의 첫 복셀 플레이어입니다(탐침 — 프레임 경로가 아니다). */
            static const VoxelPlayerComponent* findFirstPlayer( const GameObjectManager* pManager )
            {
                const VoxelPlayerComponent* pFound = nullptr;
                if ( pManager != nullptr )
                {
                    pManager->forEachComponentOfType<VoxelPlayerComponent>( [&pFound]( VoxelPlayerComponent* pPlayer )
                    {
                        if ( pFound == nullptr )
                            pFound = pPlayer;
                    } );
                }
                return pFound;
            }

            [[nodiscard]] static bool readWalkedDistance( const GameObjectManager* pManager, float64& outValue )
            {
                const VoxelPlayerComponent* pPlayer = findFirstPlayer( pManager );
                if ( pPlayer == nullptr )
                    return false;
                const float3 delta = pPlayer->getBody().getPosition() - pPlayer->getStartPosition();
                outValue           = static_cast<float64>( MathUtil::sqrt( delta._x * delta._x + delta._z * delta._z ) );
                return true;
            }

            [[nodiscard]] static bool readHotbarSlot( const GameObjectManager* pManager, float64& outValue )
            {
                const VoxelPlayerComponent* pPlayer = findFirstPlayer( pManager );
                if ( pPlayer == nullptr )
                    return false;
                outValue = pPlayer->getHotbar().getSelectedIndex();
                return true;
            }

            [[nodiscard]] static bool readBrokenCount( const GameObjectManager* pManager, float64& outValue )
            {
                const VoxelPlayerComponent* pPlayer = findFirstPlayer( pManager );
                if ( pPlayer == nullptr )
                    return false;
                outValue = pPlayer->getBrokenCount();
                return true;
            }

            [[nodiscard]] static bool readControlYaw( const GameObjectManager* pManager, float64& outValue )
            {
                const VoxelPlayerComponent* pPlayer = findFirstPlayer( pManager );
                const GameObject*           pOwner  = pPlayer != nullptr ? pPlayer->getOwner() : nullptr;
                const PawnComponent*        pPawn   = pOwner != nullptr ? pOwner->getComponent<PawnComponent>() : nullptr;
                if ( pPawn == nullptr )
                    return false;
                outValue = static_cast<float64>( pPawn->getIntent()._controlYaw );
                return true;
            }

            /** @brief 플레이어 폰을 누가 쥐었나 — 0 플레이어 조종자, 1 자동 플레이 AI, −1 아무도. */
            [[nodiscard]] static bool readControllerKind( const GameObjectManager* pManager, float64& outValue )
            {
                const VoxelPlayerComponent* pPlayer       = findFirstPlayer( pManager );
                GameObject*                 pOwner        = pPlayer != nullptr ? pPlayer->getOwner() : nullptr;
                GameObjectManager*          pOwnerManager = pOwner != nullptr ? pOwner->getManager() : nullptr;
                const PawnComponent*        pPawn         = pOwner != nullptr ? pOwner->getComponent<PawnComponent>() : nullptr;
                if ( pPawn == nullptr || pOwnerManager == nullptr )
                    return false;
                Component* pController = pPawn->isPossessed() ? pOwnerManager->resolveComponent( pPawn->getController() ) : nullptr;
                if ( pController == nullptr )
                    outValue = -1.0;
                else if ( castTo<VoxelAutoPlayControllerComponent>( pController ) != nullptr )
                    outValue = 1.0;
                else if ( castTo<PlayerControllerComponent>( pController ) != nullptr )
                    outValue = 0.0;
                else
                    outValue = 2.0;
                return true;
            }
        };
    } // namespace

    SW_AUTOMATION_PROBE( voxelWalkedDistance, "VoxelCraft.WalkedDistance", "Horizontal distance of the player body from where it was first placed", &VoxelPlayerComponentInternal::readWalkedDistance );
    SW_AUTOMATION_PROBE( voxelHotbarSlot, "VoxelCraft.HotbarSlot", "Selected hotbar slot (0-based)", &VoxelPlayerComponentInternal::readHotbarSlot );
    SW_AUTOMATION_PROBE( voxelBrokenCount, "VoxelCraft.BrokenCount", "Blocks the player has broken", &VoxelPlayerComponentInternal::readBrokenCount );
    SW_AUTOMATION_PROBE( voxelControlYaw, "VoxelCraft.ControlYaw", "Control yaw of the player pawn (radians)", &VoxelPlayerComponentInternal::readControlYaw );
    SW_AUTOMATION_PROBE( voxelControllerKind, "VoxelCraft.ControllerKind", "Who possesses the player pawn: 0 player controller, 1 auto play AI, -1 nobody",
                         &VoxelPlayerComponentInternal::readControllerKind );
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
        , _intentSlots{}
        , _startPosition{ 0.0f, 0.0f, 0.0f }
        , _breakProgress{ 0.0f }
        , _placeCooldown{}
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
            for ( const utf8* pID : arrStartBlock )
            {
                (void)_hotbar.addBlock( pCatalog->findBlockIndex( hashed_string( pID ) ), 64 );
            }
        }
        GameObject*                   pOwner    = getOwner();
        GameObjectManager*            pManager  = pOwner != nullptr ? pOwner->getManager() : nullptr;
        FirstPersonCameraComponent*   pCamera   = pOwner != nullptr ? pOwner->getComponent<FirstPersonCameraComponent>() : nullptr;
        const VoxelDirectorComponent* pDirector = pManager != nullptr ? GameDirectorComponent::resolve<VoxelDirectorComponent>( *pManager, _director ) : nullptr;
        // 자동 플레이는 플레이어 조종자가 폰을 쥐지 않는다(마우스를 잠그지 않는다) — 시점은 AI 가 정한다(`setAngles` → 조종 회전 요청).
        PawnComponent* pPawn = pOwner != nullptr ? pOwner->getComponent<PawnComponent>() : nullptr;
        if ( pPawn != nullptr )
        {
            resolveIntentSlots( *pPawn );
            // 자동 플레이면 플레이어 조종자가 먼저 쥐지 않게 한다(잠금이 한 프레임 걸렸다 풀리지 않게) — 디렉터가 AI 를 빙의시킨다.
            if ( pDirector != nullptr && pDirector->isAutoPlayOn() )
                pPawn->setAutoPossess( PawnAutoPossess::None );
        }
        if ( pCamera != nullptr )
            pCamera->setAngles( _startYaw, _startPitch );
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
        const PawnComponent*          pPawn     = pOwner != nullptr ? pOwner->getComponent<PawnComponent>() : nullptr;
        const VoxelDirectorComponent* pDirector = pManager != nullptr ? GameDirectorComponent::resolve<VoxelDirectorComponent>( *pManager, _director ) : nullptr;
        const VoxelBlockCatalog*      pCatalog  = game::getService<VoxelBlockCatalog>();
        if ( pCamera == nullptr || pPawn == nullptr || pDirector == nullptr || pCatalog == nullptr || pDirector->isStarted() == false || deltaTime <= 0.0f )
            return;
        if ( _bBodyPlaced == SW_FALSE )
            initializeBody( *pDirector );
        const float32 step = MathUtil::min( deltaTime, 0.1f );

        float3 wish{ 0.0f, 0.0f, 0.0f };
        bool   bJump   = false;
        bool   bSprint = false;
        bool   bBreak  = false;
        bool   bPlace  = false;
        readIntent( *pPawn, *pCatalog, wish, bJump, bSprint, bBreak, bPlace );

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
        // 누르고 있으면 간격마다 하나 — 지나친 몫을 이어 놓기 빈도가 걸음 크기에 매이지 않는다(`Countdown::tickRepeat`).
        if ( _placeCooldown.tickRepeat( step, _placeInterval, bPlace ) )
            placeBlock( *pDirector, *pCatalog );
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
        _startPosition                      = _body.getPosition();
        _bBodyPlaced                        = SW_TRUE;
        GameObject*                 pOwner  = getOwner();
        FirstPersonCameraComponent* pCamera = pOwner != nullptr ? pOwner->getComponent<FirstPersonCameraComponent>() : nullptr;
        if ( pCamera != nullptr )
            pCamera->setEyePosition( _body.getEyePosition() );
    }

    void VoxelPlayerComponent::resolveIntentSlots( const PawnComponent& pawn )
    {
        constexpr const utf8* kArrSlotName[VoxelHotbar::kSlotCount] = { "Voxel.Slot1", "Voxel.Slot2", "Voxel.Slot3", "Voxel.Slot4", "Voxel.Slot5",
                                                                        "Voxel.Slot6", "Voxel.Slot7", "Voxel.Slot8", "Voxel.Slot9" };
        _intentSlots._jump                                          = pawn.findButton( hashed_string( "Voxel.Jump" ) );
        _intentSlots._sprint                                        = pawn.findButton( hashed_string( "Voxel.Sprint" ) );
        _intentSlots._break                                         = pawn.findButton( hashed_string( "Voxel.Break" ) );
        _intentSlots._place                                         = pawn.findButton( hashed_string( "Voxel.Place" ) );
        _intentSlots._hotbarScroll                                  = pawn.findAnalog( hashed_string( "Voxel.HotbarScroll" ) );
        for ( int32 slotIndex = 0; slotIndex < VoxelHotbar::kSlotCount; ++slotIndex )
        {
            _intentSlots._arrSlot[slotIndex] = pawn.findButton( hashed_string( kArrSlotName[slotIndex] ) );
        }
    }

    void VoxelPlayerComponent::readIntent( const PawnComponent& pawn, const VoxelBlockCatalog& catalog, float3& outWish, bool& outJump, bool& outSprint,
                                           bool& outBreak, bool& outPlace )
    {
        // 이동 축은 조종 요 기준이다 — 1인칭 카메라가 앞 그룹에서 같은 조종 회전으로 시점을 두었다. 위아래 칸은 쓰지 않는다(물에서는 점프가 헤엄).
        const ControlIntent& intent = pawn.getIntent();
        const float3         move   = intent.computeWorldMove();
        outWish                     = float3{ move._x, 0.0f, move._z };
        outJump                     = pawn.isButtonDown( _intentSlots._jump );
        outSprint                   = pawn.isButtonDown( _intentSlots._sprint );
        outBreak                    = pawn.isButtonDown( _intentSlots._break );
        outPlace                    = pawn.isButtonDown( _intentSlots._place ); // 처음 누름도 누름이다 — 간격은 `_placeCooldown` 이 거른다

        const int32 previousSlot = _hotbar.getSelectedIndex();
        for ( int32 slotIndex = 0; slotIndex < VoxelHotbar::kSlotCount; ++slotIndex )
        {
            if ( pawn.wasButtonTriggered( _intentSlots._arrSlot[slotIndex] ) )
                _hotbar.select( slotIndex );
        }
        const float32 scroll = _intentSlots._hotbarScroll >= 0 ? intent._arrAnalog[_intentSlots._hotbarScroll] : 0.0f;
        if ( scroll != 0.0f )
            _hotbar.selectRelative( scroll > 0.0f ? -1 : 1 );
        if ( previousSlot != _hotbar.getSelectedIndex() )
        {
            [[maybe_unused]] const VoxelBlockDef* pBlock = catalog.findBlock( _hotbar.getSelectedSlot()._block );
            SW_LOG_INFO( "[Voxel] slot %#: %# x%#", _hotbar.getSelectedIndex() + 1, pBlock != nullptr ? pBlock->_name.c_str() : "empty",
                         _hotbar.getSelectedSlot()._count );
        }
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
