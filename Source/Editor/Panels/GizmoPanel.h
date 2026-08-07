#pragma once
/**
 * @file GizmoPanel.h
 * @brief ImGuizmo smoke / demo panel (view/proj + transform)
 */
#include "IEditorPanel.h"

namespace sw
{
	class GizmoPanel : public IEditorPanel
	{
	public:
		GizmoPanel();

		const char* getWindowTitle() const override { return "Gizmo"; }
		void		draw( const EditorUIContext& ctx ) override;

	private:
		int	  _operation = 0; ///< 0 translate / 1 rotate / 2 scale
		float _matrix[16]{};
		float _view[16]{};
		float _proj[16]{};
	};
} // namespace sw
