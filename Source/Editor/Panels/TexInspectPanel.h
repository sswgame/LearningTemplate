#pragma once
/**
 * @file TexInspectPanel.h
 * @brief imgui_tex_inspect panel (DX11 / OpenGL backends)
 */
#include "IEditorPanel.h"

namespace ImGuiTexInspect
{
	struct Context;
}

namespace sw
{
	class TexInspectPanel : public IEditorPanel
	{
	public:
		TexInspectPanel();
		~TexInspectPanel() override;

		const char* getWindowTitle() const override { return "Tex Inspect"; }
		void		draw( const EditorUIContext& ctx ) override;
		void		shutdown( IRHIDevice* rhiDevice ) override;

	private:
		void ensureInit( IRHIDevice* rhiDevice );
		void destroyBackend();

		bool					   _bOpen		  = true;
		bool					   _bInited		  = false;
		bool					   _bUnsupported  = false;
		int						   _backendKind	  = 0; ///< 0 none, 1 dx11, 2 gl
		ImGuiTexInspect::Context*  _context		  = nullptr;
	};
} // namespace sw
