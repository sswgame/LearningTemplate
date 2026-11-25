#include "pch.h"

#include "Editor/Panels/Material/Model/MaterialViewModel.h"

#include "Core/Log/Logger.h"
#include "Core/String/formatString.h"

#include "Editor/Common/Commands/Scene/EditorViewportPreview.h"
#include "Editor/Common/Workspace/EditorSubsystems.h"
#include "Editor/Common/Workspace/Undo/DocumentEditRecorder.h"

#include "Engine/Content/AssetPaths.h"
#include "Engine/Graphics/Material/Material.h"
#include "Engine/Localization/LocText.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Visual/Component/MeshComponent.h"

namespace sw::editor
{
    namespace
    {
        /// @brief 미리보기 씬에 스폰하는 구 오브젝트 이름입니다.
        constexpr const utf8* kPreviewSubjectName = "PreviewSubject";
    } // namespace

    MaterialViewModel::MaterialViewModel()
        : _material{ Material::create() }
        , _name{}
        , _shaderPath{}
        , _status{}
        , _previewMaterialPath{}
        , _previewRedMinusBlue{ 0.0f }
        , _bPreviewDrawn{ SW_FALSE }
    {
    }

    ToolAssetLoadResult MaterialViewModel::loadDocument( string_view path )
    {
        if ( _material->loadFromAsset( AssetID{ path } ) == false )
        {
            _status = SW_LOCKEY( "EditorMaterial", "LoadFailedStatus" );
            return ToolAssetLoadResult::Malformed;
        }
        syncNameBuffers();
        _status = SW_LOCKEY( "EditorMaterial", "LoadedStatus" );
        return ToolAssetLoadResult::Loaded;
    }

    bool MaterialViewModel::saveDocument( string_view path )
    {
        if ( path.empty() )
            return false;
        commitName();
        _material->setShaderPath( _shaderPath.c_str() );
        const optional<AbsolutePath> materialFile = editor::getSubsystem<AssetPaths>().toDiskPath( path );
        if ( materialFile.has_value() == false || _material->saveToFile( *materialFile ) == false )
        {
            _status = SW_LOCKEY( "EditorMaterial", "SaveFailedStatus" );
            return false;
        }
        applyLivePreview( path ); // 캐시 리로드와 자식 머티리얼의 연쇄는 저장 뒤 `EditorDocumentPanel::onDocumentSaved` 가 한다
        _status = SW_LOCKEY( "EditorMaterial", "SavedStatus" );
        return true;
    }

    string MaterialViewModel::captureText() const
    {
        return _material->saveToString();
    }

    void MaterialViewModel::applyText( string_view text, string_view path )
    {
        if ( text.empty() )
            return;
        if ( _material->loadFromXML( text ) == false )
            SW_LOG_WARNING_ID( "MaterialPanel.MaterialUndoSnapshotCould", "Material undo snapshot could not be read - the material is left as it was" );
        syncNameBuffers();
        applyLivePreview( path );
    }

    void MaterialViewModel::commitName()
    {
        _material->getDesc()._name = _name.c_str();
        recordDocumentEdit( "Edit Material Name" );
    }

    void MaterialViewModel::setShaderPath( string_view shaderPath )
    {
        _shaderPath = shaderPath;
        _material->setShaderPath( _shaderPath.c_str() );
        recordDocumentEdit( "Edit Material Shader" );
    }

    bool MaterialViewModel::setProperty( const MaterialProperty& value )
    {
        MaterialProperty* const pProp = _material->findProperty( hashed_string( value._name.c_str() ) );
        if ( pProp == nullptr )
            return false;
        *pProp = value;
        recordDocumentEdit( "Edit Material Property" );
        return true;
    }

    bool MaterialViewModel::resetProperty( const hashed_string& name )
    {
        MaterialProperty* const pProp = _material->findProperty( name );
        if ( pProp == nullptr )
            return false;
        const bool bHadPath = pProp->_assetPath.empty() == false;
        pProp->_assetPath.clear(); // 텍스처 칸은 경로가 값보다 앞선다 — 비워야 기본값 글이 보인다
        const bool bReset = _material->resetParameterToDefault( name ) || bHadPath;
        if ( bReset )
            recordDocumentEdit( "Edit Material Property" );
        return bReset;
    }

    void MaterialViewModel::applyLivePreview( string_view path ) const
    {
        EditorViewportPreview::applyMaterial( _material.get(), path );
    }

    void MaterialViewModel::writeFloats( string& out, const float32* pValue, uint32 count )
    {
        fixed_string<constant::buffer::kMax128> buf;
        if ( count == 1 )
            formatstring( buf.data(), buf.capacity(), "%g", pValue[0] );
        else if ( count == 2 )
            formatstring( buf.data(), buf.capacity(), "%g,%g", pValue[0], pValue[1] );
        else if ( count == 3 )
            formatstring( buf.data(), buf.capacity(), "%g,%g,%g", pValue[0], pValue[1], pValue[2] );
        else if ( count >= 4 )
            formatstring( buf.data(), buf.capacity(), "%g,%g,%g,%g", pValue[0], pValue[1], pValue[2], pValue[3] );
        out = buf.c_str();
    }

    void MaterialViewModel::populateScene( Scene& scene )
    {
        GameObjectManager* pManager = scene.getObjectManager();
        GameObject*        pSubject = pManager != nullptr ? pManager->createGameObject( hashed_string( kPreviewSubjectName ) ) : nullptr;
        MeshComponent*     pMesh    = pSubject != nullptr ? pSubject->addComponent<MeshComponent>() : nullptr;
        if ( pMesh != nullptr )
            pMesh->setMeshID( "Sphere" );
        _previewMaterialPath.clear(); // 새 씬의 구에는 아직 머티리얼이 없다
    }

    void MaterialViewModel::syncPreviewSubject( Scene* pScene, string_view path )
    {
        if ( path.empty() || string_view( _previewMaterialPath ) == path )
            return;
        MeshComponent* pMesh = findPreviewSubject( pScene );
        if ( pMesh == nullptr )
            return;
        // 캐시의 머티리얼을 바인딩한다 — 편집은 `applyLivePreview` 가 캐시 사본에 옮기므로 구도 따라온다.
        pMesh->setMaterialPath( path );
        _previewMaterialPath = path;
    }

    void MaterialViewModel::recordPreviewDrawn( float32 redMinusBlue )
    {
        _previewRedMinusBlue = redMinusBlue;
        _bPreviewDrawn       = SW_TRUE;
    }

    bool MaterialViewModel::findPreviewRedMinusBlue( float32& outValue ) const
    {
        if ( _bPreviewDrawn == false )
            return false;
        outValue = _previewRedMinusBlue;
        return true;
    }

    void MaterialViewModel::syncNameBuffers()
    {
        _name       = _material->getName().c_str();
        _shaderPath = _material->getShaderPath().c_str();
    }

    MeshComponent* MaterialViewModel::findPreviewSubject( Scene* pScene )
    {
        GameObjectManager* pManager = pScene != nullptr ? pScene->getObjectManager() : nullptr;
        GameObject*        pSubject = pManager != nullptr ? pManager->findGameObjectByName( hashed_string( kPreviewSubjectName ) ) : nullptr;
        return pSubject != nullptr ? pSubject->getComponent<MeshComponent>() : nullptr;
    }

    void MaterialViewModel::recordDocumentEdit( string_view label ) const
    {
        SW_ASSERT( _pEditRecorder != nullptr ); // 문서 패널이 만들 때 건다(`bindDocumentEditRecorder`)
        _pEditRecorder->notifyEdited( label );
    }
} // namespace sw::editor
