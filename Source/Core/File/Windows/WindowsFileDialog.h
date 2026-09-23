/**
 * @file WindowsFileDialog.h
 * @brief Windows 네이티브 파일 열기 · 저장 다이얼로그입니다(GetOpenFileNameW / GetSaveFileNameW).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#if defined( SW_PLATFORM_WINDOWS )

namespace sw
{
    struct FileDialogParams;
    // ------------------------------------------------------------------------------
    // 1) WindowsFileDialog — 정적 open 만 있다. 인스턴스는 만들지 않는다
    // ------------------------------------------------------------------------------
    /**
     * @class WindowsFileDialog
     * @brief Win32 API 로 구현한 Windows 파일 선택기입니다.
     */
    class SW_API WindowsFileDialog final
    {
    public:
        /** @brief 인스턴스를 만들지 않습니다. open 만 부르십시오. */
        WindowsFileDialog() = delete;
        /** @brief 네이티브 다이얼로그를 열고, 고른 경로를 outListPath 에 담습니다. */
        static bool open( const FileDialogParams& params, vector<string>& outListPath );
    };
} // namespace sw

#endif
