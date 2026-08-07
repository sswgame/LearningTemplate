#pragma once
#include "Panels/IEditorPanel.h"

namespace sw
{
	class Material;
	class IRHIDevice;

	class InspectorPanel : public IEditorPanel
	{
	public:
		const char* getWindowTitle() const override { return "RHI & Engine Inspector"; }
		void		draw( const EditorUIContext& ctx ) override;

	private:
		void renderMaterialUI( Material* material, IRHIDevice* rhiDevice );
	};
}
