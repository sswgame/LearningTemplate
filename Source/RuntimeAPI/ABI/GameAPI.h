/**
 * @file GameAPI.h
 * @brief App ↔ SWGame 통신용 함수 테이블
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"

#include "RuntimeAPI/ABI/RuntimeHandles.h"

namespace sw
{
    // ------------------------------------------------------------------------------
    // 1) 핸들
    // ------------------------------------------------------------------------------
    /** @brief 게임 인스턴스를 가리키는 불투명(opaque) 핸들 */
    using GameHandle = void*;

    struct ModuleService;

    // ------------------------------------------------------------------------------
    // 2) GameAPI — C ABI 함수 테이블
    //    IGame 구현은 SWGame 안에 두고, App은 이 포인터만 호출
    // ------------------------------------------------------------------------------
    /** @brief App이 채우고 SWGame이 구현하는 함수 포인터 테이블 */
    struct GameAPI
    {
        GameHandle ( *create )(){ nullptr };                                                                /**< @brief 게임 인스턴스를 생성합니다. */
        void ( *destroy )( GameHandle game ){ nullptr };                                                    /**< @brief 게임 인스턴스를 파괴합니다. */
        bool ( *initialize )( GameHandle game, WindowHandle window, RHIDeviceHandle rhiDevice ){ nullptr }; /**< @brief 윈도우 및 RHI 디바이스로 게임을 초기화합니다. */
        void ( *shutdown )( GameHandle game ){ nullptr };                                                   /**< @brief 게임을 종료합니다. */
        void ( *update )( GameHandle game, float32 deltaTime ){ nullptr };                                  /**< @brief 프레임 업데이트를 수행합니다. */
        void ( *fixedUpdate )( GameHandle game, float32 fixedDeltaTime ){ nullptr };                        /**< @brief 고정 프레임 업데이트를 수행합니다. */
        void ( *bindService )( const ModuleService* pService ){ nullptr };                                  /**< @brief ModuleService를 게임 모듈에 주입하거나 nullptr로 해제합니다. */
        bool ( *serializeState )( GameHandle game, void* pOutBuffer, uint32* pInOutSize ){ nullptr };       /**< @brief 게임 상태를 버퍼에 직렬화하거나 크기를 반환합니다. 구현이 지원하는 범위만 복원됩니다. */
        bool ( *deserializeState )( GameHandle game, const void* pInBuffer, uint32 size ){ nullptr };       /**< @brief 버퍼에서 게임 상태를 복원합니다. */
    };

    /** @brief SWGame export 심볼: exportGameAPI */
    using PFN_ExportGameAPI = bool ( * )( GameAPI* pOutApi );

    class IWindow;
    class IRHIDevice;
} // namespace sw

extern "C"
{
    // ------------------------------------------------------------------------------
    // 3) export — SWGame이 채우는 진입점
    // ------------------------------------------------------------------------------
    /** @brief SWGame API 테이블을 내보냅니다. */
    SW_MODULE_API bool exportGameAPI( sw::GameAPI* pOutApi );
}
