#include "pch.h"

#include "Editor/Panels/Material/Model/MaterialModel.h"

#include "Core/Container/formatString.h"
#include "Core/Log/Logger.h"

#include "Editor/Common/Commands/Scene/EditorViewportPreview.h"
#include "Editor/Common/Workspace/EditorRenderViews.h"
#include "Editor/Common/Workspace/EditorService.h"

#include "Engine/Asset/AssetManager.h"
#include "Engine/FileSystem/AssetPaths.h"
#include "Engine/Graphics/Material/Material.h"
#include "Engine/Graphics/Material/MaterialCache.h"
#include "Engine/Localization/LocText.h"
#include "Engine/Object/Component/3D/MeshComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Scene/Scene.h"

namespace sw::editor
{
    SW_LOG_CALLER( "MaterialPanel" );

    namespace
    {
        /// @brief 미리보기 씬에 스폰하는 구 오브젝트 이름입니다.
        constexpr const utf8* kPreviewSubjectName = "PreviewSubject";
    } // namespace

    MaterialModel::MaterialModel()
        : _material{ Material::create() }
        , _name{}
        , _shaderPath{}
        , _status{}
        , _previewMaterialPath{}
        , _previewRedMinusBlue{ 0.0f }
        , _bPreviewDrawn{ false }
    {
    }

    ToolAssetLoadResult MaterialModel::loadDocument( string_view path )
    {
        if ( _material->loadFromFile( AssetID{ path } ) == false )
        {
            _status = SW_LOCKEY( "EditorMaterial", "LoadFailedStatus" );
            return ToolAssetLoadResult::Malformed;
        }
        syncNameBuffers();
        _status = SW_LOCKEY( "EditorMaterial", "LoadedStatus" );
        return ToolAssetLoadResult::Loaded;
    }

    bool MaterialModel::saveDocument( string_view path )
    {
        if ( path.empty() )
            return false;
        commitName();
        _material->setShaderPath( _shaderPath.c_str() );
        const std::optional<AbsolutePath> materialFile = editor::getSubsystem<AssetPaths>().toDiskPath( path );
        if ( materialFile.has_value() == false || _material->saveToFile( *materialFile ) == false )
        {
            _status = SW_LOCKEY( "EditorMaterial", "SaveFailedStatus" );
            return false;
        }
        editor::getSubsystem<AssetManager>().getAssetCache<MaterialCache>().reload( AssetID{ path }, editor::getSubsystem<EditorRenderViews>().getRHIDevice() );
        applyLivePreview( path );
        _status = SW_LOCKEY( "EditorMaterial", "SavedStatus" );
        return true;
    }

    string MaterialModel::captureText() const
    {
        return _material->saveToString();
    }

    void MaterialModel::applyText( string_view text, string_view path )
    {
        if ( text.empty() )
            return;
        if ( _material->loadFromXML( text ) == false )
            SW_LOG_WARNING( "Material undo snapshot could not be read - the material is left as it was" );
        syncNameBuffers();
        applyLivePreview( path );
    }

    void MaterialModel::commitName()
    {
        _material->getDesc()._name = _name.c_str();
    }

    void MaterialModel::setShaderPath( string_view shaderPath )
    {
        _shaderPath = shaderPath;
        _material->setShaderPath( _shaderPath.c_str() );
    }

    void MaterialModel::applyLivePreview( string_view path ) const
    {
        EditorViewportPreview::applyMaterial( _material.get(), path );
    }

    void MaterialModel::writeFloats( string& out, const float32* pValue, uint32 count )
    {
        fixed_string<constant::kMaxBuffer128> buf;
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

    void MaterialModel::populateScene( Scene& scene )
    {
        GameObjectManager* pManager = scene.getObjectManager();
        GameObject*        pSubject = pManager != nullptr ? pManager->createGameObject( hashed_string( kPreviewSubjectName ) ) : nullptr;
        MeshComponent*     pMesh    = pSubject != nullptr ? pSubject->addComponent<MeshComponent>() : nullptr;
        if ( pMesh != nullptr )
            pMesh->setMeshID( "Sphere" );
        _previewMaterialPath.clear(); // 새 씬의 구에는 아직 머티리얼이 없다
    }

    void MaterialModel::syncPreviewSubject( Scene* pScene, string_view path )
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

    void MaterialModel::notePreviewDrawn( float32 redMinusBlue )
    {
        _previewRedMinusBlue = redMinusBlue;
        _bPreviewDrawn       = true;
    }

    bool MaterialModel::findPreviewRedMinusBlue( float32& outValue ) const
    {
        if ( _bPreviewDrawn == false )
            return false;
        outValue = _previewRedMinusBlue;
        return true;
    }

    void MaterialModel::syncNameBuffers()
    {
        _name       = _material->getName().c_str();
        _shaderPath = _material->getShaderPath().c_str();
    }

    MeshComponent* MaterialModel::findPreviewSubject( Scene* pScene )
    {
        GameObjectManager* pManager = pScene != nullptr ? pScene->getObjectManager() : nullptr;
        GameObject*        pSubject = pManager != nullptr ? pManager->findGameObjectByName( hashed_string( kPreviewSubjectName ) ) : nullptr;
        return pSubject != nullptr ? pSubject->getComponent<MeshComponent>() : nullptr;
    }
} // namespace sw::editor
