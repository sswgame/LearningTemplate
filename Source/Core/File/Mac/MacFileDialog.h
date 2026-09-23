/**
 * @file MacFileDialog.h
 * @brief macOS 네이티브 파일 열기 · 저장 다이얼로그입니다(osascript).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#if defined( SW_PLATFORM_MACOS )

namespace sw
{
    struct FileDialogParams;
    // ------------------------------------------------------------------------------
    // 1) MacFileDialog — osascript 를 동기로 호출한다. 인스턴스는 만들지 않는다
    // ------------------------------------------------------------------------------
    /**
     * @class MacFileDialog
     * @brief AppleScript 로 구현한 macOS 파일 선택기입니다.
     */
    class SW_API MacFileDialog final
    {
    public:
        /** @brief 인스턴스를 만들지 않습니다. open 만 부르십시오. */
        MacFileDialog() = delete;
        /** @brief 네이티브 다이얼로그를 열고, 고른 경로를 outListPath 에 담습니다. */
        static bool open( const FileDialogParams& params, vector<string>& outListPath );
    };
} // namespace sw

#endif
