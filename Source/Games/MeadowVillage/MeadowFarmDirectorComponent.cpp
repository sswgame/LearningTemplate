#include "pch.h"

#include "Games/MeadowVillage/MeadowFarmDirectorComponent.h"

#include "Core/GlobalVariable/GlobalVariableManager.h"

#include "Engine/Input/InputManager.h"
#include "Engine/Input/InputMap.h"
#include "Engine/Object/Component/3D/MeshComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Utility/GameAutoplay.h"

#include "GameFramework/Base/Foundation/Framework/GameService.h"
#include "GameFramework/Base/Foundation/Utility/StateArchiveUtil.h"
#include "GameFramework/Base/Gameplay/GameState/GameStateComponent.h"
#include "GameFramework/Kits/Genre/Simulation/Farming/CropCatalog.h"

#include "Games/MeadowVillage/MeadowVillageData.h"

namespace sw
{
    SW_LOG_CALLER( "MeadowFarm" );

    namespace
    {
        struct MeadowFarmDirectorComponentInternal
        {
            /** @brief 칸 상태의 색 — 다 자람 노랑 · 물 줌 짙은 흙 · 갈았음 흙 · 갈지 않음 풀. */
            static float4 findTileColor( const FarmTile& tile )
            {
                if ( tile._bReady != SW_FALSE )
                    return float4{ 0.95f, 0.85f, 0.30f, 1.0f };
                if ( tile._bWatered != SW_FALSE )
                    return float4{ 0.33f, 0.22f, 0.13f, 1.0f };
                return tile._bTilled != SW_FALSE ? float4{ 0.58f, 0.40f, 0.24f, 1.0f } : float4{ 0.40f, 0.62f, 0.28f, 1.0f };
            }
        };
    } // namespace

    SW_TEST_GLOBAL_VARIABLE_SHIPPED( int32, gv_meadowAutoPlay, 0, "MeadowVillage: 시계를 8 배로 흘려 날을 넘긴다 (1=켜기)" );
    SW_GAME_AUTOPLAY( gv_meadowAutoPlay, "MeadowVillage", "The village runs at 8x speed" );
} // namespace sw

namespace sw
{
    MeadowFarmDirectorComponent::MeadowFarmDirectorComponent()
        : _tilePrefab{ "game/meadowvillage/prefabs/tile.prefab.xml" }
        , _startingGold{ 20 }
        , _fastTimeScale{ 8.0f }
        , _field{}
        , _tintCache{}
        , _listTileView{}
        , _bViewsDirty{ SW_FALSE }
        , _reserved{ 0 }
    {
    }

    MeadowFarmDirectorComponent::~MeadowFarmDirectorComponent() = default;

    void MeadowFarmDirectorComponent::writeState( Archive& outArchive ) const
    {
        StateArchiveUtil::writeHeader( outArchive, kStateTag, kStateVersion );
        Archive body;
        _field.writeState( body );
        StateArchiveUtil::writeSection( outArchive, FarmField::kStateTag, FarmField::kStateVersion, body );
    }

    bool MeadowFarmDirectorComponent::startGame()
    {
        GameStateComponent* pState = GameStateComponent::findOnOwner( *this );
        const CropCatalog*  pCrops = game::getService<CropCatalog>();
        if ( pState == nullptr || pCrops == nullptr )
        {
            SW_LOG_WARNING( "[Meadow] the farm cannot start - %#", pState == nullptr ? "no GameStateComponent in front of it on this object" : "the crop catalog is not loaded" );
            return false;
        }
        // 이 오브젝트의 첫 디렉터가 판을 연다. 시작 돈은 새 판일 때만(되살린 판 위에 덧쌓이지 않게).
        if ( pState->initialize( MeadowVillageData::makeStateSettings() ) == GameStateInitResult::Fresh )
            pState->getWallet().add( Wallet::getDefaultCurrency(), _startingGold );
        _field.initialize( kFieldWidth, kFieldHeight, pCrops );
        const hashed_string season = pState->getClock().getSeasonName();
        for ( int32 y = 0; y < kFieldHeight; ++y )
        {
            for ( int32 x = 0; x < kFieldWidth; ++x )
            {
                replant( x, y, season );
            }
        }
        _bViewsDirty = SW_TRUE;
        return true;
    }

    bool MeadowFarmDirectorComponent::readState( Archive& archive )
    {
        uint32     tag     = 0;
        uint32     version = 0;
        Archive    body;
        const bool bFrameRead = StateArchiveUtil::readHeader( archive, kStateTag, kStateVersion ) && StateArchiveUtil::readSection( archive, tag, version, body );
        if ( bFrameRead == false || tag != FarmField::kStateTag || version != FarmField::kStateVersion )
            return false;
        FarmField field;
        field.initialize( kFieldWidth, kFieldHeight, game::getService<CropCatalog>() );
        const bool bRead = field.readState( body ) && body.getRemainingBytes() == 0 && archive.getRemainingBytes() == 0;
        if ( bRead == false )
            return false;
        _field       = std::move( field );
        _bViewsDirty = SW_TRUE;
        return true;
    }

    void MeadowFarmDirectorComponent::onGameStarted()
    {
        SW_LOG_INFO( "[Meadow] the village is open - hold Space to fast-forward, E to talk to a visitor" );
    }

    void MeadowFarmDirectorComponent::tickGame( float32 deltaTime )
    {
        (void)deltaTime;
        GameStateComponent* pState = GameStateComponent::findOnOwner( *this );
        if ( pState == nullptr )
            return;
        // 키는 입력 맵(`data/meadow.input.xml`)이 정한다. 시계를 미는 것은 밭 디렉터 하나다(마을은 읽기만).
        const InputManager* pInput       = game::getService<InputManager>();
        const bool          bFastForward = isAutoPlayOn() ||
                                  ( pInput != nullptr && pInput->getInputMap().isActionDown( hashed_string( MeadowVillageData::kFastForwardAction ) ) );
        pState->getClock().setTimeScale( bFastForward ? _fastTimeScale : 1.0f );
        for ( const WorldClockEvent& clockEvent : pState->getClockEvents() )
        {
            if ( clockEvent._kind == WorldClockEvent::Kind::DayChanged )
                startNextDay( *pState );
        }
    }

    void MeadowFarmDirectorComponent::startNextDay( GameStateComponent& state )
    {
        const hashed_string season = state.getClock().getSeasonName(); // 계절은 공유 시계의 데이터
        _field.advanceDay( season, false );
        const CropCatalog* pCrops = game::getService<CropCatalog>();
        int32              earned = 0;
        for ( int32 y = 0; y < kFieldHeight; ++y )
        {
            for ( int32 x = 0; x < kFieldWidth; ++x )
            {
                const FarmTile* pTile = _field.findTile( x, y );
                if ( pTile == nullptr || pTile->_bReady == SW_FALSE )
                {
                    (void)_field.water( x, y );
                    continue;
                }
                hashed_string produce;
                int32         count = 0;
                if ( _field.harvest( x, y, produce, count ) == FarmActionResult::Done && 0 < count && pCrops != nullptr )
                {
                    earned += pCrops->findSellPrice( produce ) * count;
                    (void)state.getFlags().addFlag( hashed_string( MeadowVillageData::kHarvestedFlag ), count );
                }
                replant( x, y, season );
            }
        }
        if ( 0 < earned )
        {
            state.getWallet().add( Wallet::getDefaultCurrency(), earned );
            SW_LOG_INFO( "[Meadow] day %# - harvest sold for %# G (wallet %# G)", state.getClock().getDay(), earned,
                         state.getWallet().getBalance( Wallet::getDefaultCurrency() ) );
        }
        _bViewsDirty = SW_TRUE;
    }

    void MeadowFarmDirectorComponent::replant( int32 x, int32 y, const hashed_string& season )
    {
        (void)_field.till( x, y );
        (void)_field.water( x, y );
        (void)_field.plant( x, y, "turnip_seed", season );
    }

    void MeadowFarmDirectorComponent::onFlush( GameObjectManager& manager, bool bRespawnViews )
    {
        if ( bRespawnViews == false && _bViewsDirty == SW_FALSE )
            return;
        for ( GameObjectHandle& handle : _listTileView )
        {
            destroySpawned( manager, handle );
        }
        _listTileView.clear();
        spawnField( manager );
        _bViewsDirty = SW_FALSE;
    }

    void MeadowFarmDirectorComponent::onViewsDespawned()
    {
        _listTileView.clear();
    }

    void MeadowFarmDirectorComponent::spawnField( GameObjectManager& manager )
    {
        for ( int32 y = 0; y < kFieldHeight; ++y )
        {
            for ( int32 x = 0; x < kFieldWidth; ++x )
            {
                const FarmTile* pTile = _field.findTile( x, y );
                GameObject*     pView = pTile != nullptr ? spawnPrefab( manager, _tilePrefab, "MeadowFarmTile" ) : nullptr;
                MeshComponent*  pMesh = pView != nullptr ? pView->getComponent<MeshComponent>() : nullptr;
                if ( pMesh == nullptr )
                    continue;
                pMesh->setLocalPosition( float3{ ( static_cast<float32>( x ) + 0.5f ) * kTileSpacing, 0.05f, ( static_cast<float32>( y ) + 0.5f ) * kTileSpacing } );
                _tintCache.apply( *pMesh, MeadowFarmDirectorComponentInternal::findTileColor( *pTile ) );
                _listTileView.push_back( pView->getHandle() );
            }
        }
    }
} // namespace sw
