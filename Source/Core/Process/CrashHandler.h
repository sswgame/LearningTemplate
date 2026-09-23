/**
 * @file CrashHandler.h
 * @brief 처리되지 않은 예외와 치명적인 시그널을 잡아 심볼 변환한 콜 스택 · 미니덤프 · 컨텍스트를 남깁니다.
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
     * @details 설치한 뒤 접근 위반이나 시그널이 발생하면 예외 코드 · 주소와 폴트 스레드의 콜 스택을 남기고, 원래 동작(프로세스
     *          종료)으로 넘깁니다. 로그가 없으면 원인을 전혀 알 수 없으므로 부팅 초기에 설치합니다.
     *
     *          배포본에서는 텍스트 콜 스택만으로는 부족합니다. 최적화된 빌드는 인라인과 꼬리 호출로 프레임이 합쳐지고 지역
     *          변수도 없습니다. 그래서 **미니덤프**를 함께 씁니다. 언리얼이 CrashReportClient 로 덤프 · 로그 · 컨텍스트를 묶어
     *          올리는 것과 같은 역할입니다(여기서는 별도 프로세스 없이 같은 프로세스에서 씁니다. 재진입 가드와 할당 없는 경로로
     *          버팁니다).
     */
    class SW_API CrashHandler
    {
    public:
        /** @brief 크래시 리포트에 함께 적을 키-값의 최대 개수입니다. */
        static constexpr uint32 kMaxContextEntry = 24;

        /** @brief 핸들러를 설치합니다. 두 번 불러도 한 번만 설치됩니다. */
        static void initialize();
        /** @brief 핸들러를 제거하고 이전 핸들러를 되돌립니다. */
        static void shutdown();

        /**
         * @brief 크래시 리포트에 함께 적을 키-값을 등록합니다.
         * @details 엔진 · 게임이 아는 정보를 크래시 시점에 알 수 있도록 미리 올려 둡니다. 백엔드, GPU 어댑터와 드라이버 버전,
         *          빌드 식별자, 활성 씬 같은 것들입니다. **크래시 시점에는 할당을 하지 않습니다**(힙이 이미 깨져 있을 수 있습니다).
         *          그래서 여기서 미리 고정 버퍼에 복사해 둡니다. 같은 키를 다시 주면 덮어씁니다.
         *
         *          이 저장소는 RHI 백엔드가 넷이라 "어느 백엔드에서 났는가" 가 특히 중요합니다. 범위를 좁히는 첫 질문이 늘 그것이었습니다.
         * @param key   짧은 식별자(예: "RHI", "GPU", "Build")
         * @param value 값. 길면 잘립니다.
         */
        static void setContextValue( string_view key, string_view value );

        /**
         * @brief 이 실행을 식별하는 세션 ID 입니다(프로세스마다 하나이고, 부팅 때 정해집니다).
         * @details 로그 파일 이름과 크래시 리포트에 같은 값이 들어가 둘을 짝지을 수 있습니다. 사용자가 보낸 덤프와 로그가 같은
         *          실행의 것인지 확인하는 유일한 방법입니다.
         */
        static const utf8* getSessionId();

        /** @brief 덤프 · 리포트를 쓸 폴더를 정합니다(기본값은 로그 폴더). */
        static void setReportFolder( string_view folderPath );
    };
} // namespace sw
