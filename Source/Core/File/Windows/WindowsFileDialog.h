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
        [[nodiscard]] static bool open( const FileDialogParams& params, vector<string>& outListPath );

        /**
         * @brief `OPENFILENAMEW::lpstrFilter` 에 넘길 필터 글을 만듭니다: `설명\0*.a;*.b\0\0`.
         * @details 널이 구분자인 다중 문자열이라 반환값의 `size()` 가 널까지 셉니다. 확장자는 `.png` · `png` 둘 다 `*.png` 가 되고,
         *          빈 확장자는 건너뛰며, 남는 것이 없으면 `*.*` 입니다. 설명이 비면 `All Files` 입니다.
         */
        static wstring makeFilterText( const FileDialogParams& params );
    };
} // namespace sw

#endif
