#include "pch.h"

#include "GameFramework/Kits/Genre/Simulation/CreatureLife/Client/CreatureTownViewComponent.h"

#include "Engine/Asset/AssetManager.h"
#include "Engine/Foundation/WorldOrigin.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/Prefab/PrefabCache.h"

#include "GameFramework/Kits/Genre/Simulation/CreatureLife/Component/CreatureTownGameModeComponent.h"
#include "GameFramework/Runtime/Foundation/Service/GameService.h"

namespace sw
{
    namespace
    {

        struct CreatureTownViewComponentInternal
        {
            static constexpr float32 kHalfCell = 0.5f;

            static const CreatureTownObjectView* findView( const vector<CreatureTownObjectView>& listView, const hashed_string& object )
            {
                for ( const CreatureTownObjectView& view : listView )
                {
                    if ( view._object.key() == object )
                        return &view;
                }
                return nullptr;
            }

            static const CreatureTownCreatureView* findView( const vector<CreatureTownCreatureView>& listView, const hashed_string& species )
            {
                for ( const CreatureTownCreatureView& view : listView )
                {
                    if ( view._species.key() == species )
                        return &view;
                }
                return nullptr;
            }

            static void destroyView( GameObjectManager& manager, GameObjectHandle handle )
            {
                GameObject* const pView = manager.resolveGameObject( handle );
                if ( pView != nullptr )
                    manager.destroyGameObject( pView );
            }

            template <typename TID>
            static void destroyViews( GameObjectManager& manager, SimulationViewSync<TID>& inoutViews )
            {
                for ( const typename SimulationViewSync<TID>::Slot& slot : inoutViews.getSlots() )
                {
                    destroyView( manager, slot._object );
                }
                inoutViews.clear();
            }
        };
    } // namespace

    CreatureTownViewComponent::CreatureTownViewComponent()
        : GameModeViewComponent( GameModeViewBinding::of<&CreatureTownViewComponent::updateView, &CreatureTownViewComponent::submitInput>() )
        , _listObjectView{}
        , _listCreatureView{}
        , _townOrigin{}
        , _talkAction{}
        , _talkSpecies{}
        , _objectViews{}
        , _creatureViews{}
        , _pValidatedCatalog{}
        , _shownRevision{ 0 }
        , _bViewsRejected{ SW_FALSE }
        , _reserved{ 0 }
    {
    }

    CreatureTownViewComponent::~CreatureTownViewComponent() = default;

    void CreatureTownViewComponent::onBeginPlay()
    {
        GameModeViewComponent::onBeginPlay();
        _pValidatedCatalog.reset();
        _bViewsRejected = SW_FALSE;
    }

    void CreatureTownViewComponent::onEndPlay()
    {
        despawnViews();
        GameModeViewComponent::onEndPlay();
    }

    void CreatureTownViewComponent::despawnViews()
    {
        GameObject* const        pOwner   = getOwner();
        GameObjectManager* const pManager = pOwner != nullptr ? pOwner->getMutableManager() : nullptr;
        if ( pManager != nullptr )
        {
            CreatureTownViewComponentInternal::destroyViews( *pManager, _objectViews );
            CreatureTownViewComponentInternal::destroyViews( *pManager, _creatureViews );
        }
        _shownRevision = 0;
    }

    void CreatureTownViewComponent::updateView( GameObjectManager& manager, CreatureTownGameModeComponent& director )
    {
        dispatchInput( manager, director );
        const CreatureLifeCatalog* const pCatalog = director.getCatalog(); // 열린 판에는 있다
        if ( pCatalog != nullptr && pCatalog != _pValidatedCatalog.get() )
        {
            _pValidatedCatalog = director.getCatalogSnapshot();
            _bViewsRejected    = validateViews( *pCatalog ) ? SW_FALSE : SW_TRUE;
        }
        if ( _bViewsRejected == SW_TRUE || _shownRevision == director.getTownRevision() )
            return;
        syncTown( manager, director.getTown() );
        _shownRevision = director.getTownRevision();
    }

    void CreatureTownViewComponent::submitInput( const GameModeInput& input, CreatureTownGameModeComponent& director )
    {
        if ( input.wasActionTriggered( _talkAction ) )
            (void)director.submitTalk( _talkSpecies.key() ); // 거절한 까닭은 디렉터가 남긴다
    }

    bool CreatureTownViewComponent::validateViews( const CreatureLifeCatalog& catalog ) const
    {
        bool bValid = true;
        for ( const HabitatDef& habitat : catalog.getHabitats() )
        {
            for ( const HabitatCell& cell : habitat._listCell )
            {
                if ( cell._object.empty() || CreatureTownViewComponentInternal::findView( _listObjectView, cell._object ) != nullptr )
                    continue;
                SW_LOG_ERROR_ID( "CreatureTown.ObjectViewMissing", "Creature town view: object '%#' of habitat '%#' has no row in Object Views - the town is not shown", cell._object.c_str(), habitat._id.c_str() );
                bValid = false;
            }
        }
        for ( const CreatureSpeciesDef& species : catalog.getSpecies() )
        {
            if ( CreatureTownViewComponentInternal::findView( _listCreatureView, species._id ) != nullptr )
                continue;
            SW_LOG_ERROR_ID( "CreatureTown.CreatureViewMissing", "Creature town view: species '%#' has no row in Creature Views - the town is not shown", species._id.c_str() );
            bValid = false;
        }
        return bValid;
    }

    void CreatureTownViewComponent::syncTown( GameObjectManager& manager, const CreatureTown& town )
    {
        for ( int32 y = 0; y < town.getHeight(); ++y )
        {
            for ( int32 x = 0; x < town.getWidth(); ++x )
            {
                const hashed_string* const pObject = town.findObject( x, y );
                _objectViews.show( static_cast<size_t>( y * town.getWidth() + x ), pObject != nullptr ? *pObject : hashed_string{} );
            }
        }
        const vector<TownCreature>& listCreature = town.getCreatures();
        _creatureViews.beginCollect();
        for ( size_t creatureIndex = 0; creatureIndex < listCreature.size(); ++creatureIndex )
        {
            _creatureViews.show( creatureIndex, CreatureView{ listCreature[creatureIndex]._speciesID, listCreature[creatureIndex]._habitat } );
        }
        _creatureViews.endCollect();
        const auto destroy = [&manager]( GameObjectHandle& inoutHandle )
        {
            CreatureTownViewComponentInternal::destroyView( manager, inoutHandle );
            inoutHandle = GameObjectHandle{};
        };
        _objectViews.flush( destroy, [this, &manager, &town]( size_t tileIndex, const hashed_string& object )
        {
            const CreatureTownObjectView* const pRow = CreatureTownViewComponentInternal::findView( _listObjectView, object );
            if ( pRow == nullptr )
            {
                // 서식지 키에 없는 오브젝트(집 · 능력이 만든 것)가 놓였다 — 카탈로그 검사가 보지 못하는 줄이다.
                SW_LOG_ERROR_ID( "CreatureTown.PlacedObjectViewMissing", "Creature town view: object '%#' has no row in Object Views - it is not shown", object.c_str() );
                return GameObjectHandle{};
            }
            const int32 tile = static_cast<int32>( tileIndex );
            return spawnView( manager, pRow->_prefab,
                              float2{ static_cast<float32>( tile % town.getWidth() ) + CreatureTownViewComponentInternal::kHalfCell,
                                      static_cast<float32>( tile / town.getWidth() ) + CreatureTownViewComponentInternal::kHalfCell } );
        } );
        _creatureViews.flush( destroy, [this, &manager, &town]( size_t, const CreatureView& view )
        {
            const CreatureTownCreatureView* const pRow = CreatureTownViewComponentInternal::findView( _listCreatureView, view._speciesID ); // 검사했다 — 있다
            return pRow != nullptr ? spawnView( manager, pRow->_prefab, computeCreatureCenter( town, town.findHabitat( view._habitat ) ) ) : GameObjectHandle{};
        } );
    }

    float2 CreatureTownViewComponent::computeCreatureCenter( const CreatureTown& town, const HabitatInstance* pHabitat )
    {
        if ( pHabitat == nullptr || pHabitat->_listTile.empty() )
            return float2{ CreatureTownViewComponentInternal::kHalfCell, CreatureTownViewComponentInternal::kHalfCell };
        float2 center{ 0.0f, 0.0f };
        for ( const int32 tile : pHabitat->_listTile )
        {
            center._x += static_cast<float32>( tile % town.getWidth() ) + CreatureTownViewComponentInternal::kHalfCell;
            center._y += static_cast<float32>( tile / town.getWidth() ) + CreatureTownViewComponentInternal::kHalfCell;
        }
        const float32 count = static_cast<float32>( pHabitat->_listTile.size() );
        return float2{ center._x / count, center._y / count };
    }

    GameObjectHandle CreatureTownViewComponent::spawnView( GameObjectManager& manager, const AssetRef<PrefabAsset>& prefab, const float2& center ) const
    {
        AssetManager* const pAssets = game::findSubsystem<AssetManager>();
        GameObject* const   pView   = pAssets != nullptr && prefab.empty() == false ? pAssets->getAssetCache<PrefabCache>().spawn( &manager, prefab, SpawnLifetime::Derived, "CreatureTownView" ) : nullptr;
        if ( pView == nullptr )
            return GameObjectHandle{}; // 읽지 못한 까닭은 프리팹 캐시가 남겼다
        SceneComponent* const pRoot = pView->getComponent<SceneComponent>();
        if ( pRoot != nullptr )
            pRoot->setLocalPosition( manager.getWorldOrigin().toLocal( float3{ _townOrigin._x + center._x, _townOrigin._y, _townOrigin._z + center._y } ) ); // 마을 자리는 절대 자리다
        return pView->getHandle();
    }
} // namespace sw
