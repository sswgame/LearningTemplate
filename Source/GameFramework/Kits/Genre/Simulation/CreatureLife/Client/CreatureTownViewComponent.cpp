#include "pch.h"

#include "Core/Container/vector.h"
#include "Core/Foundation/Constants.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/Math.h"
#include "Core/String/Name/hashed_string.h"

#include "Engine/Asset/AssetManager.h"
#include "Engine/Asset/AssetRef.h"
#include "Engine/Foundation/GameObjectHandle.h"
#include "Engine/Foundation/WorldOrigin.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/Prefab/PrefabAsset.h"
#include "Engine/Object/Prefab/PrefabCache.h"
#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/Serialization/Reference/CatalogKey.h"

#include "GameFramework/Client/View/GameModeViewComponent.h"
#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Genre/Simulation/CreatureLife/Component/CreatureTownGameModeComponent.h"
#include "GameFramework/Runtime/Foundation/Presentation/SimulationViewSync.h"
#include "GameFramework/Runtime/Foundation/Save/StatefulComponentRegistrar.h"
#include "GameFramework/Runtime/Foundation/Service/GameSubsystems.h"

namespace sw
{
    class CreatureLifeCatalog;
    class CreatureTown;
    class CreatureTownGameModeComponent;
    class GameObjectManager;
    struct CreatureSpeciesDef;
    struct HabitatCell;
    struct HabitatInstance;

    /**
     * @struct CreatureTownObjectView
     * @brief 마을 오브젝트 뷰 표 한 줄: 서식지 키의 오브젝트(`<Habitat><Key object>`)와 그 모습 프리팹입니다(뷰의 PROPERTY, 씬 데이터).
     *        카탈로그의 오브젝트마다 한 줄이 있어야 모습을 세운다(`CreatureTownViewComponent::validateViews`). 생물 종 줄(`CreatureTownCreatureView`)과 형식을 나눈 까닭은
     *        가리키는 표가 달라서다 — 한 칸이 두 표를 가리키면 쿠커 · 인스펙터가 어느 표인지 모른다.
     */
    REFLECT()
    struct CreatureTownObjectView
    {
        REFLECT_BODY();
        PROPERTY( Tooltip = "Town object (the object of a habitat key in the creature catalog)" )
        CatalogKey<HabitatCell> _object{};
        PROPERTY( Tooltip = "Prefab spawned for it; its root scene component is placed on the town grid" )
        AssetRef<PrefabAsset> _prefab{};
    };

    /**
     * @struct CreatureTownCreatureView
     * @brief 마을 생물 뷰 표 한 줄 — 생물 종(`<Species id>`)과 그 모습 프리팹입니다. 카탈로그의 종마다 한 줄이 있어야 모습을 세운다.
     */
    REFLECT()
    struct CreatureTownCreatureView
    {
        REFLECT_BODY();
        PROPERTY( Tooltip = "Creature species id in the creature catalog" )
        CatalogKey<CreatureSpeciesDef> _species{};
        PROPERTY( Tooltip = "Prefab spawned for it; its root scene component is placed on the town grid" )
        AssetRef<PrefabAsset> _prefab{};
    };

    /**
     * @class CreatureTownViewComponent
     * @brief 셀의 오브젝트마다 프리팹 하나, 사는 생물마다 프리팹 하나를 세웁니다. 마을이 바뀐 때(`getTownRevision`)에 바뀐 슬롯만 지우고 다시 세운다.
     * @details 생물은 머무는 서식지가 차지한 셀들의 가운데에 선다(서식지가 사라졌으면 마을 모서리). `_talkAction` 을 누르면 `_talkSpecies` 와 대화한다.
     */
    REFLECT( Category = "CreatureLife", DisplayName = "Creature Town View",
             Tooltip = "Client side of a creature town: spawns the objects and creatures of the Creature Town Game Mode on the same object and sends the talk input to it" )
    class CreatureTownViewComponent : public GameModeViewComponent
    {
    public:
        REFLECT_BODY();

        static constexpr TickGroup kTickGroup = TickGroup::PostUpdate;

        CreatureTownViewComponent();
        virtual ~CreatureTownViewComponent() override;

        /** @brief 액션 이름 · 대화할 종이 비었으면 오류를 남깁니다(폴백 없음). 틱 그룹은 뷰의 `PostUpdate` 입니다. */
        void onBeginPlay() override;
        /** @brief 세운 모습을 걷습니다. */
        void onEndPlay() override;

    private:
        /** @brief 세운 모습을 모두 지웁니다(플레이 끝 · 다시 세울 때). 모습은 파생이라 상태를 실을 때 걷지 않는다(`SpawnLifetime::Derived`). */
        void despawnViews();
        /** @brief 생물 슬롯 하나가 지금 보이는 것 — 종과 머무는 서식지입니다. 서식지가 바뀌면 자리가 달라 다시 세운다. */
        struct CreatureView
        {
            hashed_string _speciesID{};
            int32         _habitat{ constant::invalid_index::kInt32 };

            bool operator==( const CreatureView& other ) const { return _speciesID == other._speciesID && _habitat == other._habitat; }
        };

    private:
        /** @brief 틱 뒤 게임 스레드: 대화 입력을 넘기고, 마을이 바뀌었으면 모습을 맞춥니다. */
        void updateView( GameObjectManager& manager, CreatureTownGameModeComponent& director );
        void submitInput( const GameModeInput& input, CreatureTownGameModeComponent& director );
        /** @brief 카탈로그의 서식지 오브젝트 · 종이 모두 뷰 표에 있는지 봅니다. 빠진 것마다 오류를 남기고 false 입니다(모습을 세우지 않는다). */
        [[nodiscard]] bool validateViews( const CreatureLifeCatalog& catalog ) const;
        /** @brief 지금 마을의 오브젝트 · 생물과 세운 모습을 견줘 바뀐 슬롯만 다시 세웁니다. */
        void syncTown( GameObjectManager& manager, const CreatureTown& town );
        /** @brief 생물이 설 자리(마을 격자 좌표)입니다. @p pHabitat 이 없으면 마을 모서리다. */
        [[nodiscard]] static float2 computeCreatureCenter( const CreatureTown& town, const HabitatInstance* pHabitat );
        /** @brief 프리팹을 마을 격자의 @p center 에 세웁니다. 프리팹을 읽지 못했거나 루트가 없으면 빈 핸들이거나 원점에 선 오브젝트의 핸들입니다. */
        [[nodiscard]] GameObjectHandle spawnView( GameObjectManager& manager, const AssetRef<PrefabAsset>& prefab, const float2& center ) const;

    private:
        PROPERTY( Category = "Prefabs", DisplayName = "Object Views", Tooltip = "Town object id -> prefab; every habitat key object of the creature catalog needs a row" )
        vector<CreatureTownObjectView> _listObjectView;
        PROPERTY( Category = "Prefabs", DisplayName = "Creature Views", Tooltip = "Species id -> prefab; every species of the creature catalog needs a row" )
        vector<CreatureTownCreatureView> _listCreatureView;
        PROPERTY( Category = "Layout", DisplayName = "Town Origin", Tooltip = "World position of the corner of town cell (0, 0)", Units = m )
        float3 _townOrigin;
        PROPERTY( Required, Category = "Input", DisplayName = "Talk Action", Tooltip = "InputMap action that talks to the creature (required)" )
        hashed_string _talkAction;
        PROPERTY( Required, Category = "Input", DisplayName = "Talk Species", Tooltip = "Creature the talk action talks to (required)" )
        CatalogKey<CreatureSpeciesDef> _talkSpecies;

        SW_TRANSIENT SimulationViewSync<hashed_string> _objectViews;       ///< 타일 번호(y × 너비 + x) → 오브젝트 id(빈 셀이면 빈 id)
        SW_TRANSIENT SimulationViewSync<CreatureView> _creatureViews;      ///< 온 순서 → 생물
        shared_ptr<const CreatureLifeCatalog>         _pValidatedCatalog;  ///< 뷰 표를 견준 카탈로그의 판 — 마을이 새 판에 묶이면 다시 견준다
        SW_TRANSIENT uint32                           _shownRevision;      ///< 맞춘 마을 판(0 이면 아직 — 열린 디렉터의 판은 1 부터다)
        SW_TRANSIENT uint8                            _bViewsRejected : 1; ///< 뷰 표에 빠진 줄이 있어 모습을 세우지 않는다
        [[maybe_unused]] uint8                        _reserved       : 7;
    };

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

#include "CreatureTownViewComponent.cpp.gen.inl"
