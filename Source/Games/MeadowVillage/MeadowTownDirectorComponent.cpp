#include "pch.h"

#include "Games/MeadowVillage/MeadowTownDirectorComponent.h"

#include "Engine/Input/InputManager.h"
#include "Engine/Input/InputMap.h"
#include "Engine/Object/Component/3D/MeshComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Serialization/Format/Archive.h"

#include "GameFramework/Base/Framework/GameService.h"
#include "GameFramework/Base/GameState/GameStateComponent.h"
#include "GameFramework/Base/Utility/StateArchiveUtil.h"
#include "GameFramework/Kits/Simulation/CreatureLife/CreatureLifeCatalog.h"

#include "Games/MeadowVillage/MeadowVillageData.h"

namespace sw
{
    SW_LOG_CALLER( "MeadowTown" );

    namespace
    {
        struct MeadowTownDirectorComponentInternal
        {
            /** @brief 마을 오브젝트의 색 — 나무 짙은 초록, 그 밖(풀) 초록. */
            static float4 findObjectColor( const hashed_string& object )
            {
                return object == hashed_string( "tree" ) ? float4{ 0.10f, 0.40f, 0.15f, 1.0f } : float4{ 0.30f, 0.70f, 0.30f, 1.0f };
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    MeadowTownDirectorComponent::MeadowTownDirectorComponent()
        : _tilePrefab{ "game/meadowvillage/prefabs/tile.prefab.xml" }
        , _town{}
        , _tintCache{}
        , _listView{}
        , _bViewsDirty{ SW_FALSE }
        , _reserved{ 0 }
    {
    }

    MeadowTownDirectorComponent::~MeadowTownDirectorComponent() = default;

    void MeadowTownDirectorComponent::writeState( Archive& outArchive ) const
    {
        StateArchiveUtil::writeHeader( outArchive, kStateTag, kStateVersion );
        Archive body;
        _town.writeState( body );
        StateArchiveUtil::writeSection( outArchive, CreatureTown::kStateTag, CreatureTown::kStateVersion, body );
    }

    bool MeadowTownDirectorComponent::startGame()
    {
        GameStateComponent* pState = GameStateComponent::findOnOwner( *this );
        if ( pState == nullptr )
        {
            SW_LOG_WARNING( "[Meadow] the town cannot start - no GameStateComponent in front of it on this object" );
            return false;
        }
        (void)pState->initialize( MeadowVillageData::makeStateSettings() ); // 밭 디렉터가 이미 열었으면 아무것도 하지 않는다
        if ( initializeTown( _town, *pState ) == false )
            return false;
        if ( pState->isFreshGame() )
        {
            (void)_town.setObject( 0, 0, "grass" );
            (void)_town.setObject( 1, 0, "grass" );
        }
        _bViewsDirty = SW_TRUE;
        return true;
    }

    bool MeadowTownDirectorComponent::readState( Archive& archive )
    {
        GameStateComponent* pState  = GameStateComponent::findOnOwner( *this );
        uint32              tag     = 0;
        uint32              version = 0;
        Archive             body;
        const bool          bFrameRead = pState != nullptr && StateArchiveUtil::readHeader( archive, kStateTag, kStateVersion ) &&
                                StateArchiveUtil::readSection( archive, tag, version, body );
        if ( bFrameRead == false || tag != CreatureTown::kStateTag || version != CreatureTown::kStateVersion )
            return false;
        CreatureTown town;
        if ( initializeTown( town, *pState ) == false )
            return false;
        const bool bRead = town.readState( body ) && body.getRemainingBytes() == 0 && archive.getRemainingBytes() == 0;
        if ( bRead == false )
            return false;
        _town        = std::move( town );
        _bViewsDirty = SW_TRUE;
        return true;
    }

    void MeadowTownDirectorComponent::tickGame( float32 deltaTime )
    {
        (void)deltaTime;
        GameStateComponent* pState = GameStateComponent::findOnOwner( *this );
        if ( pState == nullptr )
            return;
        const WorldClock&   clock = pState->getClock();
        const hashed_string sprout( MeadowVillageData::kSproutSpecies );
        for ( const WorldClockEvent& clockEvent : pState->getClockEvents() )
        {
            if ( clockEvent._kind == WorldClockEvent::Kind::HourChanged )
            {
                if ( 0 < _town.attractVisitorsAt( clock.getDay(), clockEvent._value, clock.getDayPhase(), "sunny" ) )
                {
                    SW_LOG_INFO( "[Meadow] a creature moved in on day %#", clock.getDay() );
                    _bViewsDirty = SW_TRUE;
                }
            }
            else if ( clockEvent._kind == WorldClockEvent::Kind::DayChanged )
            {
                _town.advanceDay();
            }
        }
        const hashed_string request( MeadowVillageData::kRequestQuest );
        const bool          bCanAsk = _town.findCreature( sprout ) != nullptr && pState->getQuestLog().getStatus( request ) == QuestStatus::NotStarted;
        if ( bCanAsk && _town.startRequest( sprout, request ) == CreatureRequestResult::Ok )
            SW_LOG_INFO( "[Meadow] sprout asks for an orchard (%# G)", kOrchardPrice );

        // 밭이 이번 틱에 번 돈도 여기서 보인다 — 같은 오브젝트에서 밭이 먼저 돈다.
        Wallet&    wallet         = pState->getWallet();
        const bool bCanBuyOrchard = _town.countHabitats( "orchard" ) == 0 && wallet.canAfford( Wallet::getDefaultCurrency(), kOrchardPrice );
        if ( bCanBuyOrchard && wallet.trySpend( Wallet::getDefaultCurrency(), kOrchardPrice ) )
        {
            (void)_town.setObject( 0, 2, "tree" );
            (void)_town.setObject( 1, 2, "tree" );
            SW_LOG_INFO( "[Meadow] orchard planted on day %# - request %#", clock.getDay(),
                         pState->getQuestLog().getStatus( request ) == QuestStatus::Completed ? "done" : "still open" );
            _bViewsDirty = SW_TRUE;
        }

        // 키는 입력 맵(`data/meadow.input.xml`)이 정한다. 대화는 하루 한 번(키트가 막는다).
        const InputManager* pInput = game::getService<InputManager>();
        if ( pInput != nullptr && pInput->getInputMap().wasActionTriggered( hashed_string( MeadowVillageData::kTalkAction ) ) )
        {
            const CreatureInteractResult result = _town.talkTo( sprout );
            SW_LOG_INFO( "[Meadow] talk to sprout: %# (friendship %#)", toString( result ), _town.getFriendship( sprout ) );
        }
    }

    void MeadowTownDirectorComponent::onFlush( GameObjectManager& manager, bool bRespawnViews )
    {
        if ( bRespawnViews == false && _bViewsDirty == SW_FALSE )
            return;
        for ( GameObjectHandle& handle : _listView )
            destroySpawned( manager, handle );
        _listView.clear();
        spawnTown( manager );
        _bViewsDirty = SW_FALSE;
    }

    void MeadowTownDirectorComponent::onViewsDespawned()
    {
        _listView.clear();
    }

    bool MeadowTownDirectorComponent::initializeTown( CreatureTown& outTown, GameStateComponent& state )
    {
        const CreatureLifeCatalog* pCatalog = game::getService<CreatureLifeCatalog>();
        if ( pCatalog == nullptr )
        {
            SW_LOG_WARNING( "[Meadow] the town cannot start - the creature catalog is not loaded" );
            return false;
        }
        outTown.initialize( pCatalog, state.makeRefs(), kTownSize, kTownSize, CreatureTownSettings{} );
        return true;
    }

    void MeadowTownDirectorComponent::spawnTown( GameObjectManager& manager )
    {
        for ( int32 y = 0; y < _town.getHeight(); ++y )
        {
            for ( int32 x = 0; x < _town.getWidth(); ++x )
            {
                const hashed_string* pObject = _town.findObject( x, y );
                if ( pObject == nullptr || pObject->empty() )
                    continue;
                const float3 position{ kTownOffsetX + static_cast<float32>( x ) + 0.5f, 0.3f, static_cast<float32>( y ) + 0.5f };
                spawnCube( manager, position, MeadowTownDirectorComponentInternal::findObjectColor( *pObject ) );
            }
        }
        for ( const TownCreature& creature : _town.getCreatures() )
        {
            const HabitatInstance* pHabitat = _town.findHabitat( creature._habitat );
            const int2             anchor   = pHabitat != nullptr ? pHabitat->_origin : int2{ 0, 0 };
            const float3           position{ kTownOffsetX + static_cast<float32>( anchor._x ) + 0.5f, 0.8f, static_cast<float32>( anchor._y ) + 0.5f };
            spawnCube( manager, position, float4{ 1.0f, 0.9f, 0.2f, 1.0f } );
        }
    }

    void MeadowTownDirectorComponent::spawnCube( GameObjectManager& manager, const float3& position, const float4& color )
    {
        GameObject*    pView = spawnPrefab( manager, _tilePrefab, "MeadowTownView" );
        MeshComponent* pMesh = pView != nullptr ? pView->getComponent<MeshComponent>() : nullptr;
        if ( pMesh == nullptr )
            return;
        pMesh->setLocalPosition( position );
        _tintCache.apply( *pMesh, color );
        _listView.push_back( pView->getHandle() );
    }
} // namespace sw
