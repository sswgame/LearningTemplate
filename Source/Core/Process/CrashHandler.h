/**
 * @file CrashHandler.h
 * @brief 미처리 예외 / 치명적 시그널을 잡아 심볼화된 콜 스택·미니덤프·컨텍스트를 남깁니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"

namespace sw
{
    /**
     * @class CrashHandler
     * @brief 프로세스 전역 크래시 핸들러입니다.
     * @details 설치 후 접근 위반/시그널이 나면 예외 코드·주소와 폴트 스레드의 콜 스택을 남기고
     *          기존 동작(프로세스 종료)으로 넘깁니다. 로그가 없으면 원인을 전혀 알 수 없으므로
     *          부팅 초기에 설치합니다.
     *
     *          배포본에서는 텍스트 콜 스택만으로는 부족하다 — 최적화된 빌드는 인라인·꼬리호출로
     *          프레임이 접히고 지역 변수도 없다. 그래서 **미니덤프**를 함께 쓴다. 언리얼이
     *          CrashReportClient 로 덤프·로그·컨텍스트를 묶어 올리는 것과 같은 자리다(여기서는
     *          별도 프로세스 없이 같은 프로세스에서 쓴다 — 재진입 가드와 무할당 경로로 버틴다).
     */
    class SW_API CrashHandler
    {
    public:
        /** @brief 크래시 리포트에 함께 적을 키-값의 최대 개수. */
        static constexpr uint32 kMaxContextEntry = 24;

        /** @brief 핸들러를 설치합니다. 두 번 호출해도 한 번만 설치됩니다. */
        static void initialize();
        /** @brief 핸들러를 제거하고 이전 핸들러를 복구합니다. */
        static void shutdown();

        /**
         * @brief 크래시 리포트에 함께 적을 키-값을 등록합니다.
         * @details 엔진·게임이 아는 것을 크래시 시점에 알 수 있게 미리 올려 둔다 — 백엔드, GPU 어댑터와
         *          드라이버 버전, 빌드 식별자, 활성 씬 같은 것들이다. **크래시 시점에는 할당을 하지
         *          않는다**(힙이 이미 깨져 있을 수 있다). 그래서 여기서 미리 고정 버퍼에 복사해 둔다.
         *          같은 키를 다시 주면 덮어쓴다.
         *
         *          이 저장소는 RHI 백엔드가 넷이라 "어느 백엔드에서 났는가" 가 특히 중요하다 —
         *          범위를 좁히는 첫 질문이 늘 그것이었다.
         * @param key   짧은 식별자 (예: "RHI", "GPU", "Build").
         * @param value 값. 길면 잘립니다.
         */
        static void setContextValue( string_view key, string_view value );

        /**
         * @brief 이 실행을 식별하는 세션 ID 입니다 (프로세스마다 하나, 부팅 때 정해집니다).
         * @details 로그 파일 이름과 크래시 리포트에 같은 값이 들어가 둘을 짝지을 수 있다.
         *          고객이 보낸 덤프와 로그가 같은 실행의 것인지 확인하는 유일한 방법이다.
         */
        static const utf8* getSessionId();

        /** @brief 덤프·리포트를 쓸 폴더를 정합니다 (기본은 로그 폴더). */
        static void setReportFolder( string_view folderPath );
    };
} // namespace sw
