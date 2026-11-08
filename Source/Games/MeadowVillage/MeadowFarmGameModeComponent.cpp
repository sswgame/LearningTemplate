#include "pch.h"

#include "Games/MeadowVillage/MeadowFarmGameModeComponent.h"

#include "Core/GlobalVariable/GlobalVariableManager.h"

#include "Engine/Config/GameAutoplay.h"
#include "Engine/Input/Map/InputMap.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Rendering/MeshComponent.h"
#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/Format/StateIO.h"

#include "GameFramework/Kits/Genre/Simulation/Farming/CropCatalog.h"
#include "GameFramework/Runtime/Foundation/Framework/GameService.h"
#include "GameFramework/Runtime/Gameplay/SessionState/SessionStateComponent.h"

#include "Games/MeadowVillage/MeadowVillageData.h"

namespace sw
{
    namespace
    {
        /** @brief 칸 상태의 색입니다 — 다 자람 · 물 줌 · 갈았음 · 갈지 않음 순서로 먼저 맞는 것(값은 디렉터의 PROPERTY). */
        const float4& findTileColor( const FarmTile& tile, const float4& readyColor, const float4& wateredColor, const float4& tilledColor, const float4& grassColor )
        {
            if ( tile._bReady != SW_FALSE )
                return readyColor;
            if ( tile._bWatered != SW_FALSE )
                return wateredColor;
            return tile._bTilled != SW_FALSE ? tilledColor : grassColor;
        }
    } // namespace

    SW_TEST_GLOBAL_VARIABLE( int32, gv_meadowAutoPlay, 0, "MeadowVillage: 시계를 8 배로 흘려 날을 넘긴다 (1=켜기)" );
    SW_GAME_AUTOPLAY( gv_meadowAutoPlay, "MeadowVillage", "The village runs at 8x speed" );
} // namespace sw

namespace sw
{
    MeadowFarmGameModeComponent::MeadowFarmGameModeComponent()
        : _tilePrefab{}
        , _startingGold{ 20 }
        , _fastTimeScale{ 8.0f }
        , _readyColor{ 1.60f, 1.35f, 0.55f, 1.0f } // 메시 정점 색이 이미 흙색이라 상태 색은 그 위에 곱하는 틴트다
        , _wateredColor{ 0.60f, 0.58f, 0.62f, 1.0f }
        , _tilledColor{ 1.0f, 1.0f, 1.0f, 1.0f }
        , _grassColor{ 0.80f, 1.30f, 0.60f, 1.0f }
        , _tileSpacing{ 1.0f }
        , _field{}
        , _tintCache{}
        , _listTileView{}
        , _bViewsDirty{ SW_FALSE }
        , _reserved{ 0 }
    {
    }

    MeadowFarmGameModeComponent::~MeadowFarmGameModeComponent() = default;

    void MeadowFarmGameModeComponent::serializeState( StateIO& io )
    {
        io.header( kStateTag, kStateVersion );
        if ( io.isOk() == false )
            return;
        FarmField restoredField;
        if ( io.isReading() )
        {
            restoredField.initialize( kFieldWidth, kFieldHeight, game::findSubsystem<const CropCatalog>() );
        }
        FarmField& field = io.isWriting() ? _field : restoredField;
        io.section( FarmField::kStateTag, FarmField::kStateVersion, [&field]( StateIO& bodyIO )
        { bodyIO.value( field ); } );
        io.end();
        if ( io.isReading() && io.isOk() )
        {
            _field       = std::move( restoredField );
            _bViewsDirty = SW_TRUE;
        }
    }

    bool MeadowFarmGameModeComponent::startGame()
    {
        const CropCatalog* pCrops = game::findSubsystem<const CropCatalog>();
        if ( pCrops == nullptr )
        {
            SW_LOG_ERROR_ID( "MeadowFarm.FarmCannotStart", "[Meadow] the farm cannot start - the crop catalog is not loaded" );
            return false;
        }
        // 이 오브젝트의 첫 디렉터가 판을 연다. 시작 돈은 새 판일 때만(복원한 판 위에 덧쌓이지 않게).
        SessionStateInitResult initResult = SessionStateInitResult::Refused;
        SessionStateComponent* pState     = SessionStateComponent::openOnOwner( *this, MeadowVillageData::makeStateSettings(), initResult );
        if ( pState == nullptr )
            return false;
        if ( initResult == SessionStateInitResult::Fresh )
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

    void MeadowFarmGameModeComponent::onGameStarted()
    {
        SW_LOG_INFO_ID( "MeadowFarm.VillageOpenHoldSpace", "[Meadow] the village is open - hold Space to fast-forward, E to talk to a visitor" );
    }

    void MeadowFarmGameModeComponent::updateGame( float32 deltaTime )
    {
        (void)deltaTime;
        SessionStateComponent* pState = SessionStateComponent::findOnOwner( *this );
        if ( pState == nullptr )
            return;
        // 키는 입력 맵(`data/gamesettings.xml` 의 입력 맵 묶음)이 정한다. 시계를 미는 것은 밭 디렉터 하나다(마을은 읽기만).
        const bool bFastForward = isAutoPlayOn() || isActionDown( hashed_string( MeadowVillageData::kFastForwardAction ) );
        if ( pState->getClock().setTimeScale( bFastForward ? _fastTimeScale : 1.0f ) == false )
            return;
        for ( const WorldClockEvent& clockEvent : pState->getClockEvents() )
        {
            if ( clockEvent._kind == WorldClockEvent::Kind::DayChanged )
                startNextDay( *pState );
        }
    }

    void MeadowFarmGameModeComponent::startNextDay( SessionStateComponent& state )
    {
        const hashed_string season = state.getClock().getSeasonName(); // 계절은 공유 시계의 데이터
        _field.advanceDay( season, false );
        const CropCatalog* pCrops = game::findSubsystem<const CropCatalog>();
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
                int32         count           = 0;
                const bool    bHarvestedCrops = _field.harvest( x, y, produce, count ) == FarmActionResult::Done && 0 < count && pCrops != nullptr;
                if ( bHarvestedCrops )
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
            SW_LOG_INFO_ID( "MeadowFarm.DayHarvestSoldG", "[Meadow] day %# - harvest sold for %# G (wallet %# G)", state.getClock().getDay(), earned,
                            state.getWallet().getBalance( Wallet::getDefaultCurrency() ) );
        }
        _bViewsDirty = SW_TRUE;
    }

    void MeadowFarmGameModeComponent::replant( int32 x, int32 y, const hashed_string& season )
    {
        (void)_field.till( x, y );
        (void)_field.water( x, y );
        (void)_field.plant( x, y, "turnip_seed", season );
    }

    void MeadowFarmGameModeComponent::onFlush( GameObjectManager& manager, bool bRespawnViews )
    {
        if ( bRespawnViews == false && _bViewsDirty == SW_FALSE )
            return;
        destroySpawned( manager, _listTileView );
        spawnField( manager );
        _bViewsDirty = SW_FALSE;
    }

    void MeadowFarmGameModeComponent::onViewsDespawned()
    {
        _listTileView.clear();
    }

    void MeadowFarmGameModeComponent::spawnField( GameObjectManager& manager )
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
                pMesh->setLocalPosition( float3{ ( static_cast<float32>( x ) + 0.5f ) * _tileSpacing, 0.0f, ( static_cast<float32>( y ) + 0.5f ) * _tileSpacing } );
                _tintCache.apply( *pMesh, findTileColor( *pTile, _readyColor, _wateredColor, _tilledColor, _grassColor ) );
                _listTileView.push_back( pView->getHandle() );
            }
        }
    }
} // namespace sw
