/**
 * @file CrashContext.h
 * @brief 크래시 시점에 **할당 없이** 읽을 수 있도록 미리 채워 두는 키-값 저장소
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Process/CrashHandler.h"
#include "Core/String/fixed_string.h"

namespace sw
{
    /**
     * @struct CrashContextStore
     * @brief 크래시 리포트에 함께 적을 키-값을 고정 버퍼에 들고 있습니다.
     * @details **크래시 시점에는 힙을 믿을 수 없다.** 접근 위반이 힙 손상에서 왔다면 그 안에서 하는
     *          할당이 다시 죽는다. 그래서 값은 미리 고정 크기 문자열에 복사해 두고, 핸들러는 읽기만 한다.
     *          플랫폼 세 구현(Windows/Linux/Mac)이 같은 저장소를 쓴다.
     */
    struct SW_API CrashContextStore
    {
        /** @brief 키 하나의 최대 길이 (넘으면 잘립니다). */
        static constexpr uint32 kMaxKeyLength = 32;
        /** @brief 값 하나의 최대 길이 (넘으면 잘립니다). */
        static constexpr uint32 kMaxValueLength = 160;

        struct Entry
        {
            fixed_string<kMaxKeyLength>   _key;
            fixed_string<kMaxValueLength> _value;
        };

        Entry  _arrEntry[CrashHandler::kMaxContextEntry];
        uint32 _entryCount{ 0 };

        /** @brief 프로세스 전역 저장소입니다. */
        static CrashContextStore& get();

        /** @brief 키를 넣거나 덮어씁니다. 자리가 없으면 조용히 버립니다(크래시 경로에서 실패를 키우지 않는다). */
        void set( string_view key, string_view value );
    };

    /** @brief 이 실행을 식별하는 세션 ID (첫 호출 때 정해집니다). */
    SW_API const utf8* getCrashSessionId();

    /** @brief 덤프·리포트를 쓸 폴더 (setReportFolder 로 정하지 않았으면 빈 문자열). */
    SW_API const utf8* getCrashReportFolder();

    /** @brief 덤프·리포트를 쓸 폴더를 정합니다. */
    SW_API void setCrashReportFolder( string_view folderPath );

    /**
     * @brief 리포트 파일 경로를 만듭니다 — `<폴더>/crash_<세션ID>.<확장자>`.
     * @details **할당하지 않습니다.** 크래시 시점에 힙을 만지지 않으려고 호출자의 버퍼에 씁니다.
     */
    SW_API void buildCrashReportPath( utf8* pOutPath, uint32 outSize, const utf8* pExtension );

    /**
     * @brief 크래시 컨텍스트를 텍스트로 씁니다 (백엔드·GPU·빌드 등 미리 등록해 둔 값).
     * @details 언리얼이 CrashContext 로 올리는 것과 같은 자리다. 덤프·코어만으로는 "어느 백엔드에서
     *          났는가", "어떤 드라이버였는가" 를 알 수 없는데 그게 범위를 좁히는 첫 질문이다.
     *          세 플랫폼이 같은 형식을 쓰도록 여기 한 번만 둔다.
     */
    SW_API void writeCrashContextFile( const utf8* pReason, const void* pFaultAddress, uint64 processId, uint64 threadId );

    /**
     * @brief 심볼화된 콜 스택을 파일로 남깁니다.
     * @details stderr 는 배포 환경에서 아무도 보지 않는다 — 파일로 남겨야 고객이 보낼 수 있다.
     */
    SW_API void writeCrashStackFile( const utf8* pStackText );

    /**
     * @brief 크래시 리포트 본문을 만들어 stderr · 파일 · 로그에 남깁니다.
     * @details 세 플랫폼이 **같은 리포트**를 내도록 여기 한 번만 둔다. 예전에는 Windows 와 POSIX 가 이
     *          본문을 각자 적고 있었고 이미 갈려 있었다 — 어느 파일을 보내면 되는지 적는 줄이 Windows
     *          에만 있어서, 리눅스 사용자는 리포트가 어디 났는지 알 수 없었다.
     * @param pReason          폴트 이름 (예외 코드명 · 시그널명).
     * @param pFaultAddress    폴트 주소. 없으면 nullptr.
     * @param pPlatformContext 스택을 걸어갈 시작점 (Windows 는 `CONTEXT*`). nullptr 이면 현재 스택.
     * @param bMiniDumpWritten 미니덤프를 **실제로** 남겼는가. 남긴 쪽만 그 경로를 목록에 넣는다.
     * @note 여기서부터는 **할당이 생긴다** — `symbolize` 가 `sw::string` 을 값으로 돌려주고
     *       `StringBuilder` 도 8KB 를 넘기면 힙으로 확장한다. 할당 없는 것(미니덤프 · 컨텍스트 파일)은
     *       호출자가 이 함수보다 **먼저** 써 두어야 한다.
     */
    SW_API void writeCrashReport( const utf8* pReason, const void* pFaultAddress, void* pPlatformContext, bool bMiniDumpWritten );
} // namespace sw
