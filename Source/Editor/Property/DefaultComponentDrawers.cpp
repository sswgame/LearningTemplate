#include "pch.h"

#include "Editor/Property/DefaultComponentDrawers.h"

#include "Editor/Property/ComponentDrawerRegistry.h"
#include "Editor/Property/IComponentDrawer.h"
#include "Editor/Widgets/EditorWidgets.h"
#include "Editor/Workspace/EditorWorkspace.h"

#include "Engine/Object/Component/2D/SpriteComponent.h"
#include "Engine/Object/Component/3D/MeshComponent.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/Component/TagComponent.h"

#include <imgui.h>

namespace sw
{
	namespace
	{
		/** @brief SceneComponent 전용 트랜스폼 및 기즈모 컨트롤 드로어 */
		class SceneComponentDrawer : public IComponentDrawer
		{
		public:
			bool drawBody( Component* pComponent, IRHIDevice* /*pRhiDevice*/ ) override
			{
				auto* pSceneComp = static_cast<SceneComponent*>( pComponent );
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

				return true; // 커스텀 바디로 완전 대체
			}
		};

		/** @brief TagComponent 전용 칩 스타일 드로어 */
		class TagComponentDrawer : public IComponentDrawer
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

		/** @brief SpriteComponent 전용 프리뷰 및 애셋 슬롯 드로어 */
		class SpriteComponentDrawer : public IComponentDrawer
		{
		public:
			void drawFooter( Component* /*pComponent*/, IRHIDevice* /*pRhiDevice*/ ) override
			{
				// 스프라이트 퀵 액션
				if ( ImGui::SmallButton( "Open Sprite Clip Tool" ) )
				{
					EditorWorkspace::requestOpenWindow( "Sprite Clip" );
				}
			}
		};

		/** @brief MeshComponent 전용 퀵 액션 드로어 */
		class MeshComponentDrawer : public IComponentDrawer
		{
		public:
			void drawFooter( Component* /*pComponent*/, IRHIDevice* /*pRhiDevice*/ ) override
			{
				ImGui::TextDisabled( "Mesh Component Ready" );
			}
		};
	} // namespace

	void registerDefaultComponentDrawers()
	{
		ComponentDrawerRegistry::registerComponentDrawer<SceneComponent, SceneComponentDrawer>();
		ComponentDrawerRegistry::registerComponentDrawer<TagComponent, TagComponentDrawer>();
		ComponentDrawerRegistry::registerComponentDrawer<SpriteComponent, SpriteComponentDrawer>();
		ComponentDrawerRegistry::registerComponentDrawer<MeshComponent, MeshComponentDrawer>();
	}
} // namespace sw
