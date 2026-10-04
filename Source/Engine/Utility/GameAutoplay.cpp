#include "pch.h"

#include "Engine/Utility/GameAutoplay.h"

#include "Core/Container/vector.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"

#include "Engine/Common/EngineServices.h"

namespace sw
{
    namespace
    {
        struct GameAutoplayInternal
        {
            /** @brief 등록된 자동 플레이(등록 순서). Engine 이미지의 함수 정적이라 게임 모듈이 바뀌어도 남는다. */
            static vector<const GameAutoplayRegistration*>& getRegistrations()
            {
                static vector<const GameAutoplayRegistration*> s_listRegistration;
                return s_listRegistration;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    void GameAutoplay::registerAutoplay( const GameAutoplayRegistration* pRegistration )
    {
        if ( pRegistration != nullptr && pRegistration->_pValue != nullptr )
            GameAutoplayInternal::getRegistrations().push_back( pRegistration );
    }

    void GameAutoplay::unregisterAutoplay( const GameAutoplayRegistration* pRegistration )
    {
        vector<const GameAutoplayRegistration*>& listRegistration = GameAutoplayInternal::getRegistrations();
        for ( size_t index = 0; index < listRegistration.size(); ++index )
        {
            if ( listRegistration[index] != pRegistration )
                continue;
            listRegistration.erase( listRegistration.begin() + static_cast<ptrdiff_t>( index ) );
            return;
        }
    }

    const GameAutoplayRegistration* GameAutoplay::findActive()
    {
        const vector<const GameAutoplayRegistration*>& listRegistration = GameAutoplayInternal::getRegistrations();
        return listRegistration.empty() ? nullptr : listRegistration.back();
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
