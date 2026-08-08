#pragma once
/**
 * @file GameViewWindow.h
 * @brief ê²Œì„ ?Œë” ?€ê¹?+ ImGuizmo ë¥??œì‹œ?˜ëŠ” Game View ?¨ë„
 */
#include "Windows/IEditorWindow.h"
#include "Core/Common/Types.h"

namespace sw
{
	/** @brief ê²Œì„ ?„ë ˆ??ë²„í¼ë¥?ImGui ?´ë?ì§€ë¡??œì‹œ?˜ê³  ? íƒ ?¤ë¸Œ?íŠ¸??ê¸°ì¦ˆëª¨ë? ê·¸ë¦¬??ë·°í¬???¨ë„ */
	class GameViewWindow : public IEditorWindow
	{
	public:
		GameViewWindow();

		const char* getWindowTitle() const override { return "Game View"; }
		/** @brief ê²Œì„ ?Œë” ?ìŠ¤ì²˜ì? ? íƒ ?¤ë¸Œ?íŠ¸ ê¸°ì¦ˆëª¨ë? ê·¸ë¦½?ˆë‹¤. */
		void draw( const EditorUIContext& ctx ) override;

	private:
		uint64 pickNearestOnGround( float mouseX, float mouseY, float canvasX, float canvasY, float canvasW, float canvasH ) const;

		float  _view[16]{};
		float  _proj[16]{};
		int	   _operation = 0; ///< 0=Translate, 1=Rotate, 2=Scale
		uint64 _hoverObjectId = 0;
	};
} // namespace sw
