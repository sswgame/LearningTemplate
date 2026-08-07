#pragma once
/**
 * @file InspectorPanel.h
 * @brief 선택 GameObject/Component 리플렉션 인스펙터 + 엔진/머티리얼 섹션
 */
#include "Panels/IEditorPanel.h"
#include "Core/Common/Types.h"

namespace sw
{
	class Material;
	class IRHIDevice;
	class GameObject;
	class Component;
	struct TypeInfo;
	struct PropertyInfo;
	struct FunctionInfo;

	/** @brief Outliner 선택 대상의 속성·메서드를 검사·편집하는 패널 */
	class InspectorPanel : public IEditorPanel
	{
	public:
		const char* getWindowTitle() const override { return "Inspector"; }
		void		draw( const EditorUIContext& ctx ) override;

	private:
		void drawEngineSection( const EditorUIContext& ctx );
		void drawSelectionSection();
		void drawGameObjectHeader( GameObject* obj );
		void drawComponentSection( Component* comp );
		void drawTypeProperties( void* instance, const TypeInfo* typeInfo );
		void drawPropertyWidget( void* instance, const PropertyInfo& prop );
		void drawTypeMethods( void* instance, const TypeInfo* typeInfo );
		void drawSceneComponentExtras( class SceneComponent* sceneComp );
		void renderMaterialUI( Material* material, IRHIDevice* rhiDevice );

		// Scratch buffers for FUNCTION() arg editing (panel-local).
		int32	_argInt[8]{};
		float32 _argFloat[8]{};
		bool	_argBool[8]{};
		char	_argString[8][256]{};
		char	_lastInvokeResult[256]{};
	};
} // namespace sw
