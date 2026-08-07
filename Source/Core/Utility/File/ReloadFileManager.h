#pragma once
/**
 * @file ReloadFileManager.h
 * @brief 런타임에 에셋 파일(텍스처, 셰이더 등)의 변경을 감지하고 리로드를 수행하는 시스템입니다.
 */

#include "Core/Common/CommonDefines.h"
#include "Core/Utility/File/IFileWatcher.h"
#include <memory>
#include <string>

namespace sw
{
	/**
	 * @class ReloadFileManager
	 * @brief FileWatcher를 사용하여 리소스 폴더의 파일 변경을 감지하고,
	 * 메인 스레드 업데이트 시 핫 리로드 이벤트를 발생시킵니다.
	 */
	class SW_API ReloadFileManager
	{
	public:
		ReloadFileManager();
		~ReloadFileManager();

		bool initialize();
		void shutdown();

		/**
		 * @brief 매 프레임 업데이트하여 파일 변경 사항을 폴링합니다.
		 */
		void update();

		FileChangeMulticastDelegate& getOnFileChangedEvent() { return _onFileChanged; }

	private:
		std::unique_ptr<IFileWatcher> _fileWatcher;
		FileChangeMulticastDelegate	  _onFileChanged;
	};
}
