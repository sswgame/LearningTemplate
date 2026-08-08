#pragma once
/**
 * @file LinuxFileDialog.h
 * @brief Linux 네이티브 파일 열기/저장 다이얼로그 (zenity / kdialog / yad)
 */

#include "Core/Utility/File/FileUtil.h"
#include "Core/Common/CommonHeaders.h"

#if defined( SW_PLATFORM_LINUX )

namespace sw
{
	/**
	 * @class LinuxFileDialog
	 * @brief 외부 데스크톱 다이얼로그 도구를 호출하는 Linux 파일 선택기
	 * @note 반환 경로는 I/O용으로 대소문자를 유지한다 (normalizeSeparators).
	 */
	class SW_API LinuxFileDialog final
	{
	public:
		LinuxFileDialog() = delete;

		/**
		 * @brief 파일 다이얼로그를 동기적으로 엽니다.
		 * @return 사용자가 선택하면 true (outPaths 채움). 취소/실패면 false.
		 */
		static bool open( const FileDialogParams& params, std::vector<std::string>& outPaths );
	};
} // namespace sw

#endif // SW_PLATFORM_LINUX
