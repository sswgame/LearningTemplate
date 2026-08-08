#pragma once
/**
 * @file ConsoleWindow.h
 * @brief ?”ì§„ ë¡œê·¸ë¥?êµ¬ë…Â·?„í„°ë§í•´ ?œì‹œ?˜ëŠ” Output Log ?¨ë„
 */
#include "Windows/IEditorWindow.h"
#include "Core/Utility/Delegate/Delegate.h"
#include "Core/Utility/Log/Logger.h"

namespace sw
{
	/** @brief Logger ì¶œë ¥???¤ì‹œê°„ìœ¼ë¡?ë³´ì—¬ì£¼ëŠ” ì½˜ì†” ?¨ë„ */
	class ConsoleWindow : public IEditorWindow
	{
	public:
		ConsoleWindow();
		~ConsoleWindow() override;

		const char* getWindowTitle() const override { return "Output Log"; }
		/** @brief ?„í„°Â·?ˆë²¨ ? ê?Â·ë¡œê·¸ ë¦¬ìŠ¤??UIë¥?ê·¸ë¦½?ˆë‹¤. */
		void draw( const EditorUIContext& ctx ) override;
		/** @brief ë¡œê·¸ êµ¬ë…???´ì œ?©ë‹ˆ?? */
		void shutdown( IRHIDevice* rhiDevice ) override;

	private:
		/** @brief Logger ì½œë°±: ?”íŠ¸ë¦¬ë? ?¤ë ˆ???ˆì „?˜ê²Œ ?ì— ?£ìŠµ?ˆë‹¤. */
		void onLogWritten( const Logger::LogEntry& entry );
		/** @brief Logger êµ¬ë…???´ì œ?©ë‹ˆ?? */
		void unsubscribe();

		std::deque<Logger::LogEntry>  _entries;
		std::vector<Logger::LogEntry> _drawSnapshot;
		std::mutex					  _entriesMutex;
		DelegateHandle				  _logListenerHandle;
		char						  _filterBuffer[constant::kMaxBuffer128] = {};

		// ImGui::Checkbox??ì£¼ì†Œ ?„ë‹¬ ??bool ? ì?
		bool				   _bAutoScroll		= true;
		bool				   _levelEnabled[4] = { true, true, true, true };
		uint8				   _bHasNewLogs	  : 1;
		[[maybe_unused]] uint8 _reservedFlags : 7;
	};
} // namespace sw
