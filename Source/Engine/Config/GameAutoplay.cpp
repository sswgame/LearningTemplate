#include "pch.h"

#include "Engine/Config/GameAutoplay.h"

#include "Core/Container/StringUtil.h"
#include "Core/Container/vector.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Log/Logger.h"
#include "Core/String/RegistrationList.h"

#include "Engine/Foundation/EngineServices.h"

namespace sw
{
    SW_LOG_CALLER( "GameAutoplay" );

    namespace
    {
        /**
         * @brief 등록된 자동 플레이(등록 순서 · 이름은 전역 변수 이름). Engine 이미지의 함수 정적이라 게임 모듈이 바뀌어도 남는다.
         *        핫 리로드가 올리는 교체 이미지의 같은 이름은 대체한다(`RegistrationReloadScope`).
         */
        RegistrationList<const GameAutoplayRegistration>& getRegistrations()
        {
            static RegistrationList<const GameAutoplayRegistration> s_registration{ "GameAutoplayRegistry", RegistrationOrder::Insertion, true };
            return s_registration;
        }
    } // namespace
} // namespace sw

namespace sw
{
    void GameAutoplay::registerAutoplay( const GameAutoplayRegistration* pRegistration )
    {
        const bool bInvalidAutoplayRegistration = pRegistration == nullptr || pRegistration->_pValue == nullptr || pRegistration->_pVariableName == nullptr;
        if ( bInvalidAutoplayRegistration )
            return;
        (void)getRegistrations().addLogged( pRegistration, "Game autoplay", pRegistration->_pVariableName );
    }

    void GameAutoplay::unregisterAutoplay( const GameAutoplayRegistration* pRegistration )
    {
        // 빼기가 저장소를 남은 수에 맞춘다 — 게임이 키운 용량이 EngineMisc 태그에 남지 않는다.
        (void)getRegistrations().remove( pRegistration ); // 등록되어 있지 않은 것을 빼는 것은 할 일이 없다
    }

    const GameAutoplayRegistration* GameAutoplay::findActive()
    {
        const RegistrationList<const GameAutoplayRegistration>& registration = getRegistrations();
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
        GlobalVariableInfo* pInfo = engine::areEngineServicesBound() ? engine::getSubsystem<GlobalVariableManager>().findVariable( pActive->_pVariableName ) : nullptr;
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
