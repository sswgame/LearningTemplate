#include "pch.h"

#include "Games/MeadowVillage/MeadowTownGameModeComponent.h"

#include "Engine/Input/InputManager.h"
#include "Engine/Input/Map/InputMap.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Reflection/EnumName.h"
#include "Engine/Serialization/Format/Archive.h"
#include "Engine/UI/Input/UIInputMapView.h"
#include "Engine/UI/UISystem.h"

#include "GameFramework/Kits/Genre/Simulation/CreatureLife/CreatureLifeCatalog.h"
#include "GameFramework/Runtime/Foundation/Framework/GameService.h"
#include "GameFramework/Runtime/Foundation/Utility/StateArchiveUtil.h"
#include "GameFramework/Runtime/Gameplay/SessionState/SessionStateComponent.h"

#include "Games/MeadowVillage/MeadowVillageData.h"

namespace sw
{
    namespace
    {
        const MeadowTownView* findView( const vector<MeadowTownView>& listView, const hashed_string& id )
        {
            for ( const MeadowTownView& view : listView )
            {
                if ( view._id == id )
                    return &view;
            }
            return nullptr;
        }
    } // namespace
} // namespace sw

namespace sw
{
    MeadowTownGameModeComponent::MeadowTownGameModeComponent()
        : _listObjectView{}
        , _listCreatureView{}
        , _orchardPrice{ 50 }
        , _townOffsetX{ 6.0f }
        , _town{}
        , _listView{}
        , _bViewsDirty{ SW_FALSE }
        , _reserved{ 0 }
    {
    }

    MeadowTownGameModeComponent::~MeadowTownGameModeComponent() = default;

    void MeadowTownGameModeComponent::writeState( Archive& outArchive ) const
    {
        StateArchiveUtil::writeHeader( outArchive, kStateTag, kStateVersion );
        Archive body;
        _town.writeState( body );
        StateArchiveUtil::writeSection( outArchive, CreatureTown::kStateTag, CreatureTown::kStateVersion, body );
    }

    bool MeadowTownGameModeComponent::startGame()
    {
        SessionStateComponent* pState = SessionStateComponent::findOnOwner( *this );
        if ( pState == nullptr )
        {
            SW_LOG_ERROR_ID( "MeadowTown.TownCannotStartNo", "[Meadow] the town cannot start - no SessionStateComponent in front of it on this object" );
            return false;
        }
        // 밭 디렉터가 이미 열었으면 아무것도 하지 않는다. 범위 밖 설정은 판 상태가 오류를 남겼다.
        if ( pState->initialize( MeadowVillageData::makeStateSettings() ) == SessionStateInitResult::Refused )
            return false;
        if ( initializeTown( _town, *pState ) == false || validateViews( *game::findSubsystem<CreatureLifeCatalog>() ) == false )
            return false;
        if ( pState->isFreshGame() )
        {
            (void)_town.setObject( 0, 0, "grass" );
            (void)_town.setObject( 1, 0, "grass" );
        }
        _bViewsDirty = SW_TRUE;
        return true;
    }

    bool MeadowTownGameModeComponent::readState( Archive& archive )
    {
        SessionStateComponent* pState = SessionStateComponent::findOnOwner( *this );
        Archive                body;
        const bool             bFrameRead = pState != nullptr && StateArchiveUtil::readHeader( archive, kStateTag, kStateVersion ) &&
                                StateArchiveUtil::readExpectedSection( archive, CreatureTown::kStateTag, CreatureTown::kStateVersion, body );
        if ( bFrameRead == false )
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

    void MeadowTownGameModeComponent::updateGame( float32 deltaTime )
    {
        (void)deltaTime;
        SessionStateComponent* pState = SessionStateComponent::findOnOwner( *this );
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
                    SW_LOG_INFO_ID( "MeadowTown.CreatureMovedDay", "[Meadow] a creature moved in on day %#", clock.getDay() );
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
            SW_LOG_INFO_ID( "MeadowTown.SproutAsksOrchardG", "[Meadow] sprout asks for an orchard (%# G)", _orchardPrice );

        // 밭이 이번 틱에 번 돈도 여기서 보인다 — 같은 오브젝트에서 밭이 먼저 돈다.
        Wallet&    wallet         = pState->getWallet();
        const bool bCanBuyOrchard = _town.countHabitats( "orchard" ) == 0 && wallet.canAfford( Wallet::getDefaultCurrency(), _orchardPrice );
        if ( bCanBuyOrchard && wallet.trySpend( Wallet::getDefaultCurrency(), _orchardPrice ) )
        {
            (void)_town.setObject( 0, 2, "tree" );
            (void)_town.setObject( 1, 2, "tree" );
            SW_LOG_INFO_ID( "MeadowTown.OrchardPlantedDayRequest", "[Meadow] orchard planted on day %# - request %#", clock.getDay(),
                            pState->getQuestLog().getStatus( request ) == QuestStatus::Completed ? "done" : "still open" );
            _bViewsDirty = SW_TRUE;
        }

        // 키는 입력 맵(`data/gamesettings.xml` 의 입력 맵 묶음)이 정한다. 대화는 하루 한 번(키트가 막는다).
        const InputManager* pInput = game::findSubsystem<InputManager>();
        if ( pInput != nullptr && UIInputMapView( pInput->getInputMap(), game::findSubsystem<UISystem>() ).wasActionTriggered( hashed_string( MeadowVillageData::kTalkAction ) ) )
        {
            const CreatureInteractResult result = _town.talkTo( sprout );
            SW_LOG_INFO_ID( "MeadowTown.TalkSproutFriendship", "[Meadow] talk to sprout: %# (friendship %#)", getEnumName( result ), _town.getFriendship( sprout ) );
        }
    }

    void MeadowTownGameModeComponent::onFlush( GameObjectManager& manager, bool bRespawnViews )
    {
        if ( bRespawnViews == false && _bViewsDirty == SW_FALSE )
            return;
        for ( GameObjectHandle& handle : _listView )
        {
            destroySpawned( manager, handle );
        }
        _listView.clear();
        spawnTown( manager );
        _bViewsDirty = SW_FALSE;
    }

    void MeadowTownGameModeComponent::onViewsDespawned()
    {
        _listView.clear();
    }

    bool MeadowTownGameModeComponent::initializeTown( CreatureTown& outTown, SessionStateComponent& state )
    {
        const CreatureLifeCatalog* pCatalog = game::findSubsystem<CreatureLifeCatalog>();
        if ( pCatalog == nullptr )
        {
            SW_LOG_ERROR_ID( "MeadowTown.TownCannotStartCreature", "[Meadow] the town cannot start - the creature catalog is not loaded" );
            return false;
        }
        return outTown.initialize( pCatalog, state.makeRefs(), kTownSize, kTownSize, CreatureTownSettings{} );
    }

    bool MeadowTownGameModeComponent::validateViews( const CreatureLifeCatalog& catalog ) const
    {
        bool bValid = true;
        for ( const HabitatDef& habitat : catalog.getHabitats() )
        {
            for ( const HabitatCell& cell : habitat._listCell )
            {
                if ( cell._object.empty() || findView( _listObjectView, cell._object ) != nullptr )
                    continue;
                SW_LOG_ERROR_ID( "MeadowTown.ObjectViewMissing", "[Meadow] the town cannot start - object '%#' of habitat '%#' has no row in Object Views", cell._object.c_str(), habitat._id.c_str() );
                bValid = false;
            }
        }
        for ( const CreatureSpeciesDef& species : catalog.getSpecies() )
        {
            if ( findView( _listCreatureView, species._id ) != nullptr )
                continue;
            SW_LOG_ERROR_ID( "MeadowTown.CreatureViewMissing", "[Meadow] the town cannot start - species '%#' has no row in Creature Views", species._id.c_str() );
            bValid = false;
        }
        return bValid;
    }

    void MeadowTownGameModeComponent::spawnTown( GameObjectManager& manager )
    {
        for ( int32 y = 0; y < _town.getHeight(); ++y )
        {
            for ( int32 x = 0; x < _town.getWidth(); ++x )
            {
                const hashed_string* pObject = _town.findObject( x, y );
                if ( pObject == nullptr || pObject->empty() )
                    continue;
                spawnView( manager, _listObjectView, *pObject, float3{ _townOffsetX + static_cast<float32>( x ) + 0.5f, 0.0f, static_cast<float32>( y ) + 0.5f } );
            }
        }
        // 생물은 머무는 서식지가 차지한 셀들의 가운데에 선다(서식지가 사라졌으면 마을 모서리).
        for ( const TownCreature& creature : _town.getCreatures() )
        {
            const HabitatInstance* pHabitat = _town.findHabitat( creature._habitat );
            float2                 center{ 0.5f, 0.5f };
            if ( pHabitat != nullptr && pHabitat->_listTile.empty() == false )
            {
                center = float2{ 0.0f, 0.0f };
                for ( const int32 tile : pHabitat->_listTile )
                {
                    center._x += static_cast<float32>( tile % _town.getWidth() ) + 0.5f;
                    center._y += static_cast<float32>( tile / _town.getWidth() ) + 0.5f;
                }
                const float32 count = static_cast<float32>( pHabitat->_listTile.size() );
                center              = float2{ center._x / count, center._y / count };
            }
            spawnView( manager, _listCreatureView, creature._speciesID, float3{ _townOffsetX + center._x, 0.0f, center._y } );
        }
    }

    void MeadowTownGameModeComponent::spawnView( GameObjectManager& manager, const vector<MeadowTownView>& listView, const hashed_string& id, const float3& position )
    {
        const MeadowTownView* pRow = findView( listView, id );
        if ( pRow == nullptr )
        {
            SW_LOG_ERROR_ID( "MeadowTown.ViewMissing", "[Meadow] '%#' has no town view row - not drawn", id.c_str() );
            return;
        }
        GameObject*     pView = spawnPrefab( manager, pRow->_prefab, "MeadowTownView" );
        SceneComponent* pRoot = pView != nullptr ? pView->getComponent<SceneComponent>() : nullptr;
        if ( pRoot == nullptr )
            return;
        pRoot->setLocalPosition( position );
        _listView.push_back( pView->getHandle() );
    }
} // namespace sw
