/**
 * @file GameModeTestComponent.h
 * @brief 디렉터 열기 테스트(`TestGameModeComponent.cpp`)가 다는 디렉터입니다 — 개발 구성만 코드젠하고 "Add Component" 메뉴에 나오지 않는다.
 * @details 앞 판의 값을 되돌리지 않는 모양(더하기만, 조건이 맞을 때만 적기)으로 일부러 적는다 — 되돌림은 기반(`GameModeComponent::openGame`)이 한다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Encoding/FourCCUtil.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/Serialization/Format/StateIO.h"

#include "GameFramework/Runtime/Foundation/GameMode/BoundAssetRef.h"
#include "GameFramework/Runtime/Foundation/GameMode/GameModeComponent.h"
#include "GameFramework/Runtime/Foundation/Presentation/RuleViewCatalog.h"

namespace sw
{
    /**
     * @brief 판을 열 때 라운드를 하나 더하고, `_bWins` 면 우승자 · 점수 · 보너스를 적는 디렉터입니다.
     * @details `_viewPath` 를 적으면 뷰 표를 받아, 키트 규칙 객체처럼 날 포인터(`_pBound`)와 항목(`_pView`)을 빌린다 — 읽기가 다시 묶는다.
     */
    REFLECT( HideInMenu )
    struct GameModeTestComponent : public GameModeComponent
    {
        REFLECT_BODY();

        GameModeTestComponent()
            : _roundCount{ 0 }
            , _winner{ -1 }
            , _listScore{}
            , _handValue{ 0 }
            , _viewPath{}
            , _pViews{}
            , _pBound{ nullptr }
            , _pView{ nullptr }
            , _restoredCount{ 0 }
            , _refusedCount{ 0 }
            , _bBonus{ SW_FALSE }
            , _bWins{ SW_TRUE }
            , _reserved{ 0 }
        {
        }

        PROPERTY( RuntimeState )
        int32 _roundCount;
        PROPERTY( RuntimeState )
        int32 _winner; ///< 우승자 번호(-1 없음)
        PROPERTY( RuntimeState )
        vector<int32>                  _listScore;
        int32                          _handValue; ///< 손 본문이 멤버에 바로 읽는 값 — 음수는 읽은 뒤 거절한다
        string                         _viewPath;
        BoundAssetRef<RuleViewCatalog> _pViews;
        const RuleViewCatalog*         _pBound; ///< 규칙 객체가 빌린 판
        const RuleViewDef*             _pView;  ///< 규칙 객체가 빌린 항목
        uint32                         _restoredCount;
        uint32                         _refusedCount;
        PROPERTY( RuntimeState )
        uint8                  _bBonus   : 1;
        uint8                  _bWins    : 1; ///< 다음 판이 우승자를 적는다 — 상태 칸이 아니라 되돌리지 않는다
        [[maybe_unused]] uint8 _reserved : 6;

        /** @brief 판을 열 때 부르는 플레이어 상태 찾기입니다. */
        const GameObject* requirePlayerStateForTest() const { return requirePlayerState(); }

    protected:
        bool startGame() override
        {
            if ( _viewPath.empty() == false )
            {
                if ( acquireRequired( _viewPath, _pViews ) == false )
                    return false;
                _pBound = _pViews.get();
                _pView  = _pBound->find( hashed_string( "marine" ) );
            }
            ++_roundCount;
            if ( _bWins == SW_TRUE )
            {
                _winner = 1;
                _bBonus = SW_TRUE;
                _listScore.push_back( 10 );
            }
            return true;
        }
        void serializeGameState( StateIO& io ) override
        {
            io.header( FourCCUtil::make( "TGMC" ), 1 );
            io.value( _handValue );
            io.check( _handValue >= 0 );
            io.end();
            if ( io.isWriting() || io.isOk() == false || _viewPath.empty() )
                return;
            const RuleViewDef* const pView = _pViews->find( hashed_string( "marine" ) );
            if ( io.check( pView != nullptr ) == false )
                return;
            _pBound = _pViews.get();
            _pView  = pView;
        }
        void onStateRestored( bool bRestored ) override { ++( bRestored ? _restoredCount : _refusedCount ); }
        void updateGame( float32 deltaTime ) override { (void)deltaTime; }
        void onFlush( GameObjectManager& manager, bool bRespawnViews ) override
        {
            (void)manager;
            (void)bRespawnViews;
        }
    };
} // namespace sw
