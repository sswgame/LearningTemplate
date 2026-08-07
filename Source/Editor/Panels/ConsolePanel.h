#pragma once
/**
 * @file ConsolePanel.h
 * @brief 엔진 로그를 구독·필터링해 표시하는 Output Log 패널
 */
#include "Panels/IEditorPanel.h"
#include "Core/Utility/Delegate/Delegate.h"
#include "Core/Utility/Log/Logger.h"

namespace sw
{
	/** @brief Logger 출력을 실시간으로 보여주는 콘솔 패널 */
	class ConsolePanel : public IEditorPanel
	{
	public:
		ConsolePanel();
		~ConsolePanel() override;

		const char* getWindowTitle() const override { return "Output Log"; }
		/** @brief 필터·레벨 토글·로그 리스트 UI를 그립니다. */
		void		draw( const EditorUIContext& ctx ) override;
		/** @brief 로그 구독을 해제합니다. */
		void		shutdown( IRHIDevice* rhiDevice ) override;

	private:
		/** @brief Logger 콜백: 엔트리를 스레드 안전하게 큐에 넣습니다. */
		void onLogWritten( const Logger::LogEntry& entry );
		/** @brief Logger 구독을 해제합니다. */
		void unsubscribe();

		std::deque<Logger::LogEntry>  _entries;
		std::vector<Logger::LogEntry> _drawSnapshot;
		std::mutex					  _entriesMutex;
		DelegateHandle				  _logListenerHandle;
		char						  _filterBuffer[constant::kMaxBuffer128] = {};

		// ImGui::Checkbox에 주소 전달 — bool 유지
		bool _bAutoScroll		= true;
		bool _levelEnabled[4]	= { true, true, true, true };
		uint8 _bHasNewLogs	  : 1 = 0;
		uint8 _reservedFlags  : 7 = 0;
	};
}
