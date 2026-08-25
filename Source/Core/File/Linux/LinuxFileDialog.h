/**
 * @file LinuxFileDialog.h
 * @brief Linux 네이티브 파일 열기/저장 다이얼로그 (zenity / kdialog / yad)
 */
#pragma once
#include "Core/Common/StdHeaders.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/File/FileUtil.h"

#if defined( SW_PLATFORM_LINUX )

namespace sw
{
	// ------------------------------------------------------------------------------
	// 1) LinuxFileDialog — 데스크톱 도구를 동기 호출. 경로는 대소문자 유지
	// ------------------------------------------------------------------------------
	/**
	 * @class LinuxFileDialog
	 * @brief 외부 데스크톱 다이얼로그 도구를 호출하는 Linux 파일 선택기
	 * @note 반환 경로는 I/O용으로 대소문자를 유지한다 (normalizeSeparators).
	 */
	class SW_API LinuxFileDialog final
	{
	public:
		/** @brief 인스턴스를 두지 않습니다. open 만 호출하세요. */
		LinuxFileDialog() = delete;

		/**
		 * @brief 파일 다이얼로그를 동기적으로 엽니다.
		 * @return 사용자가 선택하면 true (outPaths 채움). 취소/실패면 false.
		 */
		static bool open( const FileDialogParams& params, vector<string>& outPaths );
	};
} // namespace sw

#endif // SW_PLATFORM_LINUX
