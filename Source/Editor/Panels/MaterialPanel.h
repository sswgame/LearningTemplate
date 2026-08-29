#pragma once
#include "Editor/Common/Gui/EditorDocumentPanel.h"

#include "Engine/Graphics/Material/Material.h"

namespace sw::editor
{
	/** @brief 마스터 머티리얼 XML을 편집합니다. */
	class MaterialPanel : public EditorDocumentPanel
	{
	public:
		MaterialPanel();

		void drawContent() override;
		bool saveDocument() override;

	private:
		string captureDocumentText() const override;
		void   applyDocumentText( string_view text ) override;
		void   loadFromFocusedPath();
		void   syncNameBuffers();
		void   applyLivePreview();

	private:
		Material _material;
		utf8	 _arrName[128];
		utf8	 _arrShaderPath[256];
		string	 _status;
	};
} // namespace sw::editor
