/**
 * @file PlatformFileSystem.h
 * @brief 파일 시스템 OS 원시 연산(질의 · 폴더 · 지우기 · 복사 · 시각 · 경로 해석 · 나열)의 선언입니다. **`FileUtil` 의 구현(`FileUtilFileSystem.cpp`)과 플랫폼 구현(`File/Windows` ·
 *        `File/Linux` 의 `*FileSystem.cpp`)만 include 합니다** - 호출하는 쪽은 `FileUtil` 입니다.
 * @details 헤더가 있는 이유는 세 번역 단위(공용 · Windows · Linux)가 시그니처를 맞추기 위해서일 뿐이다. 경로 규칙 · 로그 · 자동화 쓰기 문(`isWriteAllowed`) · 캐시 알림은 `FileUtil` 에 있으므로
 *          이 층을 바로 부르면 그것들을 건너뛴다. `std::filesystem` 은 쓰지 않는다 - 한 층에 오류 모델 · UTF-16 변환이 둘이 되지 않게 Win32 · POSIX 네이티브 한 곳이다.
 *          경로는 UTF-8 이고 구분자는 `/` · `\` 모두 받는다. 실패는 `bool` 이며 이유는 `describeLastError` 가 문장으로 준다. 이 층은 로그를 쓰지 않는다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

namespace sw
{
    /**
     * @struct PlatformFileSystem
     * @brief 파일 시스템 OS 원시 연산 모음입니다.
     */
    struct PlatformFileSystem
    {
        /** @brief 경로가 가리키는 것의 종류입니다(링크는 따라간 결과). */
        enum class FileKind : uint8
        {
            File,
            Directory,
            Other,
        };

        /** @brief `queryStatus` 의 결과입니다. 시각은 1601-01-01 기원 100 ns 눈금(Windows FILETIME)으로 플랫폼이 같다. */
        struct FileStatus
        {
            FileKind _kind{ FileKind::Other };
            uint64   _size{ 0 };
            int64    _writeTicks{ 0 };
            bool     _bReadOnly{ false }; ///< 쓰기 권한이 없다(Windows 읽기 전용 속성, POSIX 소유자 쓰기 비트 없음)
            bool     _bLink{ false };     ///< 경로 자체가 링크(심볼릭 링크 · Windows 정션)다. 나머지 필드는 가리키는 쪽의 것이다
        };

        /** @brief `forEachChild` 의 콜백입니다. @p name 은 폴더 안의 이름이고, 링크이면 @p bLink 입니다(@p bDirectory 는 링크를 따라간 결과). false 를 돌려주면 멈춥니다. */
        using ChildVisitFn = bool ( * )( void* pContext, string_view name, bool bDirectory, bool bLink );

        /** @brief 이 플랫폼의 경로 비교가 대소문자를 가리지 않으면 true 입니다(Windows). */
        static constexpr bool kPathsAreCaseInsensitive =
#if defined( SW_PLATFORM_WINDOWS )
            true;
#else
            false;
#endif

        /** @brief @p path 가 가리키는 것을 묻습니다(링크는 따라간다). 없거나 못 읽으면 false 입니다. */
        [[nodiscard]] static bool queryStatus( string_view path, FileStatus& outStatus );
        /** @brief 폴더 하나를 만듭니다(한 단계). **이 호출이 만들었을 때만** true 입니다 - 이미 있으면 false 입니다. */
        [[nodiscard]] static bool createDirectory( string_view path );
        /** @brief 파일 · 링크 · 빈 폴더 하나를 지웁니다(POSIX 의 `remove` 의미). Windows 도 읽기 전용 속성을 무시하고 지웁니다. */
        [[nodiscard]] static bool removeEntry( string_view path );
        /** @brief 파일을 복사합니다. 대상이 있으면 덮어씁니다. */
        [[nodiscard]] static bool copyFileOverwrite( string_view source, string_view destination );
        /** @brief 쓰기 권한을 켜거나 끕니다(Windows 읽기 전용 속성, POSIX 쓰기 비트 셋). */
        [[nodiscard]] static bool setWritable( string_view path, bool bWritable );
        /** @brief 파일 수정 시각을 1601 기원 100 ns 눈금으로 정합니다. */
        [[nodiscard]] static bool setWriteTicks( string_view path, int64 ticks );
        /** @brief 지금의 파일 시각입니다(`FileStatus::_writeTicks` 와 같은 눈금). */
        [[nodiscard]] static int64 getNowTicks();
        /** @brief 절대 경로로 만듭니다(`.` · `..` 를 정리한다, 파일이 없어도 된다). 구분자는 `/` 입니다. */
        [[nodiscard]] static bool makeAbsolute( string_view path, string& outResult );
        /** @brief 실제 경로를 만듭니다(링크 · 짧은 이름을 푼다). 경로가 없으면 false 입니다. 구분자는 `/` 입니다. */
        [[nodiscard]] static bool makeCanonical( string_view path, string& outResult );
        /** @brief 작업 폴더입니다(구분자 `/`). */
        [[nodiscard]] static bool getCurrentDirectory( string& outResult );
        /** @brief 작업 폴더를 바꿉니다. */
        [[nodiscard]] static bool setCurrentDirectory( string_view path );
        /** @brief 임시 폴더입니다(구분자 `/`, 끝에 구분자 없음). */
        [[nodiscard]] static bool getTempDirectory( string& outResult );
        /**
         * @brief 폴더의 바로 아래 항목(한 단계)마다 @p visit 를 부릅니다. `.` · `..` 는 건너뜁니다. 순서는 정해져 있지 않습니다.
         * @return 폴더를 열 수 있었으면 true(콜백이 멈춘 것도 포함)입니다. 열지 못하면 false 입니다.
         */
        [[nodiscard]] static bool forEachChild( string_view directory, ChildVisitFn visit, void* pContext );
        /** @brief 직전에 실패한 OS 호출의 이유입니다(`GetLastError` · `errno` 를 문장으로). 실패 경로에서만 부른다 - 문자열을 만든다. */
        [[nodiscard]] static string describeLastError();
    };
} // namespace sw
