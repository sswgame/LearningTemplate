#pragma once
#include "Panels/IEditorPanel.h"
#include "Core/Utility/Delegate/Delegate.h"
#include "Core/Utility/Log/Logger.h"

namespace sw
{
	class ConsolePanel : public IEditorPanel
	{
	public:
		ConsolePanel();
		~ConsolePanel() override;

		const char* getWindowTitle() const override { return "Output Log"; }
		void		draw( const EditorUIContext& ctx ) override;
		void		shutdown( IRHIDevice* rhiDevice ) override;

	private:
		void onLogWritten( const Logger::LogEntry& entry );
		void unsubscribe();

		char						  _filterBuffer[constant::kMaxBuffer128] = {};
		bool						  _bAutoScroll							 = true;
		bool						  _levelEnabled[4]						 = { true, true, true, true };
		bool						  _bHasNewLogs							 = false;
		DelegateHandle				  _logListenerHandle;
		std::mutex					  _entriesMutex;
		std::deque<Logger::LogEntry>  _entries;
		std::vector<Logger::LogEntry> _drawSnapshot;
	};
}
