/**
 * @file GameAPI.h
 * @brief App ↔ SWGame 통신용 함수 테이블입니다.
 *
 * @note 가변 크기 결과는 호스트가 준 할당 싱크(`StateBufferSink`)로 한 번에 받는다. "버퍼가 없으면 크기만" 약속은
 *       결과를 두 번 만들게 하고, 두 번째가 더 크면 버퍼가 모자라 실패하므로 표에 두지 않는다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"

#include "RuntimeAPI/ABI/RuntimeHandles.h"

namespace sw
{
    // 핸들은 ABI/RuntimeHandles.h 가 모아 들고 있다. GameHandle 포함.

    class ISubsystemLocator;

    /**
     * @brief 호스트가 상태 바이트를 받는 버퍼 싱크입니다. 모듈은 직렬화를 한 번 돌려 크기가 정해진 뒤 `allocate` 를 한 번 부르고 거기에 씁니다(결과가 비면 부르지 않는다).
     * @details 메모리는 호스트 것이고 수명도 호스트가 정한다. 모듈은 받은 포인터를 `serializeState` 가 끝난 뒤에 들고 있지 않는다.
     */
    struct StateBufferSink
    {
        void* _pContext{ nullptr };                                  /**< @brief 호스트의 받는 쪽(예: 바이트 벡터). `allocate` 에 그대로 넘긴다. */
        void* ( *allocate )(void* pContext, uint32 size){ nullptr }; /**< @brief @p size 바이트를 쓸 호스트 메모리를 돌려준다. 받지 못하면 nullptr. */
    };
} // namespace sw

namespace sw
{
    // ------------------------------------------------------------------------------
    // 1) GameAPI — C ABI 함수 테이블
    //    IGame 구현은 SWGame 안에 두고, App 은 이 포인터만 부른다
    // ------------------------------------------------------------------------------
    /** @brief SWGame 이 exportGameAPI 로 채우고 App 이 부르는 함수 포인터 테이블입니다. */
    struct GameAPI
    {
        GameHandle ( *create )(){ nullptr };                                                                /**< @brief 게임 인스턴스를 생성합니다. */
        void ( *destroy )( GameHandle game ){ nullptr };                                                    /**< @brief 게임 인스턴스를 파괴합니다. */
        bool ( *initialize )( GameHandle game, WindowHandle window, RHIDeviceHandle rhiDevice ){ nullptr }; /**< @brief 윈도우 및 RHI 디바이스로 게임을 초기화합니다. */
        void ( *shutdown )( GameHandle game ){ nullptr };                                                   /**< @brief 게임을 종료합니다. */
        void ( *update )( GameHandle game, float32 deltaTime ){ nullptr };                                  /**< @brief 프레임 업데이트를 수행합니다. */
        void ( *fixedUpdate )( GameHandle game, float32 fixedDeltaTime ){ nullptr };                        /**< @brief 고정 프레임 업데이트를 수행합니다. */
        void ( *bindService )( const ISubsystemLocator* pService ){ nullptr };                              /**< @brief 호스트 서비스 범위를 게임 모듈에 주입하거나 nullptr 로 해제합니다. */
        bool ( *serializeState )( GameHandle game, const StateBufferSink* pSink ){ nullptr };               /**< @brief 게임 상태를 한 번 직렬화해 @p pSink 가 준 버퍼에 씁니다. 구현이 지원하는 범위만 복원됩니다. */
        bool ( *deserializeState )( GameHandle game, const void* pInBuffer, uint32 size ){ nullptr };       /**< @brief 버퍼에서 게임 상태를 복원합니다. */
    };

    /** @brief SWGame 이 export 하는 API 테이블 함수의 형입니다(심볼 이름: exportGameAPI). */
    using PFN_ExportGameAPI = bool ( * )( GameAPI* pOutAPI );
} // namespace sw

extern "C"
{
    // ------------------------------------------------------------------------------
    // 2) export — SWGame 이 채우는 진입점
    // ------------------------------------------------------------------------------
    /** @brief 이 모듈이 빌드된 표 버전입니다. 호스트가 로드 전에 자기 것과 대조합니다. */
    SW_MODULE_API uint32 getGameModuleABIVersion();
    /** @brief 이 모듈이 빌드된 표 지문입니다. 버전과 함께 대조합니다. */
    SW_MODULE_API const utf8* getGameModuleABIStamp();

    /** @brief SWGame API 테이블을 내보냅니다. */
    SW_MODULE_API bool exportGameAPI( sw::GameAPI* pOutAPI );
}
