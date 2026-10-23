#include "pch.h"

#include "Games/PixelTown/TownDirectorComponent.h"

#include "Core/Common/FourCcUtil.h"
#include "Core/Container/VectorUtil.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Object/Component/2D/TileMapRendererComponent.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Probe/AutomationProbe.h"
#include "Engine/Serialization/Format/Archive.h"

#include "GameFramework/Base/Actor/Control/Intent/ControlIntent.h"
#include "GameFramework/Base/Actor/Control/Pawn/PawnComponent.h"
#include "GameFramework/Base/Foundation/Utility/StateArchiveUtil.h"
#include "GameFramework/Base/UI/Dialogue/DialogueRunnerComponent.h"

namespace sw
{
    SW_LOG_CALLER( "TownDirector" );

    namespace
    {
        constexpr uint32 kTownStateTag     = FourCcUtil::make( "TOWN" );
        constexpr uint32 kTownStateVersion = 1;

        /** @brief 프로브가 읽는 디렉터입니다. 씬의 첫 마을 디렉터이며, 프로브는 단언 단계에서만 불리므로 프레임 경로가 아닙니다. */
        const TownDirectorComponent* findTownProbeDirector( const GameObjectManager* pManager )
        {
            const TownDirectorComponent* pFound = nullptr;
            if ( pManager != nullptr )
            {
                pManager->forEachComponentOfType<TownDirectorComponent>( [&pFound]( const TownDirectorComponent* pDirector )
                {
                    if ( pFound == nullptr )
                        pFound = pDirector;
                } );
            }
            return pFound;
        }

        [[nodiscard]] bool readHeroTileX( const GameObjectManager* pManager, float64& outValue )
        {
            const TownDirectorComponent* pDirector = findTownProbeDirector( pManager );
            if ( pDirector == nullptr )
                return false;
            outValue = pDirector->getHeroTileX();
            return true;
        }

        [[nodiscard]] bool readHeroTileY( const GameObjectManager* pManager, float64& outValue )
        {
            const TownDirectorComponent* pDirector = findTownProbeDirector( pManager );
            if ( pDirector == nullptr )
                return false;
            outValue = pDirector->getHeroTileY();
            return true;
        }

        [[nodiscard]] bool readHeroFacing( const GameObjectManager* pManager, float64& outValue )
        {
            const TownDirectorComponent* pDirector = findTownProbeDirector( pManager );
            if ( pDirector == nullptr )
                return false;
            outValue = static_cast<float64>( pDirector->getHeroFacing() );
            return true;
        }

        [[nodiscard]] bool readTalking( const GameObjectManager* pManager, float64& outValue )
        {
            const TownDirectorComponent* pDirector = findTownProbeDirector( pManager );
            if ( pDirector == nullptr )
                return false;
            outValue = pDirector->isTalking() ? 1.0 : 0.0;
            return true;
        }

        [[nodiscard]] bool readShownLineCount( const GameObjectManager* pManager, float64& outValue )
        {
            const TownDirectorComponent* pDirector = findTownProbeDirector( pManager );
            if ( pDirector == nullptr )
                return false;
            outValue = pDirector->getShownLineCount();
            return true;
        }

        [[nodiscard]] bool readFinishedTalkCount( const GameObjectManager* pManager, float64& outValue )
        {
            const TownDirectorComponent* pDirector = findTownProbeDirector( pManager );
            if ( pDirector == nullptr )
                return false;
            outValue = pDirector->getFinishedTalkCount();
            return true;
        }
    } // namespace

    SW_AUTOMATION_PROBE( townHeroTileX, "Town.HeroTileX", "Tile column of the hero", &readHeroTileX );
    SW_AUTOMATION_PROBE( townHeroTileY, "Town.HeroTileY", "Tile row of the hero (rows grow down)", &readHeroTileY );
    SW_AUTOMATION_PROBE( townHeroFacing, "Town.HeroFacing", "Hero facing (0 down, 1 left, 2 right, 3 up)", &readHeroFacing );
    SW_AUTOMATION_PROBE( townTalking, "Town.Talking", "1 while a talk with a villager is open", &readTalking );
    SW_AUTOMATION_PROBE( townShownLines, "Town.ShownLines", "Dialogue lines shown this session", &readShownLineCount );
    SW_AUTOMATION_PROBE( townFinishedTalks, "Town.FinishedTalks", "Talks finished this session", &readFinishedTalkCount );
} // namespace sw

namespace sw
{
    TownDirectorComponent::TownDirectorComponent()
        : _ground{}
        , _props{}
        , _hero{}
        , _interactButton{}
        , _stepSeconds{ 0.18f }
        , _tileMap{}
        , _mover{}
        , _listVillager{}
        , _mapOrigin{ 0.0f, 0.0f, 0.0f }
        , _heroPosition{ 0.0f, 0.0f, 0.0f }
        , _stepFromTile{ 0, 0 }
        , _tileSize{ 1.0f }
        , _stepElapsed{ 0.0f }
        , _talkTarget{}
        , _pendingTarget{}
        , _shownLineCount{ 0 }
        , _finishedTalkCount{ 0 }
        , _pendingTalk{ TalkRequest::None }
    {
    }

    TownDirectorComponent::~TownDirectorComponent() = default;

    // ------------------------------------------------------------------------------
    // 데이터
    // ------------------------------------------------------------------------------
    bool TownDirectorComponent::startGame()
    {
        GameObjectManager* pManager = getObjectManager();
        if ( pManager == nullptr )
            return false;
        const TileMapRendererComponent* pGround = findLayer( _ground, "ground" );
        if ( pGround == nullptr )
            return false;
        if ( _tileMap.assign( pGround->getTileMap(), pGround->getTileSet() ) == false )
        {
            SW_LOG_ERROR( "[Town] the ground layer '%#' cannot become a walk grid", pGround->getTileMap()._sourcePath );
            return false;
        }
        if ( _props.isValid() )
        {
            const TileMapRendererComponent* pProps = findLayer( _props, "props" );
            if ( pProps == nullptr || _tileMap.addSolidLayer( pProps->getTileMap(), pProps->getTileSet() ) == false )
                return false;
        }
        const GameObject*     pGroundObject = pManager->resolveGameObject( _ground );
        const SceneComponent* pGroundScene  = pGroundObject->getPrimarySceneComponent();
        if ( pGroundScene == nullptr )
        {
            SW_LOG_ERROR( "[Town] the ground object has no scene component - the map has no place in the world" );
            return false;
        }
        _mapOrigin = pGroundScene->getWorldPosition();
        _tileSize  = pGround->getTileSet().getTileSize();
        if ( findHeroPawn() == nullptr )
        {
            SW_LOG_ERROR( "[Town] the hero object has no PawnComponent - the town cannot start" );
            return false;
        }
        if ( findHeroPawn()->findButton( _interactButton ) < 0 )
        {
            SW_LOG_ERROR( "[Town] the hero pawn has no button '%#' (Interact Button)", _interactButton.c_str() );
            return false;
        }
        if ( collectVillagers( *pManager ) == false )
            return false;

        OverworldTileMoverSettings settings;
        settings._encounterRate = 0.0f; // 마을에는 조우가 없다
        settings._stepSeconds   = _stepSeconds;
        _mover                  = OverworldTileMover{};
        _mover.setSettings( settings );
        _mover.setTileMap( &_tileMap );
        _mover.setPosition( _tileMap.getSpawnX(), _tileMap.getSpawnY() );
        _stepFromTile      = int2{ _tileMap.getSpawnX(), _tileMap.getSpawnY() };
        _stepElapsed       = _stepSeconds;
        _talkTarget        = GameObjectHandle{};
        _pendingTarget     = GameObjectHandle{};
        _pendingTalk       = TalkRequest::None;
        _shownLineCount    = 0;
        _finishedTalkCount = 0;
        updateHeroPosition();
        return true;
    }

    const TileMapRendererComponent* TownDirectorComponent::findLayer( GameObjectHandle layer, const utf8* pRole ) const
    {
        GameObjectManager*              pManager  = getObjectManager();
        const GameObject*               pObject   = pManager != nullptr ? pManager->resolveGameObject( layer ) : nullptr;
        const TileMapRendererComponent* pRenderer = pObject != nullptr ? pObject->getComponent<TileMapRendererComponent>() : nullptr;
        if ( pRenderer == nullptr || pRenderer->getTileMap()._width <= 0 )
        {
            SW_LOG_ERROR( "[Town] the %# layer object has no loaded TileMapRendererComponent", pRole );
            return nullptr;
        }
        return pRenderer;
    }

    bool TownDirectorComponent::collectVillagers( GameObjectManager& manager )
    {
        _listVillager.clear();
        bool bAllPlaced = true;
        manager.forEachComponentOfType<DialogueRunnerComponent>( [this, &bAllPlaced]( const DialogueRunnerComponent* pRunner )
        {
            const GameObject*     pObject = pRunner->getOwner();
            const SceneComponent* pScene  = pObject != nullptr ? pObject->getPrimarySceneComponent() : nullptr;
            if ( pScene == nullptr )
                return;
            const int2 tile          = computeTile( pScene->getWorldPosition() );
            const bool bInsideTheMap = 0 <= tile._x && tile._x < _tileMap.getWidth() && 0 <= tile._y && tile._y < _tileMap.getHeight();
            if ( bInsideTheMap == false )
            {
                SW_LOG_ERROR( "[Town] villager '%#' stands outside the %#x%# map (tile %#,%#)", pObject->getName().c_str(), _tileMap.getWidth(), _tileMap.getHeight(),
                              tile._x, tile._y );
                bAllPlaced = false;
                return;
            }
            _tileMap.setWalkable( tile._x, tile._y, false ); // 주민이 선 칸은 걸어 들어갈 수 없다
            _listVillager.push_back( Villager{ tile, pObject->getHandle() } );
        } );
        return bAllPlaced;
    }

    int2 TownDirectorComponent::computeTile( const float3& worldPosition ) const
    {
        const float32 column = ( worldPosition._x - _mapOrigin._x ) / _tileSize;
        const float32 row    = ( _mapOrigin._y - worldPosition._y ) / _tileSize;
        return int2{ static_cast<int32>( MathUtil::floor( column ) ), static_cast<int32>( MathUtil::floor( row ) ) };
    }

    float3 TownDirectorComponent::computeTileCenter( int32 x, int32 y ) const
    {
        // 렌더러와 같은 배치 — 열은 +X, 행은 -Y 로 자란다(`TileMapRendererComponent`).
        return float3{ _mapOrigin._x + ( static_cast<float32>( x ) + 0.5f ) * _tileSize, _mapOrigin._y - ( static_cast<float32>( y ) + 0.5f ) * _tileSize, _mapOrigin._z };
    }

    const TownDirectorComponent::Villager* TownDirectorComponent::findVillager( const int2& tile ) const
    {
        return VectorUtil::findBy<&Villager::_tile>( _listVillager, tile );
    }

    const PawnComponent* TownDirectorComponent::findHeroPawn() const
    {
        GameObjectManager* pManager = getObjectManager();
        const GameObject*  pObject  = pManager != nullptr ? pManager->resolveGameObject( _hero ) : nullptr;
        return pObject != nullptr ? pObject->getComponent<PawnComponent>() : nullptr;
    }

    // ------------------------------------------------------------------------------
    // 상태
    // ------------------------------------------------------------------------------
    void TownDirectorComponent::writeState( Archive& outArchive ) const
    {
        StateArchiveUtil::writeHeader( outArchive, kTownStateTag, kTownStateVersion );
        Archive moverBody;
        _mover.writeState( moverBody );
        StateArchiveUtil::writeSection( outArchive, OverworldTileMover::kStateTag, OverworldTileMover::kStateVersion, moverBody );
    }

    bool TownDirectorComponent::readState( Archive& archive )
    {
        if ( StateArchiveUtil::readHeader( archive, kTownStateTag, kTownStateVersion ) == false )
            return false;
        uint32             moverTag     = 0;
        uint32             moverVersion = 0;
        Archive            moverBody;
        OverworldTileMover mover      = _mover;
        const bool         bMoverRead = StateArchiveUtil::readSection( archive, moverTag, moverVersion, moverBody ) && moverTag == OverworldTileMover::kStateTag &&
                                moverVersion == OverworldTileMover::kStateVersion && mover.readState( moverBody ) && moverBody.getRemainingBytes() == 0;
        const bool bStateRead = bMoverRead && archive.isError() == false && archive.getRemainingBytes() == 0;
        if ( bStateRead == false )
            return false;
        _mover = mover;
        _mover.setTileMap( &_tileMap );
        _mover.setInputEnabled( true ); // 대화는 저장하지 않는다 — 복원한 판은 대화 밖이다
        return true;
    }

    void TownDirectorComponent::onStateRestored( bool bRestored )
    {
        if ( bRestored == false )
            SW_LOG_WARNING( "[Town] the saved town state does not match this build - starting at the spawn tile" );
        _stepFromTile = int2{ _mover.getTileX(), _mover.getTileY() };
        _stepElapsed  = _stepSeconds;
        updateHeroPosition();
    }

    void TownDirectorComponent::onGameStarted()
    {
        SW_LOG_INFO( "[Town] town is ready - WASD / arrows walk one tile, Space / E talk to the villager you face (%# villager(s))",
                     static_cast<uint32>( _listVillager.size() ) );
    }

    // ------------------------------------------------------------------------------
    // 규칙(PrePhysics, 워커)
    // ------------------------------------------------------------------------------
    void TownDirectorComponent::updateGame( float32 deltaTime )
    {
        if ( deltaTime <= 0.0f )
            return;
        const PawnComponent* pHero = findHeroPawn();
        if ( pHero == nullptr )
            return;
        const ControlIntent& intent         = pHero->getIntent();
        const int32          interactButton = pHero->findButton( _interactButton );
        if ( isTalking() )
        {
            // 대화 중에는 걷지 않는다. 상호작용 버튼은 다음 줄이다.
            if ( intent.wasTriggered( interactButton ) )
                _pendingTalk = TalkRequest::Advance;
            return;
        }

        const int2 tileBefore{ _mover.getTileX(), _mover.getTileY() };
        _mover.update( deltaTime, intent, interactButton );
        if ( _mover.consumeMovedFlag() )
        {
            _stepFromTile = tileBefore;
            _stepElapsed  = 0.0f;
        }
        else
        {
            _stepElapsed = MathUtil::min( _stepElapsed + deltaTime, _stepSeconds );
        }
        if ( _mover.consumeInteractRequest() )
        {
            int2 facingTile{};
            _mover.getFacingTile( facingTile._x, facingTile._y );
            const Villager* pVillager = findVillager( facingTile );
            if ( pVillager != nullptr )
            {
                _pendingTalk   = TalkRequest::Start;
                _pendingTarget = pVillager->_object;
            }
        }
        updateHeroPosition();
    }

    void TownDirectorComponent::updateHeroPosition()
    {
        const float3  to       = computeTileCenter( _mover.getTileX(), _mover.getTileY() );
        const float3  from     = computeTileCenter( _stepFromTile._x, _stepFromTile._y );
        const float32 progress = _stepSeconds > 0.0f ? MathUtil::clamp( _stepElapsed / _stepSeconds, 0.0f, 1.0f ) : 1.0f;
        _heroPosition          = from + ( to - from ) * progress;
    }

    // ------------------------------------------------------------------------------
    // 대화(틱이 끝난 뒤, 게임 스레드)
    // ------------------------------------------------------------------------------
    void TownDirectorComponent::onFlush( GameObjectManager& manager, bool bRespawnViews )
    {
        (void)bRespawnViews; // 세우는 런타임 오브젝트가 없다 — 모습은 모두 씬에 있다
        processTalk( manager );
    }

    void TownDirectorComponent::processTalk( GameObjectManager& manager )
    {
        const TalkRequest request = _pendingTalk;
        _pendingTalk              = TalkRequest::None;
        if ( request == TalkRequest::None )
            return;
        const GameObjectHandle   target  = request == TalkRequest::Start ? _pendingTarget : _talkTarget;
        GameObject*              pObject = manager.resolveGameObject( target );
        DialogueRunnerComponent* pRunner = pObject != nullptr ? pObject->getComponent<DialogueRunnerComponent>() : nullptr;
        _pendingTarget                   = GameObjectHandle{};
        if ( pRunner == nullptr )
        {
            SW_LOG_ERROR( "[Town] the villager to talk to is gone - the talk is closed" );
            _talkTarget = GameObjectHandle{};
            return;
        }
        const bool bStepped = request == TalkRequest::Start ? pRunner->startDialogue() : pRunner->advance();
        if ( bStepped == false && request == TalkRequest::Start )
        {
            SW_LOG_ERROR( "[Town] villager '%#' has no dialogue to start (empty or unreadable graph)", pObject->getName().c_str() );
            return;
        }
        switch ( pRunner->getState() )
        {
            case DialogueRunnerState::ShowingDialogue:
            {
                _talkTarget = target;
                ++_shownLineCount;
                break;
            }
            case DialogueRunnerState::WaitingForChoice:
            {
                // 이 게임에는 선택지 UI 가 없다 — 고를 수 없는 대화를 열어 두면 주인공이 멈춘다. 데이터 오류로 알리고 닫는다.
                SW_LOG_ERROR( "[Town] villager '%#' asks a choice, but PixelTown has no choice UI - the talk is closed", pObject->getName().c_str() );
                pRunner->stopDialogue();
                _talkTarget = GameObjectHandle{};
                break;
            }
            case DialogueRunnerState::Idle:
            case DialogueRunnerState::Finished:
            {
                _talkTarget = GameObjectHandle{};
                ++_finishedTalkCount;
                break;
            }
        }
    }
} // namespace sw
