#include "pch.h"

#include "Games/VoxelCraft/VoxelPlayerComponent.h"

#include "Core/Common/FourCcUtil.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Automation/Watch/AutomationWatch.h"
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
            static constexpr uint32 kStateTag     = FourCcUtil::make( "PLYR" );
            static constexpr uint32 kStateVersion = 1;

            // 사운드 이벤트 이름입니다. 클립, 범위, 동시 재생 상한은 voxelcraft.audioevents.xml 이 정합니다.
            static constexpr const utf8* kEventLand  = "Land";
            static constexpr const utf8* kEventBreak = "BlockBreak";
            static constexpr const utf8* kEventPlace = "BlockPlace";

            /** @brief 폰에서 버튼 @p name 의 칸을 찾습니다. 없으면 오류를 적고 @p outResolved 를 false 로 둡니다(true 로 되돌리지는 않는다). */
            static int32 resolveButton( const PawnComponent& pawn, const hashed_string& name, const utf8* pRole, bool& outResolved )
            {
                const int32 slot = pawn.findButton( name );
                if ( slot < 0 )
                {
                    SW_LOG_ERROR( "[Voxel] %# button '%#' is not in the pawn's button list", pRole, name.c_str() );
                    outResolved = false;
                }
                return slot;
            }

            /** @brief 카탈로그에서 블록 @p blockID 의 번호를 찾습니다. 없으면 오류를 적고 @p outKnown 을 false 로 둡니다(true 로 되돌리지는 않는다). */
            static VoxelBlockIndex findBlockOrReport( const VoxelBlockCatalog& catalog, const hashed_string& blockID, bool& outKnown )
            {
                const VoxelBlockIndex index = catalog.findBlockIndex( blockID );
                if ( index == kVoxelAirBlock )
                {
                    SW_LOG_ERROR( "[Voxel] block '%#' is not in the block catalog", blockID.c_str() );
                    outKnown = false;
                }
                return index;
            }

            /** @brief 블록을 부수면 얻는 블록입니다. 블록 데이터의 `drop` 값이고(풀은 흙, 돌은 조약돌), 없으면 자기 자신입니다. 부수지 못하는 블록이면 공기 블록을 반환합니다. */
            static VoxelBlockIndex findDrop( const VoxelBlockCatalog& catalog, VoxelBlockIndex block )
            {
                const VoxelBlockDef* pBlock = catalog.findBlock( block );
                if ( pBlock == nullptr || pBlock->_bBreakable == SW_FALSE )
                    return kVoxelAirBlock;
                return pBlock->_dropID.empty() ? block : catalog.findBlockIndex( pBlock->_dropID );
            }

            /** @brief 씬의 첫 복셀 플레이어입니다. 탐침에서만 쓰고, 프레임 경로에서는 쓰지 않습니다. */
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

            /** @brief 플레이어 폰을 누가 조종하는지 반환합니다. 0은 플레이어 조종자, 1은 자동 플레이 AI, −1은 아무도 조종하지 않는 경우입니다. */
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

    SW_AUTOMATION_WATCH( voxelWalkedDistance, "VoxelCraft.WalkedDistance", "Horizontal distance of the player body from where it was first placed", &VoxelPlayerComponentInternal::readWalkedDistance );
    SW_AUTOMATION_WATCH( voxelHotbarSlot, "VoxelCraft.HotbarSlot", "Selected hotbar slot (0-based)", &VoxelPlayerComponentInternal::readHotbarSlot );
    SW_AUTOMATION_WATCH( voxelBrokenCount, "VoxelCraft.BrokenCount", "Blocks the player has broken", &VoxelPlayerComponentInternal::readBrokenCount );
    SW_AUTOMATION_WATCH( voxelControlYaw, "VoxelCraft.ControlYaw", "Control yaw of the player pawn (radians)", &VoxelPlayerComponentInternal::readControlYaw );
    SW_AUTOMATION_WATCH( voxelControllerKind, "VoxelCraft.ControllerKind", "Who possesses the player pawn: 0 player controller, 1 auto play AI, -1 nobody",
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
        , _jumpButton{}
        , _sprintButton{}
        , _breakButton{}
        , _placeButton{}
        , _hotbarScrollAnalog{}
        , _listSlotButton{}
        , _waterBlock{}
        , _listStartBlock{}
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
        // 물 블록과 처음 핫바는 씬 데이터에 적힌 블록 이름입니다. 카탈로그나 이름이 틀리면 플레이어를 만들지 않고, 이유는 오류 로그에 남깁니다.
        const VoxelBlockCatalog* pCatalog = game::findSubsystem<VoxelBlockCatalog>();
        if ( pCatalog == nullptr )
        {
            SW_LOG_ERROR( "[Voxel] no VoxelBlockCatalog game service - the player cannot start" );
            setCanEverTick( false );
            return;
        }
        if ( applyBlockNames( *pCatalog ) == false )
        {
            setCanEverTick( false );
            return;
        }
        GameObject*                   pOwner    = getOwner();
        GameObjectManager*            pManager  = pOwner != nullptr ? pOwner->getManager() : nullptr;
        FirstPersonCameraComponent*   pCamera   = pOwner != nullptr ? pOwner->getComponent<FirstPersonCameraComponent>() : nullptr;
        const VoxelDirectorComponent* pDirector = pManager != nullptr ? GameDirectorComponent::resolve<VoxelDirectorComponent>( *pManager, _director ) : nullptr;
        // 자동 플레이에서는 플레이어 조종자가 폰을 조종하지 않으므로 마우스도 잠그지 않습니다. 시점은 AI 가 정하며 `setAngles` 가 조종 회전을 요청합니다.
        PawnComponent* pPawn = pOwner != nullptr ? pOwner->getComponent<PawnComponent>() : nullptr;
        if ( pPawn != nullptr )
        {
            if ( resolveIntentSlots( *pPawn ) == false )
            {
                setCanEverTick( false );
                return;
            }
            // 자동 플레이면 플레이어 조종자가 먼저 폰을 가져가지 못하게 합니다. 가져가면 마우스 잠금이 한 프레임 걸렸다 풀립니다. 디렉터가 AI 를 빙의시킵니다.
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
        const bool bStateMismatch = bRead == false || archive.isError() || archive.getRemainingBytes() != 0;
        if ( bStateMismatch )
        {
            SW_LOG_WARNING( "[Voxel] the saved player state does not match this build - starting at the spawn" );
            return;
        }
        _hotbar      = hotbar;
        _brokenCount = brokenCount;
        _placedCount = placedCount;
        if ( bBodyPlaced != SW_FALSE )
        {
            // 몸을 놓기 전에 기록한 상태라면 처음 위치(다음 틱)에서 시작합니다.
            _body.setPosition( position );
            _bBodyPlaced = SW_TRUE;
        }
    }

    void VoxelPlayerComponent::onTick( float32 deltaTime )
    {
        Component::onTick( deltaTime );
        GameObject*                   pOwner          = getOwner();
        GameObjectManager*            pManager        = pOwner != nullptr ? pOwner->getManager() : nullptr;
        FirstPersonCameraComponent*   pCamera         = pOwner != nullptr ? pOwner->getComponent<FirstPersonCameraComponent>() : nullptr;
        const PawnComponent*          pPawn           = pOwner != nullptr ? pOwner->getComponent<PawnComponent>() : nullptr;
        const VoxelDirectorComponent* pDirector       = pManager != nullptr ? GameDirectorComponent::resolve<VoxelDirectorComponent>( *pManager, _director ) : nullptr;
        const VoxelBlockCatalog*      pCatalog        = game::findSubsystem<VoxelBlockCatalog>();
        const bool                    bNotReadyToTick = pCamera == nullptr || pPawn == nullptr || pDirector == nullptr || pCatalog == nullptr || pDirector->isStarted() == false || deltaTime <= 0.0f;
        if ( bNotReadyToTick )
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
            _soundQueue.queueEvent( VoxelPlayerComponentInternal::kEventLand );
        // 월드 밖으로 떨어지면 처음 위치로 되돌립니다.
        if ( _body.getPosition()._y < -10.0f )
            _body.setPosition( pDirector->findSpawnPosition() );
        pCamera->setEyePosition( _body.getEyePosition() );

        updateTarget( *pDirector, *pCamera );
        updateBreaking( step, *pCatalog, bBreak );
        // 버튼을 누르고 있으면 간격마다 하나씩 놓습니다. 지나간 시간을 이어서 세므로 놓기 빈도가 프레임 시간에 좌우되지 않습니다(`Countdown::updateRepeat`).
        if ( _placeCooldown.updateRepeat( step, _placeInterval, bPlace ) )
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
    // 몸과 손(DuringPhysics): 자기 오브젝트를 바꾸고, 월드는 읽기만 합니다.
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

    bool VoxelPlayerComponent::resolveIntentSlots( const PawnComponent& pawn )
    {
        bool bResolved             = true;
        _intentSlots._jump         = VoxelPlayerComponentInternal::resolveButton( pawn, _jumpButton, "jump", bResolved );
        _intentSlots._sprint       = VoxelPlayerComponentInternal::resolveButton( pawn, _sprintButton, "sprint", bResolved );
        _intentSlots._break        = VoxelPlayerComponentInternal::resolveButton( pawn, _breakButton, "break", bResolved );
        _intentSlots._place        = VoxelPlayerComponentInternal::resolveButton( pawn, _placeButton, "place", bResolved );
        _intentSlots._hotbarScroll = pawn.findAnalog( _hotbarScrollAnalog );
        if ( _intentSlots._hotbarScroll < 0 )
        {
            SW_LOG_ERROR( "[Voxel] hotbar scroll analog '%#' is not in the pawn's analog list", _hotbarScrollAnalog.c_str() );
            bResolved = false;
        }
        if ( _listSlotButton.size() != static_cast<size_t>( VoxelHotbar::kSlotCount ) )
        {
            SW_LOG_ERROR( "[Voxel] the player needs %# slot buttons, has %#", VoxelHotbar::kSlotCount, static_cast<uint32>( _listSlotButton.size() ) );
            return false;
        }
        for ( int32 slotIndex = 0; slotIndex < VoxelHotbar::kSlotCount; ++slotIndex )
        {
            _intentSlots._arrSlot[slotIndex] = VoxelPlayerComponentInternal::resolveButton( pawn, _listSlotButton[static_cast<size_t>( slotIndex )], "hotbar slot", bResolved );
        }
        return bResolved;
    }

    bool VoxelPlayerComponent::applyBlockNames( const VoxelBlockCatalog& catalog )
    {
        bool bKnown = true;
        _body.setWaterBlock( VoxelPlayerComponentInternal::findBlockOrReport( catalog, _waterBlock, bKnown ) );
        for ( const hashed_string& blockID : _listStartBlock )
        {
            const VoxelBlockIndex index = VoxelPlayerComponentInternal::findBlockOrReport( catalog, blockID, bKnown );
            if ( index != kVoxelAirBlock )
                (void)_hotbar.addBlock( index, 64 ); // 핫바 슬롯보다 많으면 넘친 블록은 버립니다. 슬롯 수는 핫바가 정합니다.
        }
        return bKnown;
    }

    void VoxelPlayerComponent::readIntent( const PawnComponent& pawn, const VoxelBlockCatalog& catalog, float3& outWish, bool& outJump, bool& outSprint,
                                           bool& outBreak, bool& outPlace )
    {
        // 이동 축은 조종 요(yaw)를 기준으로 합니다. 1인칭 카메라가 앞쪽 그룹에서 같은 조종 회전으로 시점을 정해 두었습니다. 위아래 축은 쓰지 않으며, 물에서는 점프가 헤엄이 됩니다.
        const ControlIntent& intent = pawn.getIntent();
        const float3         move   = intent.computeWorldMove();
        outWish                     = float3{ move._x, 0.0f, move._z };
        outJump                     = pawn.isButtonDown( _intentSlots._jump );
        outSprint                   = pawn.isButtonDown( _intentSlots._sprint );
        outBreak                    = pawn.isButtonDown( _intentSlots._break );
        outPlace                    = pawn.isButtonDown( _intentSlots._place ); // 처음 누른 틱도 누른 것으로 봅니다. 놓기 간격은 `_placeCooldown` 이 거릅니다.

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
        const bool  bHit             = VoxelRaycast::raycast( director.getWorld(), _body.getEyePosition(), camera.getLook().getForward(), _reachDistance, hit );
        const bool  bBreakTargetLost = bHit == false || hit._block != _target._block || _bHasTarget == SW_FALSE;
        if ( bBreakTargetLost )
            _breakProgress = 0.0f; // 다른 블록을 보면 처음부터 다시 부숩니다.
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

        // 월드는 틱이 끝난 뒤 디렉터가 바꿉니다. 여기서는 손에 든 블록과 개수만 바로 바꿉니다.
        const VoxelBlockIndex drop = VoxelPlayerComponentInternal::findDrop( catalog, _target._blockIndex );
        _listPendingEdit.push_back( BlockEdit{ _target._block, kVoxelAirBlock } );
        _soundQueue.queueEvent( VoxelPlayerComponentInternal::kEventBreak );
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
            return; // 몸과 겹치는 곳이나 월드 밖에는 놓지 않습니다.
        const VoxelBlockIndex existing = world.getBlock( cell );
        if ( existing != kVoxelAirBlock && catalog.isSolid( existing ) )
            return;
        VoxelBlockIndex block = kVoxelAirBlock;
        if ( _hotbar.consumeSelected( block ) == false )
            return;
        _listPendingEdit.push_back( BlockEdit{ cell, block } );
        _soundQueue.queueEvent( VoxelPlayerComponentInternal::kEventPlace );
        ++_placedCount;
    }

    // ------------------------------------------------------------------------------
    // 틱이 끝난 뒤(게임 스레드)
    // ------------------------------------------------------------------------------
    void VoxelPlayerComponent::scheduleFlush()
    {
        GameObject*        pOwner   = getOwner();
        GameObjectManager* pManager = pOwner != nullptr ? pOwner->getManager() : nullptr;
        if ( pManager == nullptr || _bFlushScheduled == SW_TRUE )
            return;
        _bFlushScheduled = SW_TRUE;
        pManager->executeOrDeferPostTickOnComponent( *this, []( VoxelPlayerComponent& player )
        { player.flushPending(); } );
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
                (void)pDirector->applyBlockEdit( edit._coord, edit._block ); // 월드 밖의 좌표는 앞에서 이미 걸러졌습니다.
        }
        _listPendingEdit.clear();
        _soundQueue.playAll();
    }
} // namespace sw
