/**
 * @file ModelImportVrm.cpp
 * @brief 모델 임포트의 VRM 아바타 정보(humanoid · 표정 · 스프링 본 · 1인칭) 읽기와 `.avatar.json` · 얼굴 리그(`.facial.json`) · 후처리 리그(`.rig.json`) 쓰기입니다.
 * @details 값의 해석은 `VrmAvatarImporter`, 런타임 형식으로 옮기기는 `VrmRuntimeAssets` 가 합니다.
 */
#include "pch.h"

#include "Core/Container/StringUtil.h"
#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"

#include "Editor/Common/Asset/ModelImportUtil.h"
#include "Editor/Common/Asset/ModelImporter.h"
#include "Editor/Common/Asset/VrmAvatarImporter.h"
#include "Editor/Common/Asset/VrmRuntimeAssets.h"

#include "Engine/Animation/Facial/FacialRig.h"
#include "Engine/Animation/Rig/RigAsset.h"
#include "Engine/Animation/Skeletal/Skeleton.h"
#include "Engine/Serialization/JSON/JSONDocument.h"

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
                if ( std::find( inoutListMissing.begin(), inoutListMissing.end(), bone ) == inoutListMissing.end() )
                    inoutListMissing.push_back( bone );
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
        const JSONValue extensions = document.getRoot().get( "extensions", false );
        const JSONValue vrm0       = extensions.isObject() ? extensions.get( "VRM", false ) : JSONValue{};
        const JSONValue vrm1       = extensions.isObject() ? extensions.get( "VRMC_vrm", false ) : JSONValue{};
        if ( vrm0.isObject() == false && vrm1.isObject() == false )
            return true;

        VrmGltfNames names;
        populateVrmGltfNames( data, names );
        string error;
        bool   bRead = false;
        if ( vrm1.isObject() )
        {
            const JSONValue springBone = extensions.get( "VRMC_springBone", false );
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
        FileUtil::ensureDirectoryExists( sideFolder );
        if ( FileUtil::writeTextFile( avatarPath, VrmAvatarImporter::makeAvatarJSON( result._avatar ) ) == false )
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
            if ( FileUtil::writeTextFile( facialPath, VrmRuntimeAssets::makeFacialRigJSON( result._avatar, &listNotConverted ) ) == false )
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
            if ( FileUtil::writeTextFile( rigPath, VrmRuntimeAssets::makeSpringRigJSON( result._avatar, result._skeleton, &listNotConverted ) ) == false )
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

    bool ModelImportUtil::containsVrmExtension( const vector<uint8>& bytes )
    {
        const string_view text( reinterpret_cast<const utf8*>( bytes.data() ), bytes.size() );
        return StringUtil::contains( text, "\"VRM\"" ) || StringUtil::contains( text, "VRMC_vrm" );
    }
} // namespace sw::editor
