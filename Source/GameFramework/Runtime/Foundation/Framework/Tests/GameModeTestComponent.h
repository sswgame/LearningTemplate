/**
 * @file GameModeTestComponent.h
 * @brief 디렉터 열기 테스트(`TestGameModeComponent.cpp`)가 다는 디렉터입니다 — 개발 구성만 코드젠하고 "Add Component" 메뉴에 나오지 않는다.
 * @details 앞 판의 값을 되돌리지 않는 모양(더하기만, 조건이 맞을 때만 적기)으로 일부러 적는다 — 되돌림은 기반(`GameModeComponent::openGame`)이 한다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Encoding/FourCCUtil.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/Serialization/Format/StateIO.h"

#include "GameFramework/Runtime/Foundation/Framework/GameModeComponent.h"

namespace sw
{
    /** @brief 판을 열 때 라운드를 하나 더하고, `_bWins` 면 우승자 · 점수 · 보너스를 적는 디렉터입니다. */
    REFLECT( HideInMenu )
    struct GameModeTestComponent : public GameModeComponent
    {
        REFLECT_BODY();

        GameModeTestComponent()
            : _roundCount{ 0 }
            , _winner{ -1 }
            , _listScore{}
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
        vector<int32> _listScore;
        PROPERTY( RuntimeState )
        uint8                  _bBonus   : 1;
        uint8                  _bWins    : 1; ///< 다음 판이 우승자를 적는다 — 상태 칸이 아니라 되돌리지 않는다
        [[maybe_unused]] uint8 _reserved : 6;

    protected:
        bool startGame() override
        {
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
            io.end();
        }
        void updateGame( float32 deltaTime ) override { (void)deltaTime; }
        void onFlush( GameObjectManager& manager, bool bRespawnViews ) override
        {
            (void)manager;
            (void)bRespawnViews;
        }
    };
} // namespace sw
