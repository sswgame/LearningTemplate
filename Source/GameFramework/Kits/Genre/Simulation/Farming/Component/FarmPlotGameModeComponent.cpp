#include "pch.h"

#include "GameFramework/Kits/Genre/Simulation/Farming/Component/FarmPlotGameModeComponent.h"

#include "Engine/Asset/Cache/SharedAssetCache.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/Format/StateIO.h"

#include "GameFramework/Runtime/Gameplay/GameState/PlayerComponentUtil.h"
#include "GameFramework/Runtime/Gameplay/Inventory/WalletComponent.h"

namespace sw
{
    FarmPlotGameModeComponent::FarmPlotGameModeComponent()
        : _cropDataPath{}
        , _crop{}
        , _harvestedFlag{}
        , _stateSettings{}
        , _fieldWidth{ 4 }
        , _fieldHeight{ 2 }
        , _startingGold{ 20 }
        , _speedRange{}
        , _fastTimeScale{ 8.0f }
        , _pCropCatalog{}
        , _field{}
        , _speed{}
        , _fieldRevision{ 0 }
        , _bFastForward{ SW_FALSE }
        , _reserved{ 0 }
    {
    }

    FarmPlotGameModeComponent::~FarmPlotGameModeComponent() = default;

    void FarmPlotGameModeComponent::serializeGameState( StateIO& io )
    {
        io.header( kStateFormat );
        if ( io.isOk() == false )
            return;
        io.section( FarmField::kStateFormat, [this]( StateIO& bodyIO )
        { (void)bodyIO.valueOpened( _field, [this]( FarmField& field )
          {
              if ( _pCropCatalog == nullptr )
                  return false;
              field.initialize( _fieldWidth, _fieldHeight, *_pCropCatalog );
              return true;
          } ); } ); // 실패는 아카이브에 남는다
        io.end();
        if ( io.isReading() && io.isOk() )
            ++_fieldRevision;
    }

    bool FarmPlotGameModeComponent::startGame()
    {
        if ( acquireRequired( _cropDataPath, _pCropCatalog ) == false || _speed.initialize( _speedRange ) == false )
            return false; // 까닭은 남았다
        if ( _pCropCatalog->findCrop( _crop.key() ) == nullptr )
        {
            SW_LOG_ERROR_ID( "FarmPlot.CropUnknown", "[FarmPlot] the field cannot start - crop '%#' is not in the crop catalog", _crop.c_str() );
            return false;
        }
        if ( _harvestedFlag.empty() )
        {
            SW_LOG_ERROR_ID( "FarmPlot.NoHarvestedFlag", "[FarmPlot] the field cannot start - the harvested flag is not named" );
            return false;
        }
        // 이 오브젝트의 첫 디렉터가 판을 연다. 시작 돈은 새 판일 때만(복원한 판 위에 덧쌓이지 않게).
        GameStateComponent* pState = GameStateComponent::openOnOwner( *this, _stateSettings );
        if ( pState == nullptr )
            return false;
        const GameObject* const pPlayerState = requirePlayerState();
        WalletComponent* const  pWallet      = pPlayerState != nullptr ? PlayerComponentUtil::requireOn<WalletComponent>( *pPlayerState ) : nullptr;
        if ( pWallet == nullptr )
            return false; // 까닭은 찾기가 남겼다
        if ( pWallet->isFreshGame() )
            pWallet->getWallet().add( Wallet::getDefaultCurrency(), _startingGold );
        _field.initialize( _fieldWidth, _fieldHeight, *_pCropCatalog );
        const hashed_string season = pState->getClock().getSeasonName();
        for ( int32 y = 0; y < _fieldHeight; ++y )
        {
            for ( int32 x = 0; x < _fieldWidth; ++x )
            {
                replant( x, y, season );
            }
        }
        _bFastForward = SW_FALSE;
        ++_fieldRevision;
        return true;
    }

    void FarmPlotGameModeComponent::onGameStarted()
    {
        SW_LOG_INFO_ID( "FarmPlot.Open", "[FarmPlot] the field is open - hold fast forward to speed up the clock" );
    }

    bool FarmPlotGameModeComponent::submitFastForward( bool bHeld )
    {
        if ( isStarted() == false )
            return false;
        _bFastForward = bHeld ? SW_TRUE : SW_FALSE;
        return true;
    }

    void FarmPlotGameModeComponent::collectMinigameResult( vector<MinigameResultValue>& outListValue ) const
    {
        const GameObject* const      pPlayerState = findPlayerState();
        const WalletComponent* const pWallet      = pPlayerState != nullptr ? pPlayerState->getComponent<WalletComponent>() : nullptr;
        if ( pWallet != nullptr )
            addMoneyResult( outListValue, pWallet->getWallet().getBalance( Wallet::getDefaultCurrency() ) - _startingGold );
    }

    void FarmPlotGameModeComponent::updateGame( float32 deltaTime )
    {
        (void)deltaTime;
        GameStateComponent* pState  = GameStateComponent::findOnOwner( *this );
        WalletComponent*    pWallet = findOnPlayerState<WalletComponent>();
        if ( pState == nullptr || pWallet == nullptr )
            return;
        // 시계를 미는 것은 이 디렉터 하나다(뒤에 붙은 디렉터는 읽기만).
        const bool bFastForward = isAutoPlayOn() || _bFastForward == SW_TRUE;
        if ( _speed.setTimeScale( bFastForward ? _fastTimeScale : 1.0f ) == false || pState->getClock().setTimeScale( _speed.getTimeScale() ) == false )
            return; // 배속은 디렉터의 GameSpeedControl 이 쥐고 공유 시계가 받는다(감사 K-10)
        for ( const WorldClockEvent& clockEvent : pState->getClockEvents() )
        {
            if ( clockEvent._kind == WorldClockEvent::Kind::DayChanged )
                startNextDay( *pState, *pWallet );
        }
    }

    void FarmPlotGameModeComponent::startNextDay( GameStateComponent& state, WalletComponent& wallet )
    {
        const hashed_string season = state.getClock().getSeasonName(); // 계절은 공유 시계의 데이터
        _field.advanceDay( season, false );
        int32 earned = 0;
        for ( int32 y = 0; y < _fieldHeight; ++y )
        {
            for ( int32 x = 0; x < _fieldWidth; ++x )
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
                    (void)state.getFlags().addFlag( _harvestedFlag, count );
                }
                replant( x, y, season );
            }
        }
        if ( 0 < earned )
        {
            wallet.getWallet().add( Wallet::getDefaultCurrency(), earned );
            SW_LOG_INFO_ID( "FarmPlot.DayHarvestSold", "[FarmPlot] day %# - harvest sold for %# G (wallet %# G)", state.getClock().getDay(), earned,
                            wallet.getWallet().getBalance( Wallet::getDefaultCurrency() ) );
        }
        ++_fieldRevision;
    }

    void FarmPlotGameModeComponent::replant( int32 x, int32 y, const hashed_string& season )
    {
        (void)_field.till( x, y );
        (void)_field.water( x, y );
        const CropDef* const pCrop = _pCropCatalog->findCrop( _crop.key() ); // 판을 열 때 보았다 — 다시 읽은 카탈로그가 지웠으면 심지 않는다
        if ( pCrop != nullptr )
            (void)_field.plant( x, y, pCrop->_seedItem, season );
    }
} // namespace sw
