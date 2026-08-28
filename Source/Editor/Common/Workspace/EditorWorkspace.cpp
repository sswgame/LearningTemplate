#include "pch.h"

#include "Editor/Common/Workspace/EditorWorkspace.h"

#include "Core/File/FileUtil.h"

#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorTransaction.h"
#include "Editor/Common/Workspace/SelectionManager.h"

#include "Engine/Object/Component/2D/BoxCollider2DComponent.h"
#include "Engine/Object/Component/3D/MeshComponent.h"
#include "Engine/Object/Component/Component.h"
#include "Engine/Object/Component/ComponentPtr.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/GameObjectPtr.h"
#include "Engine/Object/GameObject/ObjectStateSerializer.h"
#include "Engine/Reflection/ReflectionCore.h"
#include "Engine/Serialization/Format/XmlSerializer.h"

#include <algorithm>

namespace sw::editor
{
	namespace
	{
		string componentTypeBaseName( const Component* pComp )
		{
			if ( pComp == nullptr )
				return "Component";

			const TypeInfo* pTypeInfo = pComp->getTypeInfo();
			if ( pTypeInfo != nullptr )
			{
				if ( pTypeInfo->_fullyQualifiedName.empty() == false )
					return pTypeInfo->_fullyQualifiedName.c_str();
			}

			if ( pComp->getComponentName().empty() == false )
				return pComp->getComponentName().c_str();

			return "Component";
		}

		string componentBaseKey( const Component* pComp )
		{
			if ( pComp != nullptr && pComp->getComponentName().empty() == false )
				return pComp->getComponentName().c_str();

			return componentTypeBaseName( pComp );
		}

		string makeStableComponentKey( const Component* pComp, int32 occurrence )
		{
			if ( occurrence <= 0 )
				return componentBaseKey( pComp );

			return componentBaseKey( pComp ) + "#" + to_string( occurrence );
		}

		string computeStableComponentKey( const GameObject* pGameObject, const Component* pTarget )
		{
			if ( pGameObject == nullptr || pTarget == nullptr )
				return {};

			unordered_map<string, int32> mapOccurrence;
			for ( Component* pComp : pGameObject->getAllComponents() )
			{
				if ( pComp == nullptr )
					continue;

				const string base = componentBaseKey( pComp );
				const int32	 occ  = mapOccurrence[base]++;
				if ( pComp == pTarget )
					return makeStableComponentKey( pComp, occ );
			}
			return {};
		}

		Component* findComponentByStableKey( GameObject* pGameObject, string_view key )
		{
			if ( pGameObject == nullptr || key.empty() )
				return nullptr;

			unordered_map<string, int32> mapOccurrence;
			for ( Component* pComp : pGameObject->getAllComponents() )
			{
				if ( pComp == nullptr )
					continue;

				const string base	   = componentBaseKey( pComp );
				const int32	 occ	   = mapOccurrence[base]++;
				const string stableKey = makeStableComponentKey( pComp, occ );
				if ( stableKey == key )
					return pComp;
			}
			return nullptr;
		}
	} // namespace

	// ------------------------------------------------------------------------------
	// Constructor
	// ------------------------------------------------------------------------------
	EditorWorkspace::EditorWorkspace()
		: _selectedComponentId{ 0 }
		, _selectedComponentKey{}
		, _focusedAssetPath{}
		, _inspectMode{ InspectMode::GameObject }
		, _gizmoOperation{ 0 }
		, _pendingOpenPanelTitle{}
		, _pendingScenePath{}
		, _pendingSceneMutex{}
		, _scrollToComponentId{ 0 }
		, _scrollToObjectId{ 0 }
		, _mapGameObjectToPrefab{}
		, _emptyString{}
		, _arrCameraBookmark{}
		, _copiedComponentXml{}
		, _copiedComponentTypeName{}
		, _bGizmoLocalSpace{ true }
		, _bBoneHierarchyPopupOpen{ false }
	{
	}

	// ------------------------------------------------------------------------------
	// Member Methods
	// ------------------------------------------------------------------------------
	uint64 EditorWorkspace::getSelectedObjectId() const
	{
		EditorContext* pContext = EditorContext::get();
		if ( pContext != nullptr )
			return pContext->getSelectionManager().getPrimaryObjectId();
		return 0;
	}

	GameObjectPtr EditorWorkspace::getSelectedObject() const
	{
		EditorContext* pContext = EditorContext::get();
		if ( pContext != nullptr )
			return pContext->getSelectionManager().getPrimaryObject();
		return GameObjectPtr{};
	}

	string EditorWorkspace::getSelectedObjectName() const
	{
		GameObjectPtr pObj = getSelectedObject();
		if ( pObj.isValid() )
			return string{ pObj.get()->getName().c_str() };
		return {};
	}

	void EditorWorkspace::clearSelection()
	{
		EditorContext* pContext = EditorContext::get();
		if ( pContext != nullptr )
			pContext->getSelectionManager().clearAll();
		_selectedComponentId = 0;
		_selectedComponentKey.clear();
	}

	void EditorWorkspace::selectGameObject( GameObjectPtr pObj, SelectionMode mode )
	{
		EditorContext* pContext = EditorContext::get();
		if ( pContext != nullptr )
			pContext->getSelectionManager().selectObject( pObj, mode );
		_selectedComponentId = 0;
		_selectedComponentKey.clear();
		_inspectMode = InspectMode::GameObject;
	}

	void EditorWorkspace::selectComponent( GameObjectPtr pObj, ComponentPtr pComp )
	{
		EditorContext* pContext = EditorContext::get();
		if ( pContext != nullptr )
			pContext->getSelectionManager().selectObject( pObj, SelectionMode::Replace );

		Component* pRawComp = pComp.get();
		if ( pRawComp != nullptr )
			_selectedComponentId = pRawComp->getComponentId();
		else
			_selectedComponentId = 0;

		GameObject* pRawObj = pObj.get();
		if ( pRawObj != nullptr && pRawComp != nullptr )
			_selectedComponentKey = computeStableComponentKey( pRawObj, pRawComp );
		else
			_selectedComponentKey.clear();

		_scrollToComponentId = _selectedComponentId;
		_inspectMode		 = InspectMode::GameObject;
	}

	void EditorWorkspace::remapSelectionByObjectName( GameObjectManager* pGameObjectManager )
	{
		if ( pGameObjectManager == nullptr )
			return;

		const string name = getSelectedObjectName();
		if ( name.empty() )
		{
			_selectedComponentId = 0;
			_selectedComponentKey.clear();
			return;
		}

		GameObject* pObj = pGameObjectManager->findGameObjectByName( hashed_string( name.c_str() ) );
		if ( pObj == nullptr )
		{
			clearSelection();
			return;
		}

		EditorContext* pContext = EditorContext::get();
		if ( pContext != nullptr )
			pContext->getSelectionManager().selectObject( GameObjectPtr{ pObj }, SelectionMode::Replace );

		if ( _selectedComponentKey.empty() )
		{
			_selectedComponentId = 0;
			return;
		}

		Component* pRematerialized = findComponentByStableKey( pObj, _selectedComponentKey );
		if ( pRematerialized != nullptr )
			_selectedComponentId = pRematerialized->getComponentId();
		else
			_selectedComponentId = 0;
	}

	void EditorWorkspace::setFocusedAssetPath( const utf8* pPath )
	{
		_focusedAssetPath = ( pPath != nullptr ) ? pPath : "";
		if ( pPath != nullptr && pPath[0] != '\0' )
		{
			EditorContext* pContext = EditorContext::get();
			if ( pContext != nullptr )
				pContext->getSelectionManager().selectAsset( pPath, SelectionMode::Replace );
		}
	}

	void EditorWorkspace::requestOpenPanel( const utf8* pTitle )
	{
		_pendingOpenPanelTitle = ( pTitle != nullptr ) ? pTitle : "";
	}

	bool EditorWorkspace::consumeOpenPanel( string& outTitle )
	{
		if ( _pendingOpenPanelTitle.empty() )
			return false;
		outTitle = _pendingOpenPanelTitle;
		_pendingOpenPanelTitle.clear();
		return true;
	}

	void EditorWorkspace::requestLoadScene( string_view path )
	{
		std::scoped_lock<mutex> lock{ _pendingSceneMutex };
		_pendingScenePath = path;
	}

	bool EditorWorkspace::consumeLoadScene( string& outPath )
	{
		std::scoped_lock<mutex> lock{ _pendingSceneMutex };
		if ( _pendingScenePath.empty() )
			return false;
		outPath = _pendingScenePath;
		_pendingScenePath.clear();
		return true;
	}

	void EditorWorkspace::setGameObjectPrefabPath( uint64 objectId, string_view prefabPath )
	{
		if ( objectId == 0 )
			return;
		if ( prefabPath.empty() )
			_mapGameObjectToPrefab.erase( objectId );
		else
			_mapGameObjectToPrefab[objectId] = string{ prefabPath };
	}

	const string& EditorWorkspace::getGameObjectPrefabPath( uint64 objectId ) const
	{
		const auto it = _mapGameObjectToPrefab.find( objectId );
		if ( it != _mapGameObjectToPrefab.end() )
			return it->second;
		return _emptyString;
	}

	bool EditorWorkspace::isGameObjectPrefabInstance( uint64 objectId ) const
	{
		const auto it = _mapGameObjectToPrefab.find( objectId );
		return ( it != _mapGameObjectToPrefab.end() && it->second.empty() == false );
	}

	void EditorWorkspace::setCameraBookmark( uint32 slot, const CameraBookmark& bookmark )
	{
		if ( slot < _arrCameraBookmark.size() )
		{
			_arrCameraBookmark[slot]		 = bookmark;
			_arrCameraBookmark[slot]._bValid = true;
		}
	}

	const CameraBookmark* EditorWorkspace::getCameraBookmark( uint32 slot ) const
	{
		if ( slot < _arrCameraBookmark.size() && _arrCameraBookmark[slot]._bValid )
			return &_arrCameraBookmark[slot];
		return nullptr;
	}

	bool EditorWorkspace::hasCameraBookmark( uint32 slot ) const
	{
		return slot < _arrCameraBookmark.size() && _arrCameraBookmark[slot]._bValid;
	}

	void EditorWorkspace::clearCameraBookmark( uint32 slot )
	{
		if ( slot < _arrCameraBookmark.size() )
		{
			_arrCameraBookmark[slot]		 = CameraBookmark{};
			_arrCameraBookmark[slot]._bValid = false;
		}
	}

	void EditorWorkspace::copyComponent( const Component* pComp )
	{
		if ( pComp == nullptr || pComp->getTypeInfo() == nullptr )
			return;

		_copiedComponentTypeName = pComp->getComponentName().empty() == false ? pComp->getComponentName().c_str()
																			  : pComp->getTypeInfo()->_name.c_str();
		_copiedComponentXml		 = XmlSerializer::serialize( pComp, *pComp->getTypeInfo() );
	}

	bool EditorWorkspace::hasCopiedComponent() const
	{
		return _copiedComponentXml.empty() == false;
	}

	bool EditorWorkspace::pasteComponentValues( Component* pTargetComp )
	{
		if ( pTargetComp == nullptr || pTargetComp->getTypeInfo() == nullptr || _copiedComponentXml.empty() )
			return false;

		GameObject* const pOwner	= pTargetComp->getOwner();
		const string	  beforeXml = ( pOwner != nullptr ) ? EditorTransaction::captureSnapshot( GameObjectPtr{ pOwner } )
															: string{};

		const bool bSuccess = XmlSerializer::deserialize( pTargetComp, *pTargetComp->getTypeInfo(), _copiedComponentXml );
		if ( bSuccess && pOwner != nullptr )
		{
			const string afterXml = EditorTransaction::captureSnapshot( GameObjectPtr{ pOwner } );
			EditorTransaction::recordModify( GameObjectPtr{ pOwner }, beforeXml, afterXml, "Paste Component Values" );
		}
		return bSuccess;
	}

	Component* EditorWorkspace::pasteComponentAsNew( GameObject* pTargetObj )
	{
		if ( pTargetObj == nullptr || _copiedComponentTypeName.empty() || _copiedComponentXml.empty() )
			return nullptr;

		GameObjectManager* pManager = pTargetObj->getManager();
		if ( pManager == nullptr )
			return nullptr;

		const string beforeXml = EditorTransaction::captureSnapshot( GameObjectPtr{ pTargetObj } );

		Component* pNewComp = pManager->addComponentByName( pTargetObj, hashed_string( _copiedComponentTypeName ) );
		if ( pNewComp != nullptr && pNewComp->getTypeInfo() != nullptr )
		{
			XmlSerializer::deserialize( pNewComp, *pNewComp->getTypeInfo(), _copiedComponentXml );
			const string afterXml = EditorTransaction::captureSnapshot( GameObjectPtr{ pTargetObj } );
			EditorTransaction::recordModify( GameObjectPtr{ pTargetObj }, beforeXml, afterXml,
											 "Paste Component as New" );
			return pNewComp;
		}
		return nullptr;
	}

	bool EditorWorkspace::saveComponentPreset( const Component* pComp, string_view presetName )
	{
		if ( pComp == nullptr || pComp->getTypeInfo() == nullptr || presetName.empty() )
			return false;

		const string presetDir = FileUtil::joinPath( FileUtil::getCurrentPath(), "Resource/game/demo/data/presets" );
		FileUtil::ensureDirectoryExists( presetDir );

		const string compName = pComp->getComponentName().empty() == false ? pComp->getComponentName().c_str()
																		   : pComp->getTypeInfo()->_name.c_str();
		const string fileName = compName + "_" + string{ presetName } + ".preset.xml";
		const string fullPath = FileUtil::joinPath( presetDir, fileName );

		const string xmlData = XmlSerializer::serialize( pComp, *pComp->getTypeInfo() );
		return FileUtil::writeTextFile( fullPath, xmlData );
	}

	bool EditorWorkspace::loadComponentPreset( Component* pComp, string_view presetFilePath )
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

	void EditorWorkspace::snapSelectedToGround()
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
			{
				bottomOffset = scl._y * 0.5f;
			}

			pos._y = bottomOffset;
			pSc->setLocalPosition( pos );

			const string afterXml = EditorTransaction::captureSnapshot( pGoPtr );
			EditorTransaction::recordModify( pGoPtr, beforeXml, afterXml, "Snap to Ground" );
		}
	}

	void EditorWorkspace::alignSelectedObjects( AlignAxis axis, AlignType type )
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

			const float3  pos = pGo->getPrimarySceneComponent()->getWorldPosition();
			const float32 val = ( axis == AlignAxis::X ) ? pos._x : ( ( axis == AlignAxis::Y ) ? pos._y : pos._z );

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

			if ( axis == AlignAxis::X )
				pos._x = targetVal;
			else if ( axis == AlignAxis::Y )
				pos._y = targetVal;
			else
				pos._z = targetVal;

			pSc->setLocalPosition( pos );

			const string afterXml = EditorTransaction::captureSnapshot( pGoPtr );
			EditorTransaction::recordModify( pGoPtr, beforeXml, afterXml, "Align Objects" );
		}
	}

	void EditorWorkspace::distributeSelectedObjects( AlignAxis axis )
	{
		EditorContext* pContext = EditorContext::get();
		if ( pContext == nullptr )
			return;

		SelectionManager&	  selMgr  = pContext->getSelectionManager();
		vector<GameObjectPtr> listSel = selMgr.getSelectedObjects();
		if ( listSel.size() < 3 )
			return;

		std::sort( listSel.begin(), listSel.end(),
				   [axis]( const GameObjectPtr& a, const GameObjectPtr& b )
		{
			if ( a.isValid() == false || b.isValid() == false )
				return false;
			const float3  posA = a->getPrimarySceneComponent()
								   ? a->getPrimarySceneComponent()->getWorldPosition()
								   : float3{};
			const float3  posB = b->getPrimarySceneComponent()
								   ? b->getPrimarySceneComponent()->getWorldPosition()
								   : float3{};
			const float32 valA =
				( axis == AlignAxis::X ) ? posA._x : ( ( axis == AlignAxis::Y ) ? posA._y : posA._z );
			const float32 valB =
				( axis == AlignAxis::X ) ? posB._x : ( ( axis == AlignAxis::Y ) ? posB._y : posB._z );
			return valA < valB;
		} );

		const float3 firstPos = listSel.front()->getPrimarySceneComponent()->getWorldPosition();
		const float3 lastPos  = listSel.back()->getPrimarySceneComponent()->getWorldPosition();

		const float32 minVal =
			( axis == AlignAxis::X ) ? firstPos._x : ( ( axis == AlignAxis::Y ) ? firstPos._y : firstPos._z );
		const float32 maxVal =
			( axis == AlignAxis::X ) ? lastPos._x : ( ( axis == AlignAxis::Y ) ? lastPos._y : lastPos._z );
		const float32 step = ( maxVal - minVal ) / static_cast<float32>( listSel.size() - 1 );

		for ( size_t idx = 0; idx < listSel.size(); ++idx )
		{
			const GameObjectPtr& pGoPtr = listSel[idx];
			if ( pGoPtr.isValid() == false || pGoPtr->getPrimarySceneComponent() == nullptr )
				continue;

			const string	beforeXml = EditorTransaction::captureSnapshot( pGoPtr );
			SceneComponent* pSc		  = pGoPtr->getPrimarySceneComponent();
			float3			pos		  = pSc->getLocalPosition();
			const float32	newVal	  = minVal + step * static_cast<float32>( idx );

			if ( axis == AlignAxis::X )
				pos._x = newVal;
			else if ( axis == AlignAxis::Y )
				pos._y = newVal;
			else
				pos._z = newVal;

			pSc->setLocalPosition( pos );

			const string afterXml = EditorTransaction::captureSnapshot( pGoPtr );
			EditorTransaction::recordModify( pGoPtr, beforeXml, afterXml, "Distribute Objects" );
		}
	}
} // namespace sw::editor
