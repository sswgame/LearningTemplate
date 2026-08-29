#include "pch.h"

#include "Editor/Common/Commands/EditorTransformCommands.h"

#include "Core/File/FileUtil.h"
#include "Core/Math/MathUtil.h"

#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorTransaction.h"
#include "Editor/Common/Workspace/SelectionManager.h"

#include "Engine/Object/Component/2D/BoxCollider2DComponent.h"
#include "Engine/Object/Component/3D/MeshComponent.h"
#include "Engine/Object/Component/Component.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/GameObjectPtr.h"
#include "Engine/Serialization/Format/XmlSerializer.h"
#include "Engine/Utility/Resource/ResourceUtil.h"

#include <algorithm>

namespace sw::editor
{
	namespace
	{
		struct EditorTransformCommandsInternal
		{
			static float32 worldAxisValue( const float3& pos, AlignAxis axis )
			{
				if ( axis == AlignAxis::X )
					return pos._x;
				if ( axis == AlignAxis::Y )
					return pos._y;
				return pos._z;
			}

			static void setLocalAxisValue( float3& pos, AlignAxis axis, float32 value )
			{
				if ( axis == AlignAxis::X )
					pos._x = value;
				else if ( axis == AlignAxis::Y )
					pos._y = value;
				else
					pos._z = value;
			}

			struct AlignSortLess
			{
				AlignAxis _axis{ AlignAxis::X };

				bool operator()( const GameObjectPtr& lhs, const GameObjectPtr& rhs ) const
				{
					if ( lhs.isValid() == false || rhs.isValid() == false )
						return false;
					const SceneComponent* pLeftSc  = lhs->getPrimarySceneComponent();
					const SceneComponent* pRightSc = rhs->getPrimarySceneComponent();
					const float3		  posA	   = pLeftSc != nullptr ? pLeftSc->getWorldPosition() : float3{};
					const float3		  posB	   = pRightSc != nullptr ? pRightSc->getWorldPosition() : float3{};
					return worldAxisValue( posA, _axis ) < worldAxisValue( posB, _axis );
				}
			};
		};
	} // namespace
} // namespace sw::editor

namespace sw::editor
{
	bool EditorTransformCommands::pasteComponentValues( Component* pTargetComp, string_view xml )
	{
		if ( pTargetComp == nullptr || pTargetComp->getTypeInfo() == nullptr || xml.empty() )
			return false;

		GameObject* const pOwner	= pTargetComp->getOwner();
		const string	  beforeXml = ( pOwner != nullptr ) ? EditorTransaction::captureSnapshot( GameObjectPtr{ pOwner } )
															: string{};

		const bool bSuccess = XmlSerializer::deserialize( pTargetComp, *pTargetComp->getTypeInfo(), string{ xml } );
		if ( bSuccess && pOwner != nullptr )
		{
			const string afterXml = EditorTransaction::captureSnapshot( GameObjectPtr{ pOwner } );
			EditorTransaction::recordModify( GameObjectPtr{ pOwner }, beforeXml, afterXml, "Paste Component Values" );
		}
		return bSuccess;
	}

	Component* EditorTransformCommands::pasteComponentAsNew( GameObject* pTargetObj, string_view typeName, string_view xml )
	{
		if ( pTargetObj == nullptr || typeName.empty() || xml.empty() )
			return nullptr;

		GameObjectManager* pManager = pTargetObj->getManager();
		if ( pManager == nullptr )
			return nullptr;

		const string beforeXml = EditorTransaction::captureSnapshot( GameObjectPtr{ pTargetObj } );

		Component* pNewComp = pManager->addComponentByName( pTargetObj, hashed_string( string{ typeName }.c_str() ) );
		if ( pNewComp != nullptr && pNewComp->getTypeInfo() != nullptr )
		{
			XmlSerializer::deserialize( pNewComp, *pNewComp->getTypeInfo(), string{ xml } );
			const string afterXml = EditorTransaction::captureSnapshot( GameObjectPtr{ pTargetObj } );
			EditorTransaction::recordModify( GameObjectPtr{ pTargetObj }, beforeXml, afterXml, "Paste Component as New" );
			return pNewComp;
		}
		return nullptr;
	}

	bool EditorTransformCommands::saveComponentPreset( const Component* pComp, string_view presetName )
	{
		if ( pComp == nullptr || pComp->getTypeInfo() == nullptr || presetName.empty() )
			return false;

		const string presetDir = FileUtil::joinPath( ResourceUtil::getGameFolderPath(), "demo/data/presets" );
		FileUtil::ensureDirectoryExists( presetDir );

		const string compName = pComp->getComponentName().empty() == false ? pComp->getComponentName().c_str()
																		   : pComp->getTypeInfo()->_name.c_str();
		const string fileName = compName + "_" + string{ presetName } + ".preset.xml";
		const string fullPath = FileUtil::joinPath( presetDir, fileName );

		const string xmlData = XmlSerializer::serialize( pComp, *pComp->getTypeInfo() );
		return FileUtil::writeTextFile( fullPath, xmlData );
	}

	bool EditorTransformCommands::loadComponentPreset( Component* pComp, string_view presetFilePath )
	{
		if ( pComp == nullptr || pComp->getTypeInfo() == nullptr || presetFilePath.empty() )
			return false;

		string xmlData;
		if ( FileUtil::readTextFile( presetFilePath, xmlData ) == false )
			return false;

		GameObject* const pOwner	= pComp->getOwner();
		const string	  beforeXml = ( pOwner != nullptr ) ? EditorTransaction::captureSnapshot( GameObjectPtr{ pOwner } )
															: string{};

		const bool bSuccess = XmlSerializer::deserialize( pComp, *pComp->getTypeInfo(), xmlData );
		if ( bSuccess && pOwner != nullptr )
		{
			const string afterXml = EditorTransaction::captureSnapshot( GameObjectPtr{ pOwner } );
			EditorTransaction::recordModify( GameObjectPtr{ pOwner }, beforeXml, afterXml, "Apply Component Preset" );
		}
		return bSuccess;
	}

	void EditorTransformCommands::snapSelectedToGround()
	{
		EditorContext* pContext = EditorContext::get();
		if ( pContext == nullptr )
			return;

		SelectionManager&			 selMgr	 = pContext->getSelectionManager();
		const vector<GameObjectPtr>& listSel = selMgr.getSelectedObjects();

		for ( const GameObjectPtr& pGoPtr : listSel )
		{
			GameObject* pGo = pGoPtr.get();
			if ( pGo == nullptr )
				continue;
			SceneComponent* pSc = pGo->getPrimarySceneComponent();
			if ( pSc == nullptr )
				continue;

			const string beforeXml = EditorTransaction::captureSnapshot( pGoPtr );

			float3		 pos		  = pSc->getWorldPosition();
			const float3 scl		  = pSc->getLocalScale();
			float32		 bottomOffset = 0.0f;

			BoxCollider2DComponent* pBox = pGo->getComponent<BoxCollider2DComponent>();
			if ( pBox != nullptr )
			{
				const float2 boxScl = pBox->getOffsetScaleVec();
				bottomOffset		= boxScl._y * 0.5f;
			}
			MeshComponent* pMesh = pGo->getComponent<MeshComponent>();
			if ( pMesh != nullptr )
				bottomOffset = scl._y * 0.5f;

			pos._y = bottomOffset;
			pSc->setLocalPosition( pos );

			const string afterXml = EditorTransaction::captureSnapshot( pGoPtr );
			EditorTransaction::recordModify( pGoPtr, beforeXml, afterXml, "Snap to Ground" );
		}
	}

	void EditorTransformCommands::alignSelectedObjects( AlignAxis axis, AlignType type )
	{
		EditorContext* pContext = EditorContext::get();
		if ( pContext == nullptr )
			return;

		SelectionManager&			 selMgr	 = pContext->getSelectionManager();
		const vector<GameObjectPtr>& listSel = selMgr.getSelectedObjects();
		if ( listSel.size() < 2 )
			return;

		float32 targetVal = 0.0f;
		if ( type == AlignType::Min )
			targetVal = 1e9f;
		else if ( type == AlignType::Max )
			targetVal = -1e9f;

		float32 sumVal	   = 0.0f;
		uint32	validCount = 0;

		for ( const GameObjectPtr& pGoPtr : listSel )
		{
			GameObject* pGo = pGoPtr.get();
			if ( pGo == nullptr || pGo->getPrimarySceneComponent() == nullptr )
				continue;

			const float32 val = EditorTransformCommandsInternal::worldAxisValue( pGo->getPrimarySceneComponent()->getWorldPosition(), axis );

			if ( type == AlignType::Min )
				targetVal = MathUtil::min( targetVal, val );
			else if ( type == AlignType::Max )
				targetVal = MathUtil::max( targetVal, val );

			sumVal += val;
			validCount++;
		}

		if ( validCount == 0 )
			return;

		if ( type == AlignType::Center )
			targetVal = sumVal / static_cast<float32>( validCount );

		for ( const GameObjectPtr& pGoPtr : listSel )
		{
			GameObject* pGo = pGoPtr.get();
			if ( pGo == nullptr || pGo->getPrimarySceneComponent() == nullptr )
				continue;

			const string	beforeXml = EditorTransaction::captureSnapshot( pGoPtr );
			SceneComponent* pSc		  = pGo->getPrimarySceneComponent();
			float3			pos		  = pSc->getLocalPosition();
			EditorTransformCommandsInternal::setLocalAxisValue( pos, axis, targetVal );
			pSc->setLocalPosition( pos );

			const string afterXml = EditorTransaction::captureSnapshot( pGoPtr );
			EditorTransaction::recordModify( pGoPtr, beforeXml, afterXml, "Align Objects" );
		}
	}

	void EditorTransformCommands::distributeSelectedObjects( AlignAxis axis )
	{
		EditorContext* pContext = EditorContext::get();
		if ( pContext == nullptr )
			return;

		SelectionManager&	  selMgr  = pContext->getSelectionManager();
		vector<GameObjectPtr> listSel = selMgr.getSelectedObjects();
		if ( listSel.size() < 3 )
			return;

		EditorTransformCommandsInternal::AlignSortLess sortLess{};
		sortLess._axis = axis;
		std::sort( listSel.begin(), listSel.end(), sortLess );

		const float3 firstPos = listSel.front()->getPrimarySceneComponent()->getWorldPosition();
		const float3 lastPos  = listSel.back()->getPrimarySceneComponent()->getWorldPosition();

		const float32 minVal = EditorTransformCommandsInternal::worldAxisValue( firstPos, axis );
		const float32 maxVal = EditorTransformCommandsInternal::worldAxisValue( lastPos, axis );
		const float32 step	 = ( maxVal - minVal ) / static_cast<float32>( listSel.size() - 1 );

		for ( size_t idx = 0; idx < listSel.size(); ++idx )
		{
			const GameObjectPtr& pGoPtr = listSel[idx];
			if ( pGoPtr.isValid() == false || pGoPtr->getPrimarySceneComponent() == nullptr )
				continue;

			const string	beforeXml = EditorTransaction::captureSnapshot( pGoPtr );
			SceneComponent* pSc		  = pGoPtr->getPrimarySceneComponent();
			float3			pos		  = pSc->getLocalPosition();
			EditorTransformCommandsInternal::setLocalAxisValue( pos, axis, minVal + step * static_cast<float32>( idx ) );
			pSc->setLocalPosition( pos );

			const string afterXml = EditorTransaction::captureSnapshot( pGoPtr );
			EditorTransaction::recordModify( pGoPtr, beforeXml, afterXml, "Distribute Objects" );
		}
	}
} // namespace sw::editor
