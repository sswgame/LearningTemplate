/**
 * @file GameModuleExports.h
 * @brief 게임 모듈의 진입점(C-ABI) 구현을 위한 매크로 모음
 *
 * @note 이 헤더는 **경계 계약이 아니라 모듈 쪽 접착제**다. 그래서 `ABI/` 와 달리 모듈 자신의
 *       서비스 로케이터(`GameFramework/`)를 끌어온다 — 모듈 구현 `.cpp` 만 include 한다.
 */
#pragma once
#include "Core/Memory/Memory.h"

#include "GameFramework/Base/GameService.h"

#include "RuntimeAPI/ABI/GameAPI.h"
#include "RuntimeAPI/Export/ModuleForwardUtil.h"

namespace sw
{
    // 아래 매크로가 불투명 핸들을 되돌릴 때만 필요하다. ABI/ 쪽 계약 헤더는 이 타입들을 모른다.
    class IRHIDevice;
    class IWindow;
} // namespace sw

/**
 * @brief 게임 모듈의 C-ABI 함수 테이블을 1줄로 구현 및 export하는 매크로
 * @param GameClass sw::IGame 인터페이스를 구현하는 게임 클래스
 * @details 핸들 캐스팅과 널 검사는 `ModuleForwardUtil` 이 한다. 테이블에 항목을 하나 더
 *          붙일 때 건드릴 곳은 `GameAPI` 구조체 한 줄과 여기 한 줄이어야 한다.
 */
#define SW_IMPLEMENT_GAME_MODULE( GameClass )                                                                                                                                                                \
    extern "C" SW_MODULE_API bool exportGameAPI( sw::GameAPI* pOutApi )                                                                                                                                      \
    {                                                                                                                                                                                                        \
        if ( pOutApi == nullptr )                                                                                                                                                                            \
            return false;                                                                                                                                                                                    \
        pOutApi->create     = []() -> sw::GameHandle { return sw_new GameClass(); };                                                                                                                         \
        pOutApi->destroy    = []( sw::GameHandle gameHandle ) { sw_delete( static_cast<GameClass*>( gameHandle ) ); };                                                                                       \
        pOutApi->initialize = []( sw::GameHandle gameHandle, sw::WindowHandle windowHandle, sw::RHIDeviceHandle rhiDeviceHandle ) -> bool                                                                    \
        { return sw::ModuleForwardUtil::callOr<GameClass, bool>( gameHandle, false, &GameClass::initialize, static_cast<sw::IWindow*>( windowHandle ), static_cast<sw::IRHIDevice*>( rhiDeviceHandle ) ); }; \
        pOutApi->shutdown    = []( sw::GameHandle gameHandle ) { sw::ModuleForwardUtil::callVoid<GameClass>( gameHandle, &GameClass::shutdown ); };                                                          \
        pOutApi->update      = []( sw::GameHandle gameHandle, float32 deltaTime ) { sw::ModuleForwardUtil::callVoid<GameClass>( gameHandle, &GameClass::update, deltaTime ); };                              \
        pOutApi->fixedUpdate = []( sw::GameHandle gameHandle, float32 fixedDeltaTime ) { sw::ModuleForwardUtil::callVoid<GameClass>( gameHandle, &GameClass::fixedUpdate, fixedDeltaTime ); };               \
        pOutApi->bindService = []( const sw::ModuleService* pService )                                                                                                                                       \
        {                                                                                                                                                                                                    \
            if ( pService != nullptr )                                                                                                                                                                       \
                sw::game::bindGameService( *pService );                                                                                                                                                      \
            else                                                                                                                                                                                             \
                sw::game::unbindGameService();                                                                                                                                                               \
        };                                                                                                                                                                                                   \
        pOutApi->serializeState = []( sw::GameHandle gameHandle, void* pOutBuffer, uint32* pInOutSize ) -> bool                                                                                              \
        { return sw::ModuleForwardUtil::callOr<GameClass, bool>( gameHandle, false, &GameClass::serializeState, pOutBuffer, pInOutSize ); };                                                                 \
        pOutApi->deserializeState = []( sw::GameHandle gameHandle, const void* pInBuffer, uint32 bufferSize ) -> bool                                                                                        \
        { return sw::ModuleForwardUtil::callOr<GameClass, bool>( gameHandle, false, &GameClass::deserializeState, pInBuffer, bufferSize ); };                                                                \
        return true;                                                                                                                                                                                         \
    }
