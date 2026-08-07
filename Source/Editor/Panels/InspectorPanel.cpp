/**
 * @file InspectorPanel.cpp
 */
#include "Panels/InspectorPanel.h"
#include "EditorSelection.h"
#include "Runtime/EditorUIContext.h"
#include "Core/Common/CoreServices.h"
#include "Core/Game/Scene/SceneManager.h"
#include "Core/Game/Scene/Scene.h"
#include "Core/Object/GameObjectManager.h"
#include "Core/Object/GameObject.h"
#include "Core/Object/Component.h"
#include "Core/Object/SceneComponent.h"
#include "Core/Reflection/ReflectionCore.h"
#include "Core/Graphics/Material/Material.h"
#include "Core/Graphics/RHI/IRHIDevice.h"
#include "Core/Utility/Math/VectorMath.h"
#include "Core/Utility/Task/TaskTypes.h"
#include <imgui.h>

namespace sw
{
	namespace
	{
		const char* propLabel( const PropertyInfo& prop )
		{
			return prop._alias.empty() == false ? prop._alias.c_str() : prop._name.c_str();
		}

		bool typeNameIs( const PropertyInfo& prop, const char* name )
		{
			return std::strcmp( prop._typeName.c_str(), name ) == 0;
		}
	} // namespace

	void InspectorPanel::draw( const EditorUIContext& ctx )
	{
		if ( ImGui::Begin( getWindowTitle(), getOpenPtr() ) == false )
		{
			ImGui::End();
			return;
		}

		drawSelectionSection();
		ImGui::Separator();
		if ( ImGui::CollapsingHeader( "Engine / Material", ImGuiTreeNodeFlags_DefaultOpen ) )
			drawEngineSection( ctx );

		ImGui::End();
	}

	void InspectorPanel::drawEngineSection( const EditorUIContext& ctx )
	{
		if ( ctx.rhiDevice )
			ImGui::Text( "Active RHI Backend : %s", ctx.rhiDevice->getBackendName() );

		if ( ctx.playerSpeed )
			ImGui::SliderFloat( "Player Speed", ctx.playerSpeed, 0.0f, 20.0f );
		if ( ctx.clearColor )
			ImGui::ColorEdit3( "Clear Color", ctx.clearColor );

		if ( ctx.material )
			renderMaterialUI( ctx.material, ctx.rhiDevice );

		if ( ImGui::Button( "Reset Engine Settings" ) )
		{
			if ( ctx.playerSpeed )
				*ctx.playerSpeed = 5.0f;
			if ( ctx.clearColor )
			{
				ctx.clearColor[0] = 0.12f;
				ctx.clearColor[1] = 0.15f;
				ctx.clearColor[2] = 0.18f;
			}

			if ( ctx.material && ctx.rhiDevice )
			{
				ctx.material->loadFromFile( "Material/DefaultMaterial.material" );
				ctx.material->setPropertyData( ctx.rhiDevice, 0, static_cast<uint32>( ctx.material->getBuffer().size() ),
											   ctx.material->getBuffer().data() );
			}
		}
	}

	void InspectorPanel::drawSelectionSection()
	{
		ImGui::TextUnformatted( "Selection" );
		ImGui::Separator();

		if ( editor::selectedObjectId() == 0 )
		{
			ImGui::TextDisabled( "Nothing selected. Use Hierarchy." );
			return;
		}

		Scene* scene = getSceneManager().getActiveScene();
		if ( scene == nullptr || scene->getObjectManager() == nullptr )
		{
			ImGui::TextDisabled( "No active scene." );
			return;
		}

		GameObject* obj = scene->getObjectManager()->findGameObjectById( editor::selectedObjectId() );
		if ( obj == nullptr )
		{
			ImGui::TextDisabled( "Selected object no longer exists." );
			editor::clearSelection();
			return;
		}

		drawGameObjectHeader( obj );

		if ( editor::selectedComponentId() == 0 )
		{
			// GameObject-level: built-ins + reflected type + all components summary
			if ( const TypeInfo* typeInfo = obj->getTypeInfo() )
			{
				ImGui::SeparatorText( "Reflected Properties" );
				drawTypeProperties( obj, typeInfo );
				ImGui::SeparatorText( "Methods" );
				drawTypeMethods( obj, typeInfo );
			}

			ImGui::SeparatorText( "Components" );
			for ( Component* comp : obj->getAllComponents() )
			{
				if ( comp == nullptr )
					continue;
				ImGui::PushID( static_cast<int>( comp->getComponentId() ) );
				const char* name = comp->getComponentName().empty() == false ? comp->getComponentName().c_str() : "Component";
				if ( ImGui::Selectable( name ) )
					editor::selectComponent( obj, comp );
				ImGui::PopID();
			}
		}
		else
		{
			Component* selected = nullptr;
			for ( Component* comp : obj->getAllComponents() )
			{
				if ( comp != nullptr && comp->getComponentId() == editor::selectedComponentId() )
				{
					selected = comp;
					break;
				}
			}

			if ( selected == nullptr )
			{
				ImGui::TextDisabled( "Selected component no longer exists." );
				editor::selectedComponentId() = 0;
				editor::selectedComponentKey().clear();
			}
			else
			{
				if ( ImGui::Button( "Back to GameObject" ) )
					editor::selectGameObject( obj );
				drawComponentSection( selected );
			}
		}
	}

	void InspectorPanel::drawGameObjectHeader( GameObject* obj )
	{
		ImGui::Text( "GameObject  ID: %llu", static_cast<unsigned long long>( obj->getObjectId() ) );

		char nameBuf[256];
		std::snprintf( nameBuf, sizeof( nameBuf ), "%s", obj->getName().c_str() );
		if ( ImGui::InputText( "Name", nameBuf, sizeof( nameBuf ), ImGuiInputTextFlags_EnterReturnsTrue ) )
			obj->setName( hashed_string( nameBuf ) );

		bool bActive = obj->isActive();
		if ( ImGui::Checkbox( "Active", &bActive ) )
			obj->setActive( bActive );

		if ( GameObject* parent = obj->getParent() )
		{
			ImGui::Text( "Parent: %s", parent->getName().c_str() );
			ImGui::SameLine();
			if ( ImGui::SmallButton( "Unparent" ) )
				obj->detachFromParent();
		}
		else
		{
			ImGui::TextDisabled( "Parent: (root)" );
		}
	}

	void InspectorPanel::drawComponentSection( Component* comp )
	{
		ImGui::Text( "Component  ID: %llu", static_cast<unsigned long long>( comp->getComponentId() ) );
		ImGui::Text( "Type: %s", comp->getComponentName().c_str() );

		bool bActive = comp->isActive();
		if ( ImGui::Checkbox( "Active", &bActive ) )
			comp->setActive( bActive );

		if ( SceneComponent* sceneComp = comp->asSceneComponent() )
			drawSceneComponentExtras( sceneComp );

		if ( const TypeInfo* typeInfo = comp->getTypeInfo() )
		{
			ImGui::SeparatorText( "Reflected Properties" );
			drawTypeProperties( comp, typeInfo );
			ImGui::SeparatorText( "Methods" );
			drawTypeMethods( comp, typeInfo );
		}
		else
		{
			ImGui::TextDisabled( "No TypeInfo registered for this component." );
		}
	}

	void InspectorPanel::drawSceneComponentExtras( SceneComponent* sceneComp )
	{
		ImGui::SeparatorText( "Transform" );
		float3 pos = sceneComp->getLocalPosition();
		float3 rot = sceneComp->getLocalRotation();
		float3 scl = sceneComp->getLocalScale();

		if ( ImGui::DragFloat3( "Local Position", &pos._x, 0.1f ) )
			sceneComp->setLocalPosition( pos );
		if ( ImGui::DragFloat3( "Local Rotation", &rot._x, 0.5f ) )
			sceneComp->setLocalRotation( rot );
		if ( ImGui::DragFloat3( "Local Scale", &scl._x, 0.01f ) )
			sceneComp->setLocalScale( scl );

		const float3 world = sceneComp->getWorldPosition();
		ImGui::Text( "World Position: %.2f, %.2f, %.2f",
					 static_cast<double>( world._x ),
					 static_cast<double>( world._y ),
					 static_cast<double>( world._z ) );
	}

	void InspectorPanel::drawTypeProperties( void* instance, const TypeInfo* typeInfo )
	{
		if ( instance == nullptr || typeInfo == nullptr )
			return;

		for ( const PropertyInfo& prop : typeInfo->_propertyList )
		{
			ImGui::PushID( prop._name.c_str() );
			drawPropertyWidget( instance, prop );
			ImGui::PopID();
		}
	}

	void InspectorPanel::drawPropertyWidget( void* instance, const PropertyInfo& prop )
	{
		const char* label = propLabel( prop );

		if ( prop._bIsContainer )
		{
			size_t size = 0;
			if ( prop._containerWrapper != nullptr )
				size = prop._containerWrapper->getSize( prop.getValuePtr<void>( instance ) );
			ImGui::TextDisabled( "%s (container, size=%zu) ??edit skipped", label, size );
			return;
		}

		if ( typeNameIs( prop, "int32" ) || typeNameIs( prop, "int" ) || typeNameIs( prop, "int32_t" ) )
		{
			int32* ptr = prop.getValuePtr<int32>( instance );
			if ( ptr )
				ImGui::DragInt( label, ptr );
			return;
		}
		if ( typeNameIs( prop, "uint32" ) || typeNameIs( prop, "uint32_t" ) || typeNameIs( prop, "unsigned int" ) )
		{
			uint32* ptr = prop.getValuePtr<uint32>( instance );
			if ( ptr == nullptr )
				return;
			int tmp = static_cast<int>( *ptr );
			if ( ImGui::DragInt( label, &tmp, 1.0f, 0 ) )
				*ptr = static_cast<uint32>( tmp );
			return;
		}
		if ( typeNameIs( prop, "int64" ) || typeNameIs( prop, "int64_t" ) || typeNameIs( prop, "long long" ) )
		{
			int tmp = static_cast<int>( *prop.getValuePtr<int64>( instance ) );
			if ( ImGui::DragInt( label, &tmp ) )
				*prop.getValuePtr<int64>( instance ) = static_cast<int64>( tmp );
			return;
		}
		if ( typeNameIs( prop, "float32" ) || typeNameIs( prop, "float" ) )
		{
			float32* ptr = prop.getValuePtr<float32>( instance );
			if ( ptr )
				ImGui::DragFloat( label, ptr, 0.01f );
			return;
		}
		if ( typeNameIs( prop, "float64" ) || typeNameIs( prop, "double" ) )
		{
			float tmp = static_cast<float>( *prop.getValuePtr<float64>( instance ) );
			if ( ImGui::DragFloat( label, &tmp, 0.01f ) )
				*prop.getValuePtr<float64>( instance ) = static_cast<float64>( tmp );
			return;
		}
		if ( typeNameIs( prop, "bool" ) )
		{
			bool* ptr = prop.getValuePtr<bool>( instance );
			if ( ptr )
				ImGui::Checkbox( label, ptr );
			return;
		}
		if ( typeNameIs( prop, "std::string" ) || typeNameIs( prop, "string" ) )
		{
			std::string* ptr = prop.getValuePtr<std::string>( instance );
			if ( ptr == nullptr )
				return;
			char buf[512];
			std::snprintf( buf, sizeof( buf ), "%s", ptr->c_str() );
			if ( ImGui::InputText( label, buf, sizeof( buf ) ) )
				*ptr = buf;
			return;
		}
		if ( typeNameIs( prop, "sw::float3" ) || typeNameIs( prop, "float3" ) )
		{
			float3* ptr = prop.getValuePtr<float3>( instance );
			if ( ptr )
				ImGui::DragFloat3( label, &ptr->_x, 0.1f );
			return;
		}
		if ( typeNameIs( prop, "sw::float2" ) || typeNameIs( prop, "float2" ) )
		{
			float2* ptr = prop.getValuePtr<float2>( instance );
			if ( ptr )
				ImGui::DragFloat2( label, &ptr->_x, 0.1f );
			return;
		}
		if ( typeNameIs( prop, "sw::float4" ) || typeNameIs( prop, "float4" ) )
		{
			float4* ptr = prop.getValuePtr<float4>( instance );
			if ( ptr )
				ImGui::DragFloat4( label, &ptr->_x, 0.01f );
			return;
		}
		if ( typeNameIs( prop, "sw::hashed_string" ) || typeNameIs( prop, "hashed_string" ) )
		{
			hashed_string* ptr = prop.getValuePtr<hashed_string>( instance );
			if ( ptr == nullptr )
				return;
			char buf[256];
			std::snprintf( buf, sizeof( buf ), "%s", ptr->c_str() );
			if ( ImGui::InputText( label, buf, sizeof( buf ), ImGuiInputTextFlags_EnterReturnsTrue ) )
				*ptr = hashed_string( buf );
			return;
		}

		ImGui::TextDisabled( "%s (%s)", label, prop._typeName.c_str() );
	}

	namespace
	{
		bool isSupportedMethodArgType( const std::string& typeName )
		{
			return typeName == "int32" || typeName == "int" || typeName == "sw::int32" ||
				   typeName == "int64" || typeName == "int64_t" || typeName == "long long" || typeName == "sw::int64" ||
				   typeName == "float32" || typeName == "float" || typeName == "sw::float32" ||
				   typeName == "bool" ||
				   typeName == "std::string" || typeName == "string";
		}

		bool isInt64MethodArgType( const std::string& typeName )
		{
			return typeName == "int64" || typeName == "int64_t" || typeName == "long long" || typeName == "sw::int64";
		}

		bool formatTaskValue( const TaskValue& value, char* outBuf, size_t outSize )
		{
			if ( outBuf == nullptr || outSize == 0 )
				return false;
			outBuf[0] = '\0';

			if ( value.hasValue() == false )
			{
				std::snprintf( outBuf, outSize, "(void / empty)" );
				return true;
			}

			if ( const int32* p = value.getPtr<int32>() )
			{
				std::snprintf( outBuf, outSize, "%d", static_cast<int>( *p ) );
				return true;
			}
			if ( const int* p = value.getPtr<int>() )
			{
				std::snprintf( outBuf, outSize, "%d", *p );
				return true;
			}
			if ( const int64* p = value.getPtr<int64>() )
			{
				std::snprintf( outBuf, outSize, "%lld", static_cast<long long>( *p ) );
				return true;
			}
			if ( const float32* p = value.getPtr<float32>() )
			{
				std::snprintf( outBuf, outSize, "%g", static_cast<double>( *p ) );
				return true;
			}
			if ( const float* p = value.getPtr<float>() )
			{
				std::snprintf( outBuf, outSize, "%g", static_cast<double>( *p ) );
				return true;
			}
			if ( const bool* p = value.getPtr<bool>() )
			{
				std::snprintf( outBuf, outSize, "%s", *p ? "true" : "false" );
				return true;
			}
			if ( const std::string* p = value.getPtr<std::string>() )
			{
				std::snprintf( outBuf, outSize, "%s", p->c_str() );
				return true;
			}

			std::snprintf( outBuf, outSize, "(unsupported return type: %s)", value.type().name() );
			return false;
		}
	} // namespace

	void InspectorPanel::drawTypeMethods( void* instance, const TypeInfo* typeInfo )
	{
		if ( instance == nullptr || typeInfo == nullptr || typeInfo->_methods.empty() )
		{
			ImGui::TextDisabled( "No FUNCTION() methods." );
			return;
		}

		if ( _lastInvokeResult[0] != '\0' )
			ImGui::TextDisabled( "Last result: %s", _lastInvokeResult );

		for ( const FunctionInfo& method : typeInfo->_methods )
		{
			ImGui::PushID( method._name.c_str() );
			ImGui::Text( "%s (%s)", method._name.c_str(),
						 method._returnTypeName.empty() ? "?" : method._returnTypeName.c_str() );

			const uint32 paramCount = static_cast<uint32>( method._paramTypeNames.size() );
			bool		 bArgsOk	= true;
			for ( uint32 i = 0; i < paramCount; ++i )
			{
				if ( i >= 8 || isSupportedMethodArgType( method._paramTypeNames[i] ) == false )
				{
					bArgsOk = false;
					break;
				}
			}

			for ( uint32 i = 0; i < paramCount && i < 8; ++i )
			{
				ImGui::PushID( static_cast<int>( i ) );
				const std::string& p = method._paramTypeNames[i];
				char			   label[64];
				std::snprintf( label, sizeof( label ), "arg%u (%s)", i, p.c_str() );

				if ( p == "int32" || p == "int" || p == "sw::int32" || isInt64MethodArgType( p ) )
					ImGui::InputInt( label, &_argInt[i] );
				else if ( p == "float32" || p == "float" || p == "sw::float32" )
					ImGui::DragFloat( label, &_argFloat[i], 0.1f );
				else if ( p == "bool" )
					ImGui::Checkbox( label, &_argBool[i] );
				else if ( p == "std::string" || p == "string" )
					ImGui::InputText( label, _argString[i], sizeof( _argString[i] ) );
				else
					ImGui::TextDisabled( "%s (unsupported in UI)", label );

				ImGui::PopID();
			}

			if ( paramCount > 8 )
				ImGui::TextDisabled( "Too many arguments (max 8 in UI)." );

			if ( bArgsOk == false )
			{
				ImGui::BeginDisabled();
				ImGui::Button( "Invoke" );
				ImGui::EndDisabled();
				ImGui::TextDisabled( "Unsupported FUNCTION args ??invoke skipped." );
			}
			else if ( ImGui::Button( "Invoke" ) )
			{
				TaskArgs args;
				for ( uint32 i = 0; i < paramCount && i < 8; ++i )
				{
					const std::string& p = method._paramTypeNames[i];
					if ( p == "int32" || p == "int" || p == "sw::int32" )
						args.add( int32{ _argInt[i] } );
					else if ( isInt64MethodArgType( p ) )
						args.add( int64{ _argInt[i] } );
					else if ( p == "float32" || p == "float" || p == "sw::float32" )
						args.add( _argFloat[i] );
					else if ( p == "bool" )
						args.add( _argBool[i] );
					else if ( p == "std::string" || p == "string" )
						args.add( std::string( _argString[i] ) );
				}

				const TaskValue result = getTypeRegistry().invokeMethod(
					instance, typeInfo->_fullyQualifiedName, method._hashName, args );
				formatTaskValue( result, _lastInvokeResult, sizeof( _lastInvokeResult ) );
			}

			ImGui::PopID();
		}
	}

	void InspectorPanel::renderMaterialUI( Material* material, IRHIDevice* rhiDevice )
	{
		if ( material == nullptr || rhiDevice == nullptr )
			return;

		ImGui::PushID( material );

		const auto& props  = material->getProperties();
		const auto& buffer = material->getBuffer();

		bool			   bChanged	  = false;
		std::vector<uint8> tempBuffer = buffer;

		for ( const auto& prop : props )
		{
			ImGui::PushID( prop.name.c_str() );

			if ( prop.type == MaterialPropertyType::Float )
			{
				float32* ptr = reinterpret_cast<float32*>( tempBuffer.data() + prop.offset );
				if ( ImGui::DragFloat( prop.name.c_str(), ptr, 0.01f ) )
					bChanged = true;
			}
			else if ( prop.type == MaterialPropertyType::Float2 )
			{
				float32* ptr = reinterpret_cast<float32*>( tempBuffer.data() + prop.offset );
				if ( ImGui::DragFloat2( prop.name.c_str(), ptr, 0.01f ) )
					bChanged = true;
			}
			else if ( prop.type == MaterialPropertyType::Float3 )
			{
				float32* ptr = reinterpret_cast<float32*>( tempBuffer.data() + prop.offset );
				if ( ImGui::DragFloat3( prop.name.c_str(), ptr, 0.01f ) )
					bChanged = true;
			}
			else if ( prop.type == MaterialPropertyType::Float4 )
			{
				float32* ptr = reinterpret_cast<float32*>( tempBuffer.data() + prop.offset );
				if ( ImGui::ColorEdit4( prop.name.c_str(), ptr ) )
					bChanged = true;
			}
			else if ( prop.type == MaterialPropertyType::Float4x4 )
			{
				float32* ptr = reinterpret_cast<float32*>( tempBuffer.data() + prop.offset );
				ImGui::Text( "%s", prop.name.c_str() );
				if ( ImGui::DragFloat4( "##r0", ptr, 0.01f ) )
					bChanged = true;
				if ( ImGui::DragFloat4( "##r1", ptr + 4, 0.01f ) )
					bChanged = true;
				if ( ImGui::DragFloat4( "##r2", ptr + 8, 0.01f ) )
					bChanged = true;
				if ( ImGui::DragFloat4( "##r3", ptr + 12, 0.01f ) )
					bChanged = true;
			}
			else if ( prop.type == MaterialPropertyType::Uint || prop.type == MaterialPropertyType::Int )
			{
				int* ptr = reinterpret_cast<int*>( tempBuffer.data() + prop.offset );
				if ( ImGui::InputInt( prop.name.c_str(), ptr ) )
					bChanged = true;
			}
			else if ( prop.type == MaterialPropertyType::Uint2 || prop.type == MaterialPropertyType::Int2 )
			{
				int* ptr = reinterpret_cast<int*>( tempBuffer.data() + prop.offset );
				if ( ImGui::InputInt2( prop.name.c_str(), ptr ) )
					bChanged = true;
			}
			else if ( prop.type == MaterialPropertyType::Uint3 || prop.type == MaterialPropertyType::Int3 )
			{
				int* ptr = reinterpret_cast<int*>( tempBuffer.data() + prop.offset );
				if ( ImGui::InputInt3( prop.name.c_str(), ptr ) )
					bChanged = true;
			}
			else if ( prop.type == MaterialPropertyType::Uint4 || prop.type == MaterialPropertyType::Int4 )
			{
				int* ptr = reinterpret_cast<int*>( tempBuffer.data() + prop.offset );
				if ( ImGui::InputInt4( prop.name.c_str(), ptr ) )
					bChanged = true;
			}

			ImGui::PopID();
		}

		if ( bChanged )
			material->setPropertyData( rhiDevice, 0, static_cast<uint32>( tempBuffer.size() ), tempBuffer.data() );

		ImGui::PopID();
	}
} // namespace sw
