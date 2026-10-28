/**
 * @file ModelImportVrm.cpp
 * @brief 모델 임포트의 VRM 아바타 정보(humanoid · 표정 · 스프링 본 · 1인칭) 읽기와 `.avatar.xml` · 얼굴 리그(`.facial.xml`) · 후처리 리그(`.rig.xml`) 쓰기입니다.
 * @details 값의 해석은 `VrmAvatarImporter`, 런타임 형식으로 옮기기는 `VrmRuntimeAssets` 가 합니다.
 */
#include "pch.h"

#include "Core/Container/StringUtil.h"
#include "Core/Container/VectorUtil.h"
#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"

#include "Editor/Common/Asset/Model/ModelImportUtil.h"
#include "Editor/Common/Asset/Model/ModelImporter.h"
#include "Editor/Common/Asset/Model/VrmAvatarImporter.h"
#include "Editor/Common/Asset/Model/VrmRuntimeAssets.h"

#include "Engine/Animation/Facial/FacialRig.h"
#include "Engine/Animation/Rig/RigAsset.h"
#include "Engine/Animation/Skeletal/Skeleton.h"
#include "Engine/Asset/AssetPaths.h"
#include "Engine/Serialization/JSON/JSONDocument.h"
#include "Engine/Serialization/XML/XMLDocument.h"

namespace sw::editor
{
    SW_LOG_CALLER( "ModelImporter" );

    namespace
    {
        struct ModelImportVrmInternal
        {
            /** @brief 스켈레톤에 없는 본 이름을 한 번씩 모읍니다. 빈 이름(월드 기준)은 건너뜁니다. */
            static void collectMissingBone( const Skeleton& skeleton, const string& bone, vector<string>& inoutListMissing )
            {
                if ( bone.empty() || skeleton.findBoneIndex( hashed_string( bone ) ) >= 0 )
                    return;
                if ( VectorUtil::contains( inoutListMissing, bone ) == false )
                    inoutListMissing.push_back( bone );
            }

            /** @brief 프리팹 `_listComponent` 에 컴포넌트 원소 하나(`_componentName` · `_bActive` 까지)를 더합니다. */
            static XMLNode appendComponent( const XMLNode& listComponent, const utf8* pTypeName )
            {
                const XMLNode component = listComponent.appendChild( pTypeName );
                component.appendAttribute( "_componentName", pTypeName );
                component.appendAttribute( "_bActive", "true" );
                return component;
            }

            static string joinNames( const vector<string>& listName )
            {
                string joined;
                for ( const string& name : listName )
                {
                    joined += ( joined.empty() ? "" : ", " ) + name;
                }
                return joined;
            }
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    void ModelImportUtil::populateVrmGltfNames( const cgltf_data& data, VrmGltfNames& outNames )
    {
        outNames = VrmGltfNames{};
        outNames._listNodeName.reserve( data.nodes_count );
        outNames._listNodeMesh.reserve( data.nodes_count );
        outNames._listNodeChild.resize( data.nodes_count );
        for ( cgltf_size nodeIndex = 0; nodeIndex < data.nodes_count; ++nodeIndex )
        {
            const cgltf_node& node = data.nodes[nodeIndex];
            outNames._listNodeName.push_back( makeNodeName( data, node ) );
            outNames._listNodeMesh.push_back( node.mesh != nullptr ? static_cast<int32>( node.mesh - data.meshes ) : -1 );
            for ( cgltf_size childIndex = 0; childIndex < node.children_count; ++childIndex )
            {
                if ( node.children[childIndex] != nullptr )
                    outNames._listNodeChild[nodeIndex].push_back( static_cast<uint32>( node.children[childIndex] - data.nodes ) );
            }
        }
        outNames._listMeshName.reserve( data.meshes_count );
        outNames._listMeshMorphName.resize( data.meshes_count );
        for ( cgltf_size meshIndex = 0; meshIndex < data.meshes_count; ++meshIndex )
        {
            const cgltf_mesh& mesh = data.meshes[meshIndex];
            outNames._listMeshName.push_back( mesh.name != nullptr ? string( mesh.name ) : string{} );
            // 모프 가중치 커브(readMorphWeightCurves)와 같이 첫 프리미티브의 타깃 수가 메시의 타깃 수다(glTF 는 프리미티브마다 같은 수를 요구한다).
            const cgltf_size targetCount = mesh.primitives_count > 0 ? mesh.primitives[0].targets_count : 0;
            for ( cgltf_size targetIndex = 0; targetIndex < targetCount; ++targetIndex )
            {
                outNames._listMeshMorphName[meshIndex].push_back( string( makeMorphTargetName( &mesh, targetIndex ).c_str() ) );
            }
        }
    }

    bool ModelImportUtil::readVrmAvatar( const cgltf_data& data, string_view sourcePath, ModelImportResult& inoutResult )
    {
        inoutResult._avatar = VrmAvatarDef{};
        inoutResult._listIgnoredAvatarKey.clear();
        if ( data.json == nullptr || data.json_size == 0 )
            return true;
        JSONDocument document;
        if ( document.parse( string_view( data.json, data.json_size ) ) == false )
            return true; // cgltf 가 이미 읽은 JSON 이다 — 못 읽으면 VRM 이 아닌 것으로 본다(readToonMaterials 와 같다)
        const JSONValue extensions = document.getRoot().get( "extensions" );
        const JSONValue vrm0       = extensions.isObject() ? extensions.get( "VRM" ) : JSONValue{};
        const JSONValue vrm1       = extensions.isObject() ? extensions.get( "VRMC_vrm" ) : JSONValue{};
        if ( vrm0.isObject() == false && vrm1.isObject() == false )
            return true;

        VrmGltfNames names;
        populateVrmGltfNames( data, names );
        string error;
        bool   bRead = false;
        if ( vrm1.isObject() )
        {
            const JSONValue springBone = extensions.get( "VRMC_springBone" );
            bRead                      = VrmAvatarImporter::readVrm1Avatar( vrm1, springBone, names, inoutResult._avatar, &inoutResult._listIgnoredAvatarKey, error );
        }
        else
        {
            bRead = VrmAvatarImporter::readVrm0Avatar( vrm0, names, inoutResult._avatar, &inoutResult._listIgnoredAvatarKey, error );
        }
        if ( bRead == false )
        {
            SW_LOG_ERROR( "VRM '%#': %#", sourcePath, error.c_str() );
            return false;
        }
        return true;
    }

    void ModelImportUtil::warnMissingAvatarBones( const ModelImportResult& result, string_view sourcePath )
    {
        using Internal = ModelImportVrmInternal;
        if ( result._avatar.isEmpty() || result._bSkinned == SW_FALSE )
            return;
        const Skeleton& skeleton = result._skeleton;
        vector<string>  listMissing;
        for ( const VrmHumanBone& bone : result._avatar._listHumanBone )
        {
            Internal::collectMissingBone( skeleton, bone._bone, listMissing );
        }
        for ( const VrmSpring& spring : result._avatar._listSpring )
        {
            Internal::collectMissingBone( skeleton, spring._centerBone, listMissing );
            for ( const VrmSpringJoint& joint : spring._listJoint )
            {
                Internal::collectMissingBone( skeleton, joint._bone, listMissing );
            }
        }
        for ( const VrmSpringCollider& collider : result._avatar._listCollider )
        {
            Internal::collectMissingBone( skeleton, collider._bone, listMissing );
        }
        Internal::collectMissingBone( skeleton, result._avatar._firstPersonBone, listMissing );
        if ( listMissing.empty() == false )
            SW_LOG_WARNING( "VRM '%#': avatar names nodes that are not skin joints (%# bones): %#", sourcePath, listMissing.size(), Internal::joinNames( listMissing ).c_str() );
    }

    bool ModelImportUtil::writeVrmAvatar( const ModelImportResult& result, string_view sourcePath, string_view sideFolder, string_view stem )
    {
        using Internal = ModelImportVrmInternal;
        if ( result._avatar.isEmpty() )
            return true;
        if ( result._listIgnoredAvatarKey.empty() == false )
            SW_LOG_WARNING( "VRM '%#': avatar values that are not imported (ignored): %#", sourcePath, Internal::joinNames( result._listIgnoredAvatarKey ).c_str() );
        const string avatarPath = FileUtil::joinPath( sideFolder, string( stem ) + string( VrmAvatarImporter::kExtension ) );
        if ( FileUtil::ensureDirectoryExists( sideFolder ) == false || FileUtil::writeTextFile( avatarPath, VrmAvatarImporter::makeAvatarXML( result._avatar ) ) == false )
        {
            SW_LOG_ERROR( "Failed to write avatar %#", avatarPath.c_str() );
            return false;
        }
        SW_LOG_INFO( "VRM '%#': avatar with %# human bones, %# expressions, %# springs -> %#", sourcePath, result._avatar._listHumanBone.size(),
                     result._avatar._listExpression.size(), result._avatar._listSpring.size(), avatarPath.c_str() );

        // 런타임이 읽는 형식 — 표정은 얼굴 리그, 스프링 본은 후처리 리그(VrmRuntimeAssets). 캐릭터는 이 파일을 다른 얼굴 리그 · 리그처럼 참조한다.
        vector<string> listNotConverted;
        if ( VrmRuntimeAssets::hasFacialRig( result._avatar ) )
        {
            const string facialPath = FileUtil::joinPath( sideFolder, string( stem ) + string( FacialRig::kExtension ) );
            if ( FileUtil::writeTextFile( facialPath, VrmRuntimeAssets::makeFacialRigXML( result._avatar, &listNotConverted ) ) == false )
            {
                SW_LOG_ERROR( "Failed to write facial rig %#", facialPath.c_str() );
                return false;
            }
            SW_LOG_INFO( "VRM '%#': expressions -> %#", sourcePath, facialPath.c_str() );
        }
        const bool bHasSpringRig = result._bSkinned != SW_FALSE && VrmRuntimeAssets::hasSpringRig( result._avatar, result._skeleton );
        if ( bHasSpringRig )
        {
            const string rigPath = FileUtil::joinPath( sideFolder, string( stem ) + string( RigAsset::kExtension ) );
            if ( FileUtil::writeTextFile( rigPath, VrmRuntimeAssets::makeSpringRigXML( result._avatar, result._skeleton, &listNotConverted ) ) == false )
            {
                SW_LOG_ERROR( "Failed to write spring rig %#", rigPath.c_str() );
                return false;
            }
            SW_LOG_INFO( "VRM '%#': springs -> %#", sourcePath, rigPath.c_str() );
        }
        if ( listNotConverted.empty() == false )
            SW_LOG_WARNING( "VRM '%#': avatar values the runtime formats cannot hold (not converted): %#", sourcePath, Internal::joinNames( listNotConverted ).c_str() );
        return true;
    }

    vector<MeshSection> ModelImportUtil::makeMeshSections( const vector<ModelImportSection>& listSection )
    {
        vector<MeshSection> listMeshSection;
        listMeshSection.reserve( listSection.size() );
        for ( const ModelImportSection& section : listSection )
        {
            listMeshSection.push_back( MeshSection{ hashed_string( section._sectionName ), section._firstVertex, section._vertexCount, section._personVisibility } );
        }
        return listMeshSection;
    }

    bool ModelImportUtil::writeCharacterPrefab( const ModelImportResult& result, string_view meshPath, string_view sideFolder, string_view stem )
    {
        using Internal = ModelImportVrmInternal;
        if ( result._listSection.empty() || result._bSkinned == SW_FALSE )
            return true;
        auto resourceOf = [&]( string_view extension )
        { return AssetPaths::toAssetID( FileUtil::joinPath( sideFolder, string( stem ) + string( extension ) ) ); };
        auto materialOf = [&]( const ModelImportSection& section )
        { return AssetPaths::toAssetID( FileUtil::joinPath( FileUtil::joinPath( sideFolder, kMaterialFolder ), section._fileStem + ".material" ) ); };
        const string meshID       = AssetPaths::toAssetID( meshPath );
        const string skeletonPath = resourceOf( Skeleton::kExtension );

        // 문서 트리로 만들어 엔진 XML 서식으로 저장한다(이스케이프는 문서가 한다) — 저장소의 엔진 형식 파일은 바이트 그대로 왕복해야 한다(XMLAssetMerge).
        XMLDocument   prefabDocument;
        const XMLNode prefab = prefabDocument.appendRoot( "Prefab" );
        prefab.appendAttribute( "formatVersion", "0" );
        prefab.appendAttribute( "name", stem );
        const XMLNode gameObject = prefab.appendChild( "GameObject" );
        gameObject.appendAttribute( "_schemaVersion", "0" );
        gameObject.appendAttribute( "_name", stem );
        gameObject.appendAttribute( "_bActive", "true" );
        const XMLNode listComponent = gameObject.appendChild( "_listComponent" );

        // 주 컴포넌트(스켈레탈 메시)가 첫 구간을 그리고, 나머지 구간은 같은 메시 객체를 빌려 그린다(MeshSectionComponent). 구간 컴포넌트는 주 컴포넌트에 붙어 따라 움직인다.
        const ModelImportSection& first        = result._listSection.front();
        const XMLNode             skeletalMesh = Internal::appendComponent( listComponent, "SkeletalMeshComponent" );
        skeletalMesh.appendAttribute( "_meshID", string_view{ meshID } );
        skeletalMesh.appendAttribute( "_materialPath", string_view{ materialOf( first ) } );
        skeletalMesh.appendAttribute( "_sectionName", string_view{ first._sectionName } );
        skeletalMesh.appendAttribute( "_skeletonPath", string_view{ skeletonPath } );
        for ( size_t index = 1; index < result._listSection.size(); ++index )
        {
            const ModelImportSection& section     = result._listSection[index];
            const XMLNode             meshSection = Internal::appendComponent( listComponent, "MeshSectionComponent" );
            meshSection.appendAttribute( "_attachComponent", "SkeletalMeshComponent#0" );
            meshSection.appendAttribute( "_materialPath", string_view{ materialOf( section ) } );
            meshSection.appendAttribute( "_sectionName", string_view{ section._sectionName } );
        }
        if ( VrmRuntimeAssets::hasFacialRig( result._avatar ) )
            Internal::appendComponent( listComponent, "FacialAnimationComponent" ).appendAttribute( "_facialRigPath", string_view{ resourceOf( FacialRig::kExtension ) } );
        if ( VrmRuntimeAssets::hasSpringRig( result._avatar, result._skeleton ) )
            Internal::appendComponent( listComponent, "PoseModifierComponent" ).appendAttribute( "_rigPath", string_view{ resourceOf( RigAsset::kExtension ) } );

        const string prefabPath = FileUtil::joinPath( sideFolder, string( stem ) + ".prefab.xml" );
        const bool   bWritten   = prefabDocument.saveFile( prefabPath ) && writeImportedMeta( prefabPath );
        if ( bWritten == false )
        {
            SW_LOG_ERROR( "Failed to write character prefab %#", prefabPath.c_str() );
            return false;
        }
        SW_LOG_INFO( "Model '%#': character prefab with %# sections -> %#", meshPath, result._listSection.size(), prefabPath.c_str() );
        return true;
    }

    bool ModelImportUtil::containsVrmExtension( const vector<uint8>& bytes )
    {
        const string_view text( reinterpret_cast<const utf8*>( bytes.data() ), bytes.size() );
        return StringUtil::contains( text, "\"VRM\"" ) || StringUtil::contains( text, "VRMC_vrm" );
    }
} // namespace sw::editor
