#pragma once
/**
 * @file InspectorPanel.h
 * @brief RHI·엔진·머티리얼 속성 검사기 패널
 */
#include "Panels/IEditorPanel.h"

namespace sw
{
	class Material;
	class IRHIDevice;

	/** @brief 선택 리소스/머티리얼 속성을 검사·편집하는 패널 */
	class InspectorPanel : public IEditorPanel
	{
	public:
		const char* getWindowTitle() const override { return "RHI & Engine Inspector"; }
		/** @brief 인스펙터 UI를 그립니다. */
		void draw( const EditorUIContext& ctx ) override;

	private:
		/** @brief Material 파라미터 UI를 그립니다. */
		void renderMaterialUI( Material* material, IRHIDevice* rhiDevice );
	};
} // namespace sw
