#pragma once
/**
 * @file EditorUtil.h
 * @brief EditorModule 유틸 (폰트 경로 등 플랫폼 의존 처리)
 */
#include "Core/Common/Types.h"
#include "Core/Common/CommonHeaders.h"

namespace sw
{
	class EditorUtil
	{
	public:
		/** @brief 플랫폼별 시스템 폰트 디렉터리 목록 (존재하는 경로만). */
		static std::vector<std::filesystem::path> getSystemFontsDirectories();

		/**
		 * @brief 에디터 리소스 Fonts → 시스템 Fonts 순으로 폰트 파일을 찾습니다.
		 * @return 존재하면 절대 경로, 없으면 빈 path
		 */
		static std::filesystem::path resolveFontFile( const utf8* fileName );
	};
}
