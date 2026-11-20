/**
 * @file GameAPI.h
 * @brief App ↔ SWGame 통신용 함수 테이블입니다.
 *
 * @note 가변 크기 결과는 호스트가 준 할당 싱크(`StateBufferSink`)로 한 번에 받는다. "버퍼가 없으면 크기만" 약속은
 *       결과를 두 번 만들게 하고, 두 번째가 더 크면 버퍼가 모자라 실패하므로 표에 두지 않는다.
 */
#pragma once
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Memory/MemoryTag.h"

#include "ModuleAPI/ABI/ModuleABI.h"
#include "ModuleAPI/ABI/ModuleHandles.h"

namespace sw
{
    // 핸들은 ABI/ModuleHandles.h 가 모아 들고 있다. GameHandle 포함.

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
    /**
     * @brief `GameAPI` 표의 항목 목록입니다 — `X( 반환형, 이름, ( 매개변수 ) )`. 구조체 멤버와 스탬프(`kGameModuleABIStamp`)가 둘 다 여기서 나옵니다.
     * @details 항목을 더하거나 순서 · 시그니처를 바꾸면 스탬프가 저절로 바뀌어 옛 모듈 DLL 은 로드 때 거부됩니다(`ModuleABI.h`).
     *          줄 연결 매크로 안이라 설명은 블록 주석으로 씁니다(줄 주석은 다음 줄을 삼킨다).
     */
#define SW_GAME_API_ENTRIES( X )                                                                    \
    /** 게임 인스턴스를 생성합니다. */                                                              \
    X( GameHandle, create, () )                                                                     \
    /** 게임 인스턴스를 파괴합니다. */                                                              \
    X( void, destroy, ( GameHandle game ) )                                                         \
    /** 윈도우로 게임을 초기화합니다(디바이스는 받지 않는다 — 엔진이 붙인다). */                    \
    X( bool, initialize, ( GameHandle game, WindowHandle window ) )                                 \
    /** 게임을 종료합니다. */                                                                       \
    X( void, shutdown, ( GameHandle game ) )                                                        \
    /** 프레임 업데이트를 수행합니다. */                                                            \
    X( void, update, ( GameHandle game, float32 deltaTime ) )                                       \
    /** 고정 프레임 업데이트를 수행합니다. */                                                       \
    X( void, fixedUpdate, ( GameHandle game, float32 fixedDeltaTime ) )                             \
    /** 호스트 서비스 범위를 게임 모듈에 주입하거나 nullptr 로 해제합니다. */                       \
    X( void, bindService, ( const ISubsystemLocator* pService ) )                                   \
    /** 게임 상태를 한 번 직렬화해 pSink 가 준 버퍼에 씁니다. 구현이 지원하는 범위만 복원됩니다. */ \
    X( bool, serializeState, ( GameHandle game, const StateBufferSink* pSink ) )                    \
    /** 버퍼에서 게임 상태를 복원합니다. */                                                         \
    X( bool, deserializeState, ( GameHandle game, const void* pInBuffer, uint32 size ) )

    /** @brief SWGame 이 exportGameAPI 로 채우고 App 이 부르는 함수 포인터 테이블입니다. */
    struct GameAPI
    {
        SW_GAME_API_ENTRIES( SW_MODULE_ABI_ENTRY_MEMBER )
    };

    /** @brief `GameAPI` 표의 스탬프입니다. 게임 모듈이 내보내고(`getGameModuleABIStamp`) 호스트가 로드 전에 자기 것과 대조합니다. */
    inline constexpr uint64 kGameModuleABIStamp = ModuleABIUtil::computeStamp( SW_GAME_API_ENTRIES( SW_MODULE_ABI_ENTRY_TEXT ) );

    /** @brief 게임 모듈 경계의 할당 태그입니다 — 표 항목(`GameModuleForwardUtil`)과 모듈 매니저의 로드 · 리로드가 이 값 하나를 쓴다. */
    inline constexpr MemoryTag kGameModuleMemoryTag = MemoryTag::Game;

    /** @brief SWGame 이 export 하는 API 테이블 함수의 형입니다(심볼 이름: exportGameAPI). */
    using PFN_ExportGameAPI = bool ( * )( GameAPI* pOutAPI );
} // namespace sw

extern "C"
{
    // ------------------------------------------------------------------------------
    // 2) export — SWGame 이 채우는 진입점
    // ------------------------------------------------------------------------------
    /** @brief 이 모듈이 빌드된 표 스탬프(`sw::kGameModuleABIStamp`)입니다. 호스트가 로드 전에 자기 것과 대조합니다. */
    SW_MODULE_API uint64 getGameModuleABIStamp();

    /** @brief SWGame API 테이블을 내보냅니다. */
    SW_MODULE_API bool exportGameAPI( sw::GameAPI* pOutAPI );
}
