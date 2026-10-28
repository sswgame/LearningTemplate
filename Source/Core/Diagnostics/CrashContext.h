/**
 * @file CrashContext.h
 * @brief 크래시 시점에 **할당 없이** 읽을 수 있도록 미리 채워 두는 키-값 저장소입니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Concurrency/atomic.h"
#include "Core/Container/string.h"
#include "Core/Diagnostics/CrashHandler.h"
#include "Core/String/fixed_string.h"

namespace sw
{
    /**
     * @struct CrashContextStore
     * @brief 크래시 리포트에 함께 적을 키-값을 고정 버퍼에 들고 있습니다.
     * @details **크래시 시점에는 힙을 믿을 수 없습니다.** 접근 위반이 힙 손상에서 왔다면 그 안에서 하는 할당이 다시 죽습니다.
     *          그래서 값은 미리 고정 크기 문자열에 복사해 두고, 핸들러는 읽기만 합니다. 플랫폼 구현(Windows · Linux)이
     *          같은 저장소를 씁니다.
     */
    struct SW_API CrashContextStore
    {
        /** @brief 키 하나의 최대 길이입니다(넘으면 잘립니다). */
        static constexpr uint32 kMaxKeyLength = 32;
        /** @brief 값 하나의 최대 길이입니다(넘으면 잘립니다). */
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

        /** @brief 키를 넣거나 덮어씁니다. 공간이 없으면 조용히 버립니다(크래시 경로에서 실패를 키우지 않기 위해서입니다). */
        void set( string_view key, string_view value );
    };
} // namespace sw

namespace sw
{
    /**
     * @struct CrashBreadcrumbStore
     * @brief 크래시 직전의 최근 사건(빵부스러기 — 텔레메트리 사건 · 게임 표지)을 고정 버퍼 고리에 들고 있습니다. 크래시 때 할당 없이 파일로 씁니다.
     * @details 쓰는 쪽은 위치를 원자 카운터로 받아 서로 다른 슬롯에 씁니다(한 바퀴 — 32 개 — 안에서 겹치지 않는다). 크래시 경로는 잠그지 않고 읽으므로
     *          그 순간 쓰던 한 줄은 잘려 있을 수 있습니다(진단용이라 받아들인다). Sentry 의 breadcrumbs 에 해당합니다.
     */
    struct SW_API CrashBreadcrumbStore
    {
        static constexpr uint32 kMaxBreadcrumb = 32;
        static constexpr uint32 kMaxLength     = 160;

        fixed_string<kMaxLength> _arrEntry[kMaxBreadcrumb];
        atomic<uint32>           _pushedCount{ 0 }; ///< 지금까지 넣은 수 — 다음 칸은 `% kMaxBreadcrumb`

        /** @brief 프로세스 전역 저장소입니다. */
        static CrashBreadcrumbStore& get();

        /** @brief 한 줄을 넣습니다(길면 잘린다). 가장 오래된 것을 덮습니다. */
        void add( string_view text );
        /** @brief 들어 있는 줄을 오래된 것부터 @p pOutText 에 줄바꿈으로 이어 씁니다. **할당하지 않습니다.** 쓴 길이입니다. */
        uint32 writeText( utf8* pOutText, uint32 capacity ) const;
    };

    /** @brief 이 실행을 식별하는 세션 ID 입니다(처음 부를 때 정해집니다). */
    SW_API const utf8* getCrashSessionID();

    /** @brief 덤프 · 리포트를 쓸 폴더입니다(setCrashReportFolder 로 정하지 않았으면 빈 문자열). */
    SW_API const utf8* getCrashReportFolder();

    /** @brief 덤프 · 리포트를 쓸 폴더를 정합니다. */
    SW_API void setCrashReportFolder( string_view folderPath );

    /** @brief 크래시 보고에 줄 시한(초)입니다(`CrashHandler::setReportDeadline`). */
    SW_API uint32 getCrashReportDeadlineSeconds();

    /**
     * @brief 크래시 문맥 파일(`crash_<세션>.txt`) 끝에 붙일 글을 쓰는 함수입니다. **할당 · 잠금 없이** @p pOutText 에 쓰고 쓴 길이를 돌려줍니다.
     * @details 크래시 시점에 읽어야 하는 소유자 쪽 상태(서버의 최근 틱 이력 링 버퍼 같은 것)를 미리 정해 둔 키-값 대신 그대로 싣는 자리입니다.
     *          죽어 가는 프로세스에서 새 파일을 쓰지 않고 이미 쓰는 문맥 파일에 얹으므로, 보고 묶음(`CrashReportService`)이 따로 챙길 것이 없습니다.
     */
    using CrashExtraTextWriter = uint32 ( * )( void* pUser, utf8* pOutText, uint32 capacity );

    /** @brief 문맥 파일 끝에 글을 붙일 함수를 겁니다(하나뿐 — 다시 걸면 바꿉니다). @p pWriter 가 nullptr 이면 풉니다. 소유자는 @p pUser 를 지우기 전에 풀어야 합니다. */
    SW_API void setCrashExtraTextWriter( CrashExtraTextWriter pWriter, void* pUser );

    /**
     * @brief 리포트 파일 경로 `<폴더>/crash_<세션ID>.<확장자>` 를 만듭니다.
     * @details **할당하지 않습니다.** 크래시 시점에 힙을 건드리지 않도록 호출하는 쪽의 버퍼에 씁니다.
     */
    SW_API void makeCrashReportPath( utf8* pOutPath, uint32 outSize, const utf8* pExtension );

    /**
     * @brief 크래시 컨텍스트(백엔드 · GPU · 빌드 등 미리 등록해 둔 값)를 텍스트로 씁니다.
     * @details 언리얼이 CrashContext 로 올리는 것과 같은 역할입니다. 덤프나 코어 파일만으로는 "어느 백엔드에서 났는가",
     *          "어떤 드라이버였는가" 를 알 수 없는데, 그것이 범위를 좁히는 첫 질문입니다. 세 플랫폼이 같은 형식을 쓰도록 여기에
     *          한 번만 둡니다.
     */
    SW_API void writeCrashContextFile( const utf8* pReason, const void* pFaultAddress, uint64 processID, uint64 threadID );

    /**
     * @brief 심볼 변환한 콜 스택을 파일로 남깁니다.
     * @details 배포 환경에서는 아무도 stderr 를 보지 않습니다. 파일로 남겨야 사용자가 보내 줄 수 있습니다.
     */
    SW_API void writeCrashStackFile( const utf8* pStackText );

    /**
     * @brief 파일 하나를 통째로 씁니다(덮어쓰기). **할당하지 않고 stdio 도 쓰지 않습니다** — OS 원시 호출(`CreateFileW` · `open`)만 부릅니다.
     * @details 크래시 경로와 같은 이유로, 게임 스레드가 힙 · 스트림 락을 쥔 채 멈춘 행에서 감시 스레드가 보고를 남길 때도 이것을 씁니다. 경로는 UTF-8, 못 쓰면 조용히 돌아옵니다.
     */
    SW_API void writeFileWithoutAllocation( const utf8* pPath, const utf8* pText, uint32 length );

    /** @brief 빵부스러기(`CrashBreadcrumbStore`)를 `crash_<세션>.breadcrumbs.txt` 로 씁니다. 비어 있으면 쓰지 않습니다. **할당하지 않습니다.** */
    SW_API void writeCrashBreadcrumbFile();

    /**
     * @brief 크래시 리포트 본문을 만들어 stderr · 파일 · 로그에 남깁니다.
     * @details 세 플랫폼이 **같은 리포트**를 내도록 여기에 한 번만 둡니다(어느 파일을 보내면 되는지 적는 줄 같은 것이
     *          한 플랫폼에서만 빠지지 않게).
     * @param pReason          폴트 이름(예외 코드 이름 · 시그널 이름)
     * @param pFaultAddress    폴트 주소. 없으면 nullptr
     * @param pPlatformContext 스택을 따라가기 시작할 지점(Windows 는 `CONTEXT*`). nullptr 이면 현재 스택
     * @param bMiniDumpWritten 미니덤프를 **실제로** 남겼는지 여부. 남긴 경우에만 그 경로를 목록에 넣습니다
     * @param pPlatformThread  폴트 스레드(Windows `HANDLE`). nullptr 이면 지금 스레드 — 보고 스레드가 대신 쓸 때 넘깁니다
     * @note 여기서부터는 **할당이 생깁니다.** `symbolize` 가 `sw::string` 을 값으로 반환하고, `StringBuilder` 도 8KB 를 넘기면
     *       힙으로 늘어납니다. 할당이 없는 것(미니덤프 · 컨텍스트 파일)은 호출하는 쪽이 이 함수보다 **먼저** 써 두어야 합니다.
     */
    SW_API void writeCrashReport( const utf8* pReason, const void* pFaultAddress, void* pPlatformContext, bool bMiniDumpWritten,
                                  void* pPlatformThread = nullptr );
} // namespace sw
