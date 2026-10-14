#include "pch.h"

#include "Engine/Utility/GameAutoplay.h"

#include "Core/Console/DevCommandRegistry.h"
#include "Core/Container/StringUtil.h"
#include "Core/Container/vector.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Log/Logger.h"
#include "Core/String/RegistrationList.h"

#include "Engine/Common/EngineServices.h"

namespace sw
{
    SW_LOG_CALLER( "GameAutoplay" );

    namespace
    {
        struct GameAutoplayInternal
        {
            /**
             * @brief 등록된 자동 플레이(등록 순서 · 이름은 전역 변수 이름). Engine 이미지의 함수 정적이라 게임 모듈이 바뀌어도 남는다.
             *        핫 리로드가 올리는 교체 이미지의 같은 이름은 대체한다(`RegistrationReloadScope`).
             */
            static RegistrationList<const GameAutoplayRegistration>& getRegistrations()
            {
                static RegistrationList<const GameAutoplayRegistration> s_registration{ "GameAutoplayRegistry", RegistrationOrder::Insertion, true };
                return s_registration;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    void GameAutoplay::registerAutoplay( const GameAutoplayRegistration* pRegistration )
    {
        const bool bInvalidAutoplayRegistration = pRegistration == nullptr || pRegistration->_pValue == nullptr || pRegistration->_pVariableName == nullptr;
        if ( bInvalidAutoplayRegistration )
            return;
        const RegistrationResult result = GameAutoplayInternal::getRegistrations().add( pRegistration, pRegistration->_pVariableName );
        if ( result == RegistrationResult::DuplicateName )
            SW_LOG_ERROR( "Game autoplay '%#' is registered twice - the second one is ignored", pRegistration->_pVariableName );
    }

    void GameAutoplay::unregisterAutoplay( const GameAutoplayRegistration* pRegistration )
    {
        RegistrationList<const GameAutoplayRegistration>& registration = GameAutoplayInternal::getRegistrations();
        if ( registration.remove( pRegistration ) == false )
            return;
        // 게임 모듈이 내려가 비었으면 저장소를 놓는다 — 게임이 키운 용량이 EngineMisc 태그에 남지 않게.
        if ( registration.getCount() == 0 )
            registration.shrinkToFit();
    }

    const GameAutoplayRegistration* GameAutoplay::findActive()
    {
        const RegistrationList<const GameAutoplayRegistration>& registration = GameAutoplayInternal::getRegistrations();
        const uint32                                            count        = registration.getCount();
        return count == 0 ? nullptr : registration.getAt( count - 1 );
    }

    bool GameAutoplay::isOn()
    {
        const GameAutoplayRegistration* pActive = findActive();
        return pActive != nullptr && *pActive->_pValue != 0;
    }

    bool GameAutoplay::setOn( bool bOn )
    {
        const GameAutoplayRegistration* pActive = findActive();
        if ( pActive == nullptr )
            return false;
        // 표를 거쳐 써야 변경 콜백이 불리고 전역 변수 패널이 같은 값을 본다(C++ 대입은 콜백을 부르지 않는다).
        GlobalVariableInfo* pInfo = engine::areEngineServicesBound() ? engine::getGlobalVariableManager().findVariable( pActive->_pVariableName ) : nullptr;
        if ( pInfo != nullptr && pInfo->_pData == pActive->_pValue )
            (void)pInfo->setValueAsInt( bOn ? 1 : 0 ); // 타입이 int32 라 실패하지 않는다
        else
            *pActive->_pValue = bOn ? 1 : 0;
        return true;
    }

    GameAutoplayRegistrar::GameAutoplayRegistrar( const GameAutoplayRegistration* pRegistration )
        : _pRegistration{ pRegistration }
    {
        GameAutoplay::registerAutoplay( pRegistration );
    }

    GameAutoplayRegistrar::~GameAutoplayRegistrar()
    {
        GameAutoplay::unregisterAutoplay( _pRegistration );
    }
} // namespace sw

#if SW_DEV_COMMANDS_ENABLED
namespace sw
{
    namespace
    {
        struct GameAutoplayDevCommandsInternal
        {
            static bool runAutoplay( const vector<string>& listArgument, string& outReply )
            {
                const GameAutoplayRegistration* pActive = GameAutoplay::findActive();
                if ( pActive == nullptr )
                {
                    outReply = "this game has no autoplay (SW_GAME_AUTOPLAY)";
                    return false;
                }
                if ( listArgument.size() > 1 )
                    return false;
                if ( listArgument.size() == 1 )
                {
                    bool bOn{ false };
                    if ( StringUtil::tryParseBool( listArgument[0], bOn ) == false )
                        return false;
                    (void)GameAutoplay::setOn( bOn ); // 위에서 등록을 확인했다
                }
                outReply = string( pActive->_pGameName ) + " autoplay " + ( GameAutoplay::isOn() ? "on" : "off" ) + " (" + pActive->_pVariableName + ")";
                return true;
            }
        };
    } // namespace

    SW_DEV_COMMAND( Autoplay, "autoplay", "autoplay [on|off]", "Show or switch the game's autoplay (AI drives the player - SW_GAME_AUTOPLAY)",
                    &GameAutoplayDevCommandsInternal::runAutoplay );
} // namespace sw
#endif
