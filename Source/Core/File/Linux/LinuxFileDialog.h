/**
 * @file LinuxFileDialog.h
 * @brief Linux 네이티브 파일 열기 · 저장 다이얼로그입니다(zenity / kdialog / yad).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#if defined( SW_PLATFORM_LINUX )

namespace sw
{
    struct FileDialogParams;
    // ------------------------------------------------------------------------------
    // 1) LinuxFileDialog — 데스크톱 도구를 동기로 호출한다. 경로의 대소문자는 그대로 둔다
    // ------------------------------------------------------------------------------
    /**
     * @class LinuxFileDialog
     * @brief 외부 데스크톱 다이얼로그 도구를 호출하는 Linux 파일 선택기입니다.
     * @note 반환하는 경로는 I/O 용이라 대소문자를 그대로 둡니다(normalizeSeparators).
     */
    class SW_API LinuxFileDialog final
    {
    public:
        /** @brief 인스턴스를 만들지 않습니다. open 만 부르십시오. */
        LinuxFileDialog() = delete;

        /**
         * @brief 파일 다이얼로그를 동기로 엽니다.
         * @return 사용자가 고르면 true(outListPath 를 채웁니다). 취소하거나 실패하면 false.
         */
        static bool open( const FileDialogParams& params, vector<string>& outListPath );
    };
} // namespace sw

#endif // SW_PLATFORM_LINUX
