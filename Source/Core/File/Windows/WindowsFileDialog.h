/**
 * @file WindowsFileDialog.h
 * @brief Windows 네이티브 파일 열기/저장 다이얼로그 (GetOpenFileNameW / GetSaveFileNameW)
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/File/FileUtil.h"

#if defined( SW_PLATFORM_WINDOWS )

namespace sw
{
	// ------------------------------------------------------------------------------
	// 1) WindowsFileDialog — 정적 open 만. 인스턴스는 두지 않음
	// ------------------------------------------------------------------------------
	/**
	 * @class WindowsFileDialog
	 * @brief Win32 API를 사용한 Windows 파일 선택기
	 */
	class SW_API WindowsFileDialog final
	{
	public:
		/** @brief 인스턴스를 두지 않습니다. open 만 호출하세요. */
		WindowsFileDialog() = delete;
		/** @brief 네이티브 다이얼로그를 열고 선택 경로를 outListPath 에 담습니다. */
		static bool open( const FileDialogParams& params, vector<string>& outListPath );
	};
} // namespace sw

#endif
