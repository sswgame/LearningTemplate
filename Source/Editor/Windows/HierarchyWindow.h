#pragma once
/**
 * @file HierarchyWindow.h
 * @brief ?œì„± ??GameObject / Component ê³„ì¸µ ?„ì›ƒ?¼ì´??
 */
#include "Windows/IEditorWindow.h"

namespace sw
{
	/** @brief SceneManager ?œì„± ?¬ì˜ ?¤ë¸Œ?íŠ¸ ?¸ë¦¬ë¥??œì‹œÂ·? íƒ?©ë‹ˆ?? */
	class HierarchyWindow : public IEditorWindow
	{
	public:
		const char* getWindowTitle() const override { return "Hierarchy"; }
		void		draw( const EditorUIContext& ctx ) override;

	private:
		char _filterBuffer[128]{};
	};
} // namespace sw
