/**
 * @file ModuleCallGuard.h
 * @brief 핫 리로드가 새 모듈 코드를 처음 부르는 자리를 하드웨어 예외(접근 위반 등)로부터 지킵니다. Dev 전용입니다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Delegate/Delegate.h"

namespace sw
{
    /**
     * @struct ModuleCallGuard
     * @brief 한 호출 안에서 난 하드웨어 예외를 잡아, 프로세스 대신 그 호출만 실패시킵니다.
     * @details 새로 빌드한 게임 · 에디터 코드가 리로드 직후 초기화에서 죽으면, 지키지 않을 때는 에디터 프로세스째 내려가 저장하지 않은
     *          작업을 잃습니다. 지키면 그 모듈만 버리고(무엇을 버릴지는 부르는 쪽이 정한다) 에디터는 살아 저장할 기회가 남습니다.
     *          - Windows 는 SEH(`__try` / `__except`), 리눅스는 결함 시그널(SIGSEGV · SIGBUS · SIGFPE · SIGILL) + `sigsetjmp`.
     *          - **중단점(assert) · 스택 넘침 · 그 밖의 예외는 잡지 않습니다.** assert 는 디버거와 크래시 처리기로 가야 합니다.
     *          - 잡은 뒤의 상태는 온전하지 않습니다. 결함 난 호출 안쪽 프레임의 소멸자는 돌지 않고(쥔 락 · 할당이 남는다), 그 모듈의
     *            자료도 반쯤 만들어진 채입니다. 이것은 **저장하고 재시작할 시간**을 버는 장치이지 계속 돌리는 장치가 아닙니다.
     *          - 모듈 로드(정적 초기화) 자체는 지키지 않습니다. 로더 락을 쥔 채 빠져나오면 다음 로드가 멈춥니다.
     *          - 메인 스레드에서만 부릅니다. 리눅스의 시그널 처리기는 프로세스 전체에 걸리므로 겹쳐 부르면 바깥 것만 설치 · 해제하고, 지키는
     *            호출 밖(다른 스레드)의 결함은 원래 처리기(크래시 처리기)로 돌려보냅니다.
     */
    struct ModuleCallGuard
    {
        /**
         * @brief @p call 을 지키며 부릅니다.
         * @param outFaultCode 결함이 났으면 그 코드(Windows 예외 코드 · 리눅스 시그널 번호), 아니면 0 입니다.
         * @return 결함 없이 끝났으면 true 입니다.
         */
        static bool run( const Delegate<void()>& call, uint32& outFaultCode );
    };
} // namespace sw
