#include "pch.h"

#include "Games/MeadowVillage/MeadowFarmGameModeComponent.h"

#include "Engine/Asset/Cache/SharedAssetCache.h"
#include "Engine/Input/Map/CommonInputActions.h"
#include "Engine/Input/Map/InputMap.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/Format/StateIO.h"
#include "Engine/Visual/Component/MeshComponent.h"

#include "GameFramework/Runtime/Gameplay/GameState/PlayerComponentUtil.h"
#include "GameFramework/Runtime/Gameplay/Inventory/WalletComponent.h"

namespace sw
{
    namespace
    {
        /** @brief 밭 칸의 모습입니다(뷰 동기화의 규칙 id — 바뀐 칸만 다시 칠한다). 0 은 "칸 없음" 이다. */
        enum TileLook : uint8
        {
            kTileLookNone = 0,
            kTileLookGrass,
            kTileLookTilled,
            kTileLookWatered,
            kTileLookReady,
        };

        uint8 findTileLook( const FarmTile& tile )
        {
            if ( tile._bReady != SW_FALSE )
                return kTileLookReady;
            if ( tile._bWatered != SW_FALSE )
                return kTileLookWatered;
            return tile._bTilled != SW_FALSE ? kTileLookTilled : kTileLookGrass;
        }
    } // namespace
} // namespace sw

namespace sw
{
    MeadowFarmGameModeComponent::MeadowFarmGameModeComponent()
        : _cropDataPath{}
        , _crop{}
        , _stateSettings{}
        , _tilePrefab{}
        , _startingGold{ 20 }
        , _fastTimeScale{ 8.0f }
        , _readyColor{ 1.60f, 1.35f, 0.55f, 1.0f } // 메시 정점 색이 이미 흙색이라 상태 색은 그 위에 곱하는 틴트다
        , _wateredColor{ 0.60f, 0.58f, 0.62f, 1.0f }
        , _tilledColor{ 1.0f, 1.0f, 1.0f, 1.0f }
        , _grassColor{ 0.80f, 1.30f, 0.60f, 1.0f }
        , _tileSpacing{ 1.0f }
        , _pCropCatalog{}
        , _field{}
        , _tintCache{}
        , _tileViews{}
        , _speed{}
        , _bViewsDirty{ SW_FALSE }
        , _reserved{ 0 }
    {
    }

    MeadowFarmGameModeComponent::~MeadowFarmGameModeComponent() = default;

    void MeadowFarmGameModeComponent::serializeGameState( StateIO& io )
    {
        io.header( kStateTag, kStateVersion );
        if ( io.isOk() == false )
            return;
        FarmField restoredField;
        if ( io.isReading() )
        {
            restoredField.initialize( kFieldWidth, kFieldHeight, _pCropCatalog.get() );
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
        if ( acquireRequired( _cropDataPath, _pCropCatalog ) == false )
            return false;
        if ( _pCropCatalog->findCrop( _crop.key() ) == nullptr )
        {
            SW_LOG_ERROR_ID( "MeadowFarm.CropUnknown", "[Meadow] the farm cannot start - crop '%#' is not in the crop catalog", _crop.c_str() );
            return false;
        }
        // 이 오브젝트의 첫 디렉터가 판을 연다. 시작 돈은 새 판일 때만(복원한 판 위에 덧쌓이지 않게).
        GameStateComponent* pState = GameStateComponent::openOnOwner( *this, _stateSettings );
        if ( pState == nullptr )
            return false;
        WalletComponent* pWallet = PlayerComponentUtil::requireOnOwner<WalletComponent>( *this );
        if ( pWallet == nullptr )
            return false;
        if ( pWallet->isFreshGame() )
            pWallet->getWallet().add( Wallet::getDefaultCurrency(), _startingGold );
        _field.initialize( kFieldWidth, kFieldHeight, _pCropCatalog.get() );
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
        SW_LOG_INFO_ID( "MeadowFarm.VillageOpenHoldSpace", "[Meadow] the village is open - hold fast forward to speed up the clock, use the talk action to talk to a visitor" );
    }

    void MeadowFarmGameModeComponent::updateGame( float32 deltaTime )
    {
        (void)deltaTime;
        GameStateComponent* pState  = GameStateComponent::findOnOwner( *this );
        WalletComponent*    pWallet = PlayerComponentUtil::findOnOwner<WalletComponent>( *this );
        if ( pState == nullptr || pWallet == nullptr )
            return;
        // 키는 입력 맵(`data/gamesettings.xml` 의 입력 맵 묶음)이 정한다. 시계를 미는 것은 밭 디렉터 하나다(마을은 읽기만).
        const bool bFastForward = isAutoPlayOn() || isActionDown( hashed_string( CommonInputActions::kFastForward ) );
        if ( _speed.setTimeScale( bFastForward ? _fastTimeScale : 1.0f ) == false || pState->getClock().setTimeScale( _speed.getTimeScale() ) == false )
            return; // 배속은 디렉터의 GameSpeedControl 이 쥐고 공유 시계가 받는다(감사 K-10)
        for ( const WorldClockEvent& clockEvent : pState->getClockEvents() )
        {
            if ( clockEvent._kind == WorldClockEvent::Kind::DayChanged )
                startNextDay( *pState, *pWallet );
        }
    }

    void MeadowFarmGameModeComponent::startNextDay( GameStateComponent& state, WalletComponent& wallet )
    {
        const hashed_string season = state.getClock().getSeasonName(); // 계절은 공유 시계의 데이터
        _field.advanceDay( season, false );
        int32 earned = 0;
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
                const bool    bHarvestedCrops = _field.harvest( x, y, produce, count ) == FarmActionResult::Done && 0 < count;
                if ( bHarvestedCrops )
                {
                    earned += _pCropCatalog->findSellPrice( produce ) * count;
                    (void)state.getFlags().addFlag( hashed_string( kHarvestedFlag ), count );
                }
                replant( x, y, season );
            }
        }
        if ( 0 < earned )
        {
            wallet.getWallet().add( Wallet::getDefaultCurrency(), earned );
            SW_LOG_INFO_ID( "MeadowFarm.DayHarvestSoldG", "[Meadow] day %# - harvest sold for %# G (wallet %# G)", state.getClock().getDay(), earned,
                            wallet.getWallet().getBalance( Wallet::getDefaultCurrency() ) );
        }
        _bViewsDirty = SW_TRUE;
    }

    void MeadowFarmGameModeComponent::replant( int32 x, int32 y, const hashed_string& season )
    {
        (void)_field.till( x, y );
        (void)_field.water( x, y );
        const CropDef* const pCrop = _pCropCatalog->findCrop( _crop.key() ); // 판을 열 때 보았다 — 다시 읽은 카탈로그가 지웠으면 심지 않는다
        if ( pCrop != nullptr )
            (void)_field.plant( x, y, pCrop->_seedItem, season );
    }

    void MeadowFarmGameModeComponent::onFlush( GameObjectManager& manager, bool bRespawnViews )
    {
        if ( bRespawnViews == false && _bViewsDirty == SW_FALSE )
            return;
        // 모습이 바뀐 칸만 지우고 다시 스폰한다(예전에는 한 칸만 바뀌어도 밭 전체를 다시 만들었다, 감사 K-10). 처음이거나 정리한 뒤면 슬롯이 비어 모두 쌓인다.
        if ( bRespawnViews )
            _tileViews.clear();
        collectFieldViews();
        _tileViews.flush( [&manager]( GameObjectHandle& inoutHandle )
        { destroySpawned( manager, inoutHandle ); },
                          [this, &manager]( size_t tileIndex, uint8 look )
        { return spawnTileView( manager, tileIndex, look ); } );
        _bViewsDirty = SW_FALSE;
    }

    void MeadowFarmGameModeComponent::onViewsDespawned()
    {
        _tileViews.clear();
    }

    void MeadowFarmGameModeComponent::collectFieldViews()
    {
        for ( int32 y = 0; y < kFieldHeight; ++y )
        {
            for ( int32 x = 0; x < kFieldWidth; ++x )
            {
                const FarmTile* pTile = _field.findTile( x, y );
                _tileViews.show( static_cast<size_t>( y * kFieldWidth + x ), pTile != nullptr ? findTileLook( *pTile ) : uint8{ kTileLookNone } );
            }
        }
    }

    GameObjectHandle MeadowFarmGameModeComponent::spawnTileView( GameObjectManager& manager, size_t tileIndex, uint8 look )
    {
        const int32    x     = static_cast<int32>( tileIndex ) % kFieldWidth;
        const int32    y     = static_cast<int32>( tileIndex ) / kFieldWidth;
        GameObject*    pView = spawnPrefab( manager, _tilePrefab, "MeadowFarmTile" );
        MeshComponent* pMesh = pView != nullptr ? pView->getComponent<MeshComponent>() : nullptr;
        if ( pMesh == nullptr )
            return pView != nullptr ? pView->getHandle() : GameObjectHandle{};
        const float4& color = look == kTileLookReady ? _readyColor : look == kTileLookWatered ? _wateredColor
                                                               : look == kTileLookTilled      ? _tilledColor
                                                                                              : _grassColor;
        pMesh->setLocalPosition( float3{ ( static_cast<float32>( x ) + 0.5f ) * _tileSpacing, 0.0f, ( static_cast<float32>( y ) + 0.5f ) * _tileSpacing } );
        _tintCache.apply( *pMesh, color );
        return pView->getHandle();
    }
} // namespace sw
