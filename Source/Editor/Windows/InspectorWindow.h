#pragma once
/**
 * @file InspectorWindow.h
 * @brief ? íƒ GameObject/Component ë¦¬í”Œ?‰ì…˜ ?¸ìŠ¤?™í„° + ?”ì§„/ë¨¸í‹°ë¦¬ì–¼ ?¹ì…˜
 */
#include "Windows/IEditorWindow.h"
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

	/** @brief Outliner ? íƒ ?€?ì˜ ?ì„±Â·ë©”ì„œ?œë? ê²€??·í¸ì§‘í•˜???¨ë„ */
	class InspectorWindow : public IEditorWindow
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
		void acceptAssetDrop( const char* path );
		void setLastDroppedAsset( const char* path );

		// Scratch buffers for FUNCTION() arg editing (panel-local).
		int32	_argInt[8]{};
		float32 _argFloat[8]{};
		bool	_argBool[8]{};
		char	_argString[8][256]{};
		char	_lastInvokeResult[256]{};
		std::string _lastDroppedAsset;
	};
} // namespace sw
