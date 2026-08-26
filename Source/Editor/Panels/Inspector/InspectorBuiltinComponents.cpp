#include "pch.h"

#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Common/Workspace/EditorWorkspace.h"
#include "Editor/Panels/Inspector/IInspectorComponent.h"
#include "Editor/Panels/Inspector/InspectorBuiltin.h"
#include "Editor/Panels/Inspector/InspectorComponentRegistry.h"

#include "Engine/Object/Component/2D/SpriteComponent.h"
#include "Engine/Object/Component/3D/MeshComponent.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/Component/TagComponent.h"

#include <imgui.h>

namespace sw::editor
{
	namespace
	{
		bool drawTransformInspector( SceneComponent* pSceneComp )
		{
			if ( pSceneComp == nullptr )
				return false;

			ImGui::SeparatorText( "Transform" );

			int32& op = EditorWorkspace::gizmoOperation();
			ImGui::RadioButton( "Translate", &op, 0 );
			ImGui::SameLine();
			ImGui::RadioButton( "Rotate", &op, 1 );
			ImGui::SameLine();
			ImGui::RadioButton( "Scale", &op, 2 );
			bool& bLocal = EditorWorkspace::gizmoLocalSpace();
			ImGui::SameLine();
			ImGui::Checkbox( "Local", &bLocal );

			float3 pos = pSceneComp->getLocalPosition();
			float3 rot = pSceneComp->getLocalRotation();
			float3 scl = pSceneComp->getLocalScale();

			if ( editor::drawVec3Control( "Position", pos, 0.0f, 80.0f, 0.1f ) )
				pSceneComp->setLocalPosition( pos );
			if ( editor::drawVec3Control( "Rotation", rot, 0.0f, 80.0f, 0.5f ) )
				pSceneComp->setLocalRotation( rot );
			if ( editor::drawVec3Control( "Scale", scl, 1.0f, 80.0f, 0.01f ) )
				pSceneComp->setLocalScale( scl );

			const float3 world = pSceneComp->getWorldPosition();
			ImGui::TextDisabled( "World: %.2f, %.2f, %.2f",
								 static_cast<float64>( world._x ),
								 static_cast<float64>( world._y ),
								 static_cast<float64>( world._z ) );
			return true;
		}

		/** @brief SceneComponent 전용 트랜스폼 및 기즈모 컨트롤 */
		class SceneComponentInspector : public IInspectorComponent
		{
		public:
			bool drawBody( Component* pComponent, IRHIDevice* /*pRhiDevice*/ ) override
			{
				return drawTransformInspector( static_cast<SceneComponent*>( pComponent ) );
			}
		};

		/** @brief TagComponent 전용 칩 스타일 */
		class TagComponentInspector : public IInspectorComponent
		{
		public:
			void drawFooter( Component* pComponent, IRHIDevice* /*pRhiDevice*/ ) override
			{
				auto* pTagComp = static_cast<TagComponent*>( pComponent );
				if ( pTagComp == nullptr )
					return;

				const vector<TagID>& listTags = pTagComp->getTags().getTags();
				for ( const TagID& tag : listTags )
				{
					if ( tag._pString != nullptr && tag._pString[0] != '\0' )
					{
						ImGui::SameLine();
						editor::drawChip( tag._pString, editor::style::kOk );
					}
				}
			}
		};

		/** @brief SpriteComponent 전용 프리뷰 및 애셋 슬롯 */
		class SpriteComponentInspector : public IInspectorComponent
		{
		public:
			void drawFooter( Component* /*pComponent*/, IRHIDevice* /*pRhiDevice*/ ) override
			{
				// 스프라이트 퀵 액션
				if ( ImGui::SmallButton( "Open Sprite Clip Tool" ) )
				{
					EditorWorkspace::requestOpenPanel( "Sprite Clip" );
				}
			}
		};

		/** @brief MeshComponent 전용 트랜스폼 + 가시성 */
		class MeshComponentInspector : public IInspectorComponent
		{
		public:
			bool drawBody( Component* pComponent, IRHIDevice* /*pRhiDevice*/ ) override
			{
				MeshComponent* pMeshComp = static_cast<MeshComponent*>( pComponent );
				if ( drawTransformInspector( pMeshComp ) == false )
					return false;

				bool bVisible = pMeshComp->isVisible();
				if ( ImGui::Checkbox( "Visible", &bVisible ) )
					pMeshComp->setVisible( bVisible );
				return true;
			}
		};
	} // namespace

	void registerInspectorBuiltinComponents()
	{
		InspectorComponentRegistry::registerComponent<SceneComponent, SceneComponentInspector>();
		InspectorComponentRegistry::registerComponent<TagComponent, TagComponentInspector>();
		InspectorComponentRegistry::registerComponent<SpriteComponent, SpriteComponentInspector>();
		InspectorComponentRegistry::registerComponent<MeshComponent, MeshComponentInspector>();
	}
} // namespace sw::editor
