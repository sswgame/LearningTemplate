/**
 * @file TaskProfileHook.h
 * @brief 태스크 실행 · 스테이지 대기를 외부 프로파일러 구간으로 내보내는 훅을 정의합니다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Delegate/Subscription.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"

namespace sw
{
    /** @brief 태스크 프로파일 구간의 종류입니다. 같은 이름이라도 실행과 기다림은 다른 구간입니다. */
    enum class TaskProfileZoneKind : uint8
    {
        Execute,   ///< 태스크 본문 실행(이름 = 태스크 이름)
        WaitStage, ///< 스테이지를 기다림(이름 = 스테이지 이름)
        WaitJoin,  ///< `runParallel` 계열의 호출 스레드가 남의 청크를 기다림(이름 = "runParallel")
    };

    struct TaskProfileHook;

    /** @brief 열린 구간 하나입니다. 연 쪽이 준 값을 닫을 때 그대로 돌려줍니다. */
    struct TaskProfileZone
    {
        void*                  _pContext{ nullptr }; ///< 구간을 연 출력(Core 는 모르는 타입). nullptr 이면 구간을 열지 않았다
        uint64                 _token{ 0 };          ///< 그 출력이 돌려준 구간 값
        const TaskProfileHook* _pHook{ nullptr };    ///< 구간을 연 훅(`TaskProfileHook::beginZone` 이 적는다). 열린 동안 훅이 바뀌어도 연 훅으로 닫는다
    };

    /**
     * @struct TaskProfileHook
     * @brief 태스크 실행 · 스테이지 대기를 외부 프로파일러 구간으로 내보내는 함수 한 쌍과, 그 훅을 거는 자리 · 태스크 이름 칸의 구성 스위치입니다. Core 는 프로파일러를 모르므로 Engine 이 꽂습니다.
     * @details 어느 스레드에서나 불리고 같은 스레드에서 짝이 맞습니다. 이름은 호출 동안만 유효합니다. 구간은 연 출력으로 닫습니다(`_pContext`).
     *          배포는 훅 자리 · 이름 칸이 없다. `beginZone` · `endZone` 은 빈 인라인 함수가 되어 부르는 곳마다 `#if` 를 두지 않는다(언리얼 `TRACE_CPUPROFILER_EVENT_SCOPE` 가 배포에서 빈 매크로인 자리).
     */
    struct TaskProfileHook
    {
        TaskProfileZone ( *_pBeginZone )( const utf8* pName, TaskProfileZoneKind kind ); ///< 구간을 연다
        void ( *_pEndZone )( const TaskProfileZone& zone );                              ///< `_pBeginZone` 이 연 구간을 닫는다

#if !defined( SW_SHIPPING )
        /** @brief 훅 자리 · 이름 칸이 이 빌드에 있으면 true 입니다. 구간을 열지 가르는 값을 읽는 비용(원자 읽기)까지 배포에서 빼려는 자리가 읽는다. */
        static constexpr bool kCompiled = true;
        /** @brief 태스크 · 스테이지 이름의 최대 길이입니다. */
        static constexpr uint32 kNameCapacity = 31;

        /**
         * @brief 태스크 실행 · 스테이지 대기 구간을 내보낼 곳을 꽂습니다. 돌려준 구독이 풀리면 뗍니다. 프로세스의 모든 태스크 매니저가 함께 씁니다.
         * @details 뒤에 건 훅이 앞의 것을 갈아 끼우고, 앞 구독이 늦게 풀려도 새 훅을 지우지 않습니다(`AtomicSlot`). @p pHook 은 프로세스 끝까지 살아 있어야 합니다
         *          (실행 중인 태스크가 읽고 있을 수 있다). 꽂혀 있지 않으면 구간마다 원자 읽기 하나가 비용의 전부입니다.
         */
        [[nodiscard]] SW_API static Subscription bind( const TaskProfileHook* pHook );
        /** @brief 꽂힌 훅이 있고 @p pName 이 비어 있지 않으면 구간을 엽니다. 아니면 열지 않은 구간(`_pContext == nullptr`)입니다. */
        [[nodiscard]] SW_API static TaskProfileZone beginZone( const utf8* pName, TaskProfileZoneKind kind );
        /** @brief `beginZone` 이 연 구간을 연 훅으로 닫습니다. 열지 않은 구간이면 아무것도 하지 않습니다. */
        SW_API static void endZone( const TaskProfileZone& zone );

        /** @brief @p name 을 이름 칸 @p arrName 에 담습니다(용량을 넘으면 잘라 담고, 늘 널로 끝낸다). */
        static void copyName( utf8 ( &arrName )[kNameCapacity + 1], string_view name )
        {
            const uint32 length = static_cast<uint32>( name.size() < kNameCapacity ? name.size() : kNameCapacity );
            if ( length > 0 )
                Memory::copy( arrName, name.data(), length );
            arrName[length] = 0;
        }
#else
        static constexpr bool kCompiled = false;

        static TaskProfileZone beginZone( const utf8* /*pName*/, TaskProfileZoneKind /*kind*/ ) { return {}; }
        static void            endZone( const TaskProfileZone& /*zone*/ ) {}
#endif
    };
} // namespace sw
