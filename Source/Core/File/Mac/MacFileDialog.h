/**
 * @file MacFileDialog.h
 * @brief macOS 네이티브 파일 열기/저장 다이얼로그 (osascript)
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/File/FileUtil.h"

#if defined( SW_PLATFORM_MACOS )

namespace sw
{
	// ------------------------------------------------------------------------------
	// 1) MacFileDialog — osascript 동기 호출. 인스턴스는 두지 않음
	// ------------------------------------------------------------------------------
	/**
	 * @class MacFileDialog
	 * @brief AppleScript를 사용한 macOS 파일 선택기
	 */
	class SW_API MacFileDialog final
	{
	public:
		/** @brief 인스턴스를 두지 않습니다. open 만 호출하세요. */
		MacFileDialog() = delete;
		/** @brief 네이티브 다이얼로그를 열고 선택 경로를 outListPath 에 담습니다. */
		static bool open( const FileDialogParams& params, vector<string>& outListPath );
	};
} // namespace sw

#endif
