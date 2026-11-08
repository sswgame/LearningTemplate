/**
 * @file ModelImporter.cpp
 * @brief 모델 임포트에서 스킨드 모델과 클립 읽기, 공개 API(`ModelImporter`), 일괄 임포트를 구현합니다.
 */
#include "pch.h"

#include "Editor/Common/Asset/Model/ModelImporter.h"

#include "Core/Container/StringUtil.h"
#include "Core/Container/VectorUtil.h"
#include "Core/Container/unordered_map.h"
#include "Core/File/FileUtil.h"
#include "Core/Hash/HashUtil.h"
#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"
#include "Core/Math/VectorMath.h"
#include "Core/Memory/Memory.h"
#include "Core/String/FourCcUtil.h"
#include "Core/String/StringBuilder.h"
#include "Core/Task/TaskManager.h"

#include "Editor/Common/Asset/AssetImportStamp.h"
#include "Editor/Common/Asset/ImportedAssetStore.h"
#include "Editor/Common/Asset/Model/MeshFracture.h"
#include "Editor/Common/Asset/Model/ModelImportAnimation.h"
#include "Editor/Common/Asset/Model/ModelImportConfig.h"
#include "Editor/Common/Asset/Model/ModelImportGeometry.h"
#include "Editor/Common/Asset/Model/ModelImportMaterial.h"
#include "Editor/Common/Asset/Model/ModelImportUtil.h"
#include "Editor/Common/Asset/Model/ModelImportVrm.h"
#include "Editor/Common/Asset/Model/ToonMaterialDef.h"
#include "Editor/Common/Asset/Texture/TextureImporter.h"
#include "Editor/Common/EditorUtil.h"
#include "Editor/Common/Workspace/EditorService.h"

#include "Engine/Animation/AnimationClip.h"
#include "Engine/Animation/AnimationSet.h"
#include "Engine/Animation/Codec/AnimationCodec.h"
#include "Engine/Character/Fit/CharacterMeshUtil.h"
#include "Engine/Config/ConfigManager.h"
#include "Engine/Config/EngineDefaultAssets.h"
#include "Engine/Content/AssetIO.h"
#include "Engine/Content/AssetPaths.h"
#include "Engine/Destruction/Fracture/FractureAsset.h"
#include "Engine/Graphics/Mesh/Mesh.h"
#include "Engine/Graphics/Mesh/MeshAssetFormat.h"
#include "Engine/Graphics/Mesh/MeshCache.h"
#include "Engine/Graphics/RHI/RHITypes.h"
#include "Engine/Serialization/JSON/JSONDocument.h"

#define CGLTF_IMPLEMENTATION
#include <cgltf.h>

namespace sw::editor
{
    namespace
    {
        /**
         * @struct ModelImporterInternal
         * @brief 스킨드 모델과 클립 읽기를 담당합니다. 나머지 도우미는 `ModelImportUtil` · `ModelImportGeometry` · `ModelImportAnimation` · `ModelImportMaterial` · `ModelImportVrm` 에 있습니다.
         */
        struct ModelImporterInternal
        {
            /** @brief 노드 메시의 정점 접근자를 읽지 못했다고 알리고 false 를 돌려줍니다(임포트 실패). */
            static bool reportUnreadableMesh( string_view sourcePath, const cgltf_node& node, const cgltf_data& data )
            {
                SW_LOG_ERROR_ID( "ModelImporter.GlTFMeshNodeVertex", "glTF '%#': the mesh of node '%#' has vertex data that cannot be read", sourcePath, ModelImportUtil::makeNodeName( data, node ).c_str() );
                return false;
            }

            /**
             * @brief 모든 스킨의 관절(노드가 같으면 한 본)로 스켈레톤을 만듭니다. 부모가 앞에 오도록 깊이 우선 순서로 정렬하고, 관절 위의 비관절 조상의
             *        변환은 루트 본에 합칩니다. 역 바인드(메시의 값)는 그 관절을 처음 가진 스킨의 것입니다. 스킨끼리 다르면 경고합니다.
             * @param outMapJointBone 관절 노드에서 본 번호로의 대응입니다.
             * @param outListRootParent 본마다 루트에 합친 행렬(엔진 공간)입니다. 루트가 아니면 단위 행렬이며, 클립 샘플에도 같은 행렬을 곱합니다.
             * @param outListBoneInverseBind 본 순서의 역 바인드입니다.
             */
            static bool buildSkeleton( const cgltf_data& data, Skeleton& outSkeleton, unordered_map<const cgltf_node*, int32>& outMapJointBone,
                                       vector<float4x4>& outListRootParent, vector<float4x4>& outListBoneInverseBind, string_view sourcePath )
            {
                outSkeleton.clear();
                outMapJointBone.clear();
                outListRootParent.clear();
                outListBoneInverseBind.clear();
                unordered_map<const cgltf_node*, uint32> mapJointSlot;
                vector<const cgltf_node*>                listJoint;
                vector<float4x4>                         listInverseBind;
                for ( cgltf_size skinIndex = 0; skinIndex < data.skins_count; ++skinIndex )
                {
                    const cgltf_skin& skin = data.skins[skinIndex];
                    for ( cgltf_size jointIndex = 0; jointIndex < skin.joints_count; ++jointIndex )
                    {
                        // 역 바인드 행렬이 없는 스킨은 단위 행렬이다(glTF 규약). 적었는데 읽지 못하면 단위 행렬로 두지 않고 실패한다.
                        float4x4 inverseBind = float4x4::Identity;
                        float32  arrMatrix[16]{};
                        if ( skin.inverse_bind_matrices != nullptr )
                        {
                            if ( ModelImportUtil::readFloats( skin.inverse_bind_matrices, jointIndex, arrMatrix, 16 ) == false )
                            {
                                SW_LOG_ERROR_ID( "ModelImporter.GlTFInverseBindMatrix", "glTF '%#': inverse bind matrix %# of a skin cannot be read", sourcePath, static_cast<uint32>( jointIndex ) );
                                return false;
                            }
                            inverseBind = ModelImportUtil::convertMatrixToEngineSpace( arrMatrix );
                        }
                        const auto it = mapJointSlot.find( skin.joints[jointIndex] );
                        if ( it == mapJointSlot.end() )
                        {
                            mapJointSlot.emplace( skin.joints[jointIndex], static_cast<uint32>( listJoint.size() ) );
                            listJoint.push_back( skin.joints[jointIndex] );
                            listInverseBind.push_back( inverseBind );
                            continue;
                        }
                        // 한 관절을 두 스킨이 다른 바인드로 쓰면 한 팔레트로 둘 다 맞출 수 없으므로 알린다. 대개는 같다.
                        const float4x4& existing  = listInverseBind[it->second];
                        float32         maxDiffer = 0.0f;
                        for ( uint32 element = 0; element < 16; ++element )
                        {
                            maxDiffer = MathUtil::max( maxDiffer, MathUtil::abs( ( &existing._11 )[element] - ( &inverseBind._11 )[element] ) );
                        }
                        if ( maxDiffer > 1e-3f )
                            SW_LOG_WARNING_ID( "ModelImporter.GlTFJointDifferentBind", "glTF '%#': joint '%#' has different bind matrices in two skins - the first is used", sourcePath,
                                               ModelImportUtil::makeNodeName( data, *skin.joints[jointIndex] ).c_str() );
                    }
                }

                // 깊이 우선으로 순회한다. 관절이 아닌 노드도 지나가지만 본은 관절만 만든다. 부모 본은 가장 가까운 관절 조상이다.
                vector<const cgltf_node*> listStack;
                for ( size_t jointIndex = listJoint.size(); jointIndex > 0; --jointIndex )
                {
                    const cgltf_node* pJoint = listJoint[jointIndex - 1];
                    if ( ModelImportAnimation::findJointAncestorBySlot( *pJoint, mapJointSlot ) == nullptr )
                        listStack.push_back( pJoint );
                }
                while ( listStack.empty() == false )
                {
                    const cgltf_node* pNode = listStack.back();
                    listStack.pop_back();
                    const auto itSlot = mapJointSlot.find( pNode );
                    if ( itSlot != mapJointSlot.end() && outMapJointBone.find( pNode ) == outMapJointBone.end() )
                    {
                        const cgltf_node* pParentJoint = ModelImportAnimation::findJointAncestorBySlot( *pNode, mapJointSlot );
                        const int32       parentBone   = pParentJoint != nullptr ? outMapJointBone[pParentJoint] : -1;
                        BoneTransform     local        = ModelImportUtil::convertTransformToEngineSpace( ModelImportAnimation::readNodeLocal( *pNode ) );
                        float4x4          rootParent   = float4x4::Identity;
                        if ( parentBone < 0 && pNode->parent != nullptr )
                        {
                            float32 arrParentWorld[16]{};
                            cgltf_node_transform_world( pNode->parent, arrParentWorld );
                            rootParent = ModelImportUtil::convertMatrixToEngineSpace( arrParentWorld );
                            local      = BoneTransform::makeFromMatrix( local.toMatrix() * rootParent );
                        }
                        const int32 boneIndex = outSkeleton.addBone( hashed_string( ModelImportUtil::makeNodeName( data, *pNode ) ), parentBone, local );
                        if ( boneIndex < 0 )
                            return false;
                        outListBoneInverseBind.push_back( listInverseBind[itSlot->second] );
                        outMapJointBone.emplace( pNode, boneIndex );
                        outListRootParent.push_back( rootParent );
                    }
                    for ( cgltf_size childIndex = pNode->children_count; childIndex > 0; --childIndex )
                    {
                        if ( pNode->children[childIndex - 1] != nullptr )
                            listStack.push_back( pNode->children[childIndex - 1] );
                    }
                }
                if ( outMapJointBone.size() != listJoint.size() )
                {
                    SW_LOG_ERROR_ID( "ModelImporter.GlTFSkinJointsNot", "glTF '%#': skin joints are not one connected hierarchy reachable from their roots", sourcePath );
                    return false;
                }
                if ( listJoint.size() > 0xFFFFu )
                {
                    SW_LOG_ERROR_ID( "ModelImporter.GlTFJointsDoNot", "glTF '%#': %# joints do not fit 16-bit bone indices", sourcePath, listJoint.size() );
                    return false;
                }
                return true;
            }

            /** @brief 스킨마다 관절 번호 → 본 번호 표를 만듭니다. */
            static vector<ModelImportUtil::SkinBinding> makeSkinBindings( const cgltf_data& data, unordered_map<const cgltf_node*, int32>& inoutMapJointBone )
            {
                vector<ModelImportUtil::SkinBinding> listBinding( data.skins_count );
                for ( cgltf_size skinIndex = 0; skinIndex < data.skins_count; ++skinIndex )
                {
                    const cgltf_skin&             skin    = data.skins[skinIndex];
                    ModelImportUtil::SkinBinding& binding = listBinding[skinIndex];
                    binding._pSkin                        = &skin;
                    binding._listJointToBone.resize( skin.joints_count, -1 );
                    for ( cgltf_size jointIndex = 0; jointIndex < skin.joints_count; ++jointIndex )
                    {
                        binding._listJointToBone[jointIndex] = inoutMapJointBone[skin.joints[jointIndex]];
                    }
                }
                return listBinding;
            }

            /**
             * @brief 관절 아래의 스킨 없는 메시를 부착으로 읽어 결과에 더합니다. 부착은 메시가 노드 로컬 공간에 있고, 변환은 가장 가까운 관절을 기준으로 한다. 사이의 비관절 노드는 곱해서 합친다.
             * @return 메시를 읽지 못하면 오류를 남기고 false 입니다.
             */
            [[nodiscard]] static bool readAttachment( const cgltf_data& data, const cgltf_node& node, const cgltf_node& joint, unordered_map<const cgltf_node*, int32>& inoutMapJointBone,
                                                      unordered_map<string, uint32>& inoutMapStemUse, string_view sourcePath, ModelImportResult& inoutResult )
            {
                float32               arrIdentity[16]{ 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f };
                ModelImportAttachment attachment{};
                attachment._name       = ModelImportUtil::makeNodeName( data, node );
                attachment._parentBone = inoutResult._skeleton.getBone( static_cast<uint32>( inoutMapJointBone[&joint] ) )._name;
                float4x4 local         = float4x4::Identity;
                for ( const cgltf_node* pStep = &node; pStep != nullptr && pStep != &joint; pStep = pStep->parent )
                {
                    float32 arrLocal[16]{};
                    cgltf_node_transform_local( pStep, arrLocal );
                    local = local * ModelImportUtil::convertMatrixToEngineSpace( arrLocal );
                }
                attachment._localTransform = BoneTransform::makeFromMatrix( local );
                const string baseStem      = ModelImportUtil::makeFileStem( attachment._name );
                const uint32 useCount      = inoutMapStemUse[baseStem]++;
                attachment._fileStem       = ModelImportUtil::makeNumberedStem( baseStem, useCount );
                ModelImportUtil::MergedMesh part;
                for ( cgltf_size primitiveIndex = 0; primitiveIndex < node.mesh->primitives_count; ++primitiveIndex )
                {
                    if ( ModelImportGeometry::appendPrimitive( node.mesh->primitives[primitiveIndex], node.mesh, arrIdentity, nullptr, part ) == false )
                        return reportUnreadableMesh( sourcePath, node, data );
                }
                ModelImportGeometry::makeMeshData( part, 0, attachment._mesh );
                if ( attachment._mesh._listVertex.empty() == false )
                    inoutResult._listAttachment.push_back( std::move( attachment ) );
                return true;
            }

            /**
             * @brief 스킨드 모델을 읽습니다. 모든 스킨이 한 스켈레톤을 쓰며, 관절 노드가 같으면 한 본입니다.
             * @param listSectionMaterialName 구간을 나누는 glTF 머티리얼마다의 이름입니다(VRM · 머티리얼이 여럿인 텍스처 glTF). 본 메시를 머티리얼 구간으로도 나눕니다.
             */
            [[nodiscard]] static bool readSkinnedModel( const cgltf_data& data, const vector<const cgltf_node*>& listRoot, const ModelImportRule& rule, string_view sourcePath,
                                                        const vector<string>& listSectionMaterialName, ModelImportResult& outResult )
            {
                unordered_map<const cgltf_node*, int32> mapJointBone;
                vector<float4x4>                        listRootParent;
                vector<float4x4>                        listBoneInverseBind;
                if ( buildSkeleton( data, outResult._skeleton, mapJointBone, listRootParent, listBoneInverseBind, sourcePath ) == false )
                    return false;
                outResult._skeleton.setPartial( rule._bPartialSkeleton == SW_TRUE );
                const uint32 boneCount = outResult._skeleton.getBoneCount();

                const vector<ModelImportUtil::SkinBinding> listBinding = makeSkinBindings( data, mapJointBone );
                // 스킨 없는 메시를 몸에 합칠 때 쓰는 고정 본 매핑이다. 루트 본 0 에 가중치 1 을 준다.
                ModelImportUtil::SkinBinding rigidBinding{};

                // 노드를 순회하며 메시를 나눈다. 이 스킨을 쓰는 메시는 몸, 관절 아래의 스킨 없는 메시는 부착, 그 밖의 스킨 없는 메시는 몸(루트 본에 가중치 1)이다.
                ModelImportUtil::MergedMesh body;
                const bool                  bSections = listSectionMaterialName.empty() == false;
                // 머티리얼(구간 머티리얼 · 텍스처 PBR 머티리얼 하나)이 색 계수를 가지면 정점에 굽지 않는다 — 두 번 곱한다(정적 모델과 같은 규칙)
                const bool bPbrMaterial  = bSections == false && rule._bImportMaterials == SW_TRUE && ModelImportMaterial::findTexturedPbrMaterial( data ) != nullptr;
                body._bBakeMaterialColor = bSections == false && bPbrMaterial == false;
                if ( bPbrMaterial && ModelImportMaterial::readPbrMaterial( data, sourcePath, outResult ) == false )
                    return false;
                vector<const cgltf_node*>     listStack( listRoot.rbegin(), listRoot.rend() );
                unordered_map<string, uint32> mapStemUse;
                float32                       arrIdentity[16]{ 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f };
                while ( listStack.empty() == false )
                {
                    const cgltf_node* pNode = listStack.back();
                    listStack.pop_back();
                    for ( cgltf_size childIndex = pNode->children_count; childIndex > 0; --childIndex )
                    {
                        if ( pNode->children[childIndex - 1] != nullptr )
                            listStack.push_back( pNode->children[childIndex - 1] );
                    }
                    if ( pNode->mesh == nullptr )
                        continue;
                    if ( pNode->skin != nullptr )
                    {
                        // glTF 규약상 스킨드 메시는 노드 변환을 쓰지 않는다. 정점은 바인드 공간에 있다.
                        const ModelImportUtil::SkinBinding& binding = listBinding[static_cast<size_t>( pNode->skin - data.skins )];
                        for ( cgltf_size primitiveIndex = 0; primitiveIndex < pNode->mesh->primitives_count; ++primitiveIndex )
                        {
                            if ( ModelImportGeometry::appendPrimitive( pNode->mesh->primitives[primitiveIndex], pNode->mesh, arrIdentity, &binding, body ) == false )
                                return reportUnreadableMesh( sourcePath, *pNode, data );
                        }
                        continue;
                    }
                    const cgltf_node* pJoint = ModelImportAnimation::findJointAncestor( *pNode, mapJointBone );
                    if ( pJoint == nullptr || rule._bImportAttachments == SW_FALSE )
                    {
                        float32 arrWorld[16]{};
                        cgltf_node_transform_world( pNode, arrWorld );
                        for ( cgltf_size primitiveIndex = 0; primitiveIndex < pNode->mesh->primitives_count; ++primitiveIndex )
                        {
                            if ( ModelImportGeometry::appendPrimitive( pNode->mesh->primitives[primitiveIndex], pNode->mesh, arrWorld, &rigidBinding, body ) == false )
                                return reportUnreadableMesh( sourcePath, *pNode, data );
                        }
                        continue;
                    }
                    if ( readAttachment( data, *pNode, *pJoint, mapJointBone, mapStemUse, sourcePath, outResult ) == false )
                        return false;
                }
                if ( body._listIndex.empty() )
                {
                    SW_LOG_ERROR_ID( "ModelImporter.GlTFSkinButNo", "glTF '%#' has a skin but no triangles bound to it", sourcePath );
                    return false;
                }
                if ( bSections )
                {
                    // 본 메시 한 벌에 (머티리얼, 1인칭 가시성) 구간을 둔다. 구간마다 순서를 최적화했으므로 그 순서대로 펼친다.
                    ModelImportMaterial::populateMaterialSections( body, data, listSectionMaterialName, outResult );
                    if ( outResult._listToonMaterial.empty() == false && ModelImportMaterial::collectSectionTextures( data, sourcePath, outResult ) == false )
                        return false;
                    ModelImportGeometry::makeMeshDataInOrder( body, boneCount, outResult._mesh );
                    outResult._mesh._listSection = ModelImportVrm::makeMeshSections( outResult._listSection );
                }
                else
                {
                    // LOD 는 구간 · 모프 없는 스킨드 메시만 받는다(`Mesh::setLODs`) — 규칙이 맞지 않는 모델에 걸렸으면 LOD 없이 쓰지 않고 임포트를 멈춘다.
                    if ( rule._listLOD.empty() == false && body._listMorphName.empty() == false )
                    {
                        SW_LOG_ERROR_ID( "ModelImporter.GlTFMorphTargetsImport", "glTF '%#' has morph targets - import rule '%#' asks for mesh LODs, which only meshes without morph targets take", sourcePath, rule._name.c_str() );
                        return false;
                    }
                    ModelImportGeometry::makeMeshData( body, boneCount, outResult._mesh );
                    if ( ModelImportGeometry::makeLODLevels( body, rule, sourcePath, boneCount, outResult._mesh._listLOD ) == false )
                        return false;
                }
                if ( rule._listLOD.empty() == false && bSections )
                {
                    SW_LOG_ERROR_ID( "ModelImporter.GlTFMaterialSectionsImport", "glTF '%#' has material sections - import rule '%#' asks for mesh LODs, which only meshes without sections take", sourcePath, rule._name.c_str() );
                    return false;
                }
                outResult._mesh._skinBinding._listInverseBind = std::move( listBoneInverseBind );
                outResult._mesh._skinBinding._layoutHash      = outResult._skeleton.getHierarchyHash( boneCount );
                outResult._bSkinned                           = SW_TRUE;
                return readClips( data, mapJointBone, listRootParent, rule, sourcePath, outResult );
            }

            /** @brief 애니메이션마다 클립 하나를 만들어 규칙의 코덱으로 압축합니다. 규칙의 클립 목록에 원본에 없는 이름이 있으면 false 입니다. */
            [[nodiscard]] static bool readClips( const cgltf_data& data, const unordered_map<const cgltf_node*, int32>& mapJointBone, const vector<float4x4>& listRootParent,
                                                 const ModelImportRule& rule, string_view sourcePath, ModelImportResult& inoutResult )
            {
                if ( rule._bImportAnimations == SW_FALSE )
                    return true;
                const IAnimationCodec* pCodec = AnimationCodecRegistry::findCodecByName( rule._animationCodec );
                if ( pCodec == nullptr )
                {
                    SW_LOG_ERROR_ID( "ModelImporter.GlTFUnknownAnimationCodec", "glTF '%#': unknown animation codec '%#'", sourcePath, rule._animationCodec.c_str() );
                    return false;
                }
                unordered_map<string, ModelImportAnimation::ClipExtra> mapExtra;
                if ( ModelImportAnimation::readClipData( sourcePath, mapExtra ) == false )
                    return false;

                const Skeleton& skeleton       = inoutResult._skeleton;
                int32           rootMotionBone = constant::invalid_index::kInt32; // 클립의 루트 모션 트랙 칸 규약(-1 = 루트 모션 없음)
                if ( rule._rootMotionBone.empty() == false )
                {
                    const optional<uint32> foundBone = skeleton.findBoneIndex( hashed_string( rule._rootMotionBone ) );
                    if ( foundBone.has_value() == false )
                    {
                        SW_LOG_ERROR_ID( "ModelImporter.GlTFRootMotionBone", "glTF '%#': root_motion_bone '%#' is not a joint", sourcePath, rule._rootMotionBone.c_str() );
                        return false;
                    }
                    rootMotionBone = static_cast<int32>( *foundBone );
                }
                // 관절의 레스트 TRS(glTF 공간, 루트에 합치기 전)다. 채널이 없는 성분은 이 값을 쓴다.
                vector<BoneTransform> listRest( skeleton.getBoneCount() );
                for ( const auto& [pNode, boneIndex] : mapJointBone )
                {
                    listRest[static_cast<uint32>( boneIndex )] = ModelImportAnimation::readNodeLocal( *pNode );
                }

                vector<uint8>                 listRequestedFound( rule._listClipName.size(), SW_FALSE );
                unordered_map<string, uint32> mapStemUse;
                AnimationCodecSettings        settings{};
                settings._precision     = rule._animationPrecision;
                settings._shellDistance = rule._animationShellDistance;
                for ( cgltf_size animationIndex = 0; animationIndex < data.animations_count; ++animationIndex )
                {
                    const cgltf_animation& animation = data.animations[animationIndex];
                    const string           clipName  = animation.name != nullptr ? string( animation.name ) : "clip" + to_string( static_cast<uint64>( animationIndex ) );
                    if ( rule._listClipName.empty() == false )
                    {
                        const optional<size_t> requestedIndex = VectorUtil::findIndex( rule._listClipName, clipName );
                        if ( requestedIndex.has_value() == false )
                            continue;
                        listRequestedFound[*requestedIndex] = SW_TRUE;
                    }

                    // 모프 가중치 채널은 타깃 이름의 커브가 된다. 가중치만 움직이는 클립(말하기)도 그 길이를 가진다.
                    vector<AnimationClipCurve> listMorphCurve;
                    float32                    morphDuration = 0.0f;
                    AnimationRawClip           rawClip;
                    const bool                 bChannelsRead = ModelImportAnimation::readMorphWeightCurves( animation, rule._animationSampleRate, listMorphCurve, morphDuration ) &&
                                               ModelImportAnimation::resampleAnimation( animation, skeleton, mapJointBone, listRest, listRootParent, rule._animationSampleRate, morphDuration, rawClip );
                    if ( bChannelsRead == false )
                    {
                        SW_LOG_ERROR_ID( "ModelImporter.GlTFClipChannelWhose", "glTF '%#': clip '%#' has a channel whose keys cannot be read", sourcePath, clipName.c_str() );
                        return false;
                    }
                    ModelImportClip imported{};
                    imported._clip.setName( hashed_string( clipName ) );
                    imported._clip.setRootMotionTrack( rootMotionBone );
                    for ( const AnimationClipCurve& curve : listMorphCurve )
                    {
                        imported._clip.addCurve( curve );
                    }
                    const auto itExtra = mapExtra.find( clipName );
                    if ( itExtra != mapExtra.end() )
                    {
                        applyClipExtra( itExtra->second, imported );
                        mapExtra.erase( itExtra );
                    }
                    if ( imported._clip.compressFrom( rawClip, *pCodec, settings, &imported._stats ) == false )
                    {
                        SW_LOG_ERROR_ID( "ModelImporter.GlTFClipCouldNot", "glTF '%#': clip '%#' could not be compressed with '%#'", sourcePath, clipName.c_str(), pCodec->getName() );
                        return false;
                    }
                    const string baseStem = ModelImportUtil::makeFileStem( clipName );
                    const uint32 useCount = mapStemUse[baseStem]++;
                    imported._fileStem    = ModelImportUtil::makeNumberedStem( baseStem, useCount );
                    inoutResult._listClip.push_back( std::move( imported ) );
                }
                return checkClipNamesMatched( rule, listRequestedFound, mapExtra, sourcePath );
            }

            /** @brief 클립 부가 데이터(루프 덮어쓰기 · 알림 · 커브)를 가져온 클립에 얹습니다. */
            static void applyClipExtra( const ModelImportAnimation::ClipExtra& extra, ModelImportClip& inoutImported )
            {
                if ( extra._loop.has_value() )
                    inoutImported._clip.setLooping( *extra._loop );
                for ( const AnimationNotifyEvent& event : extra._listNotify )
                {
                    inoutImported._clip.addNotify( event );
                }
                for ( const AnimationClipCurve& curve : extra._listCurve )
                {
                    inoutImported._clip.addCurve( curve );
                }
            }

            /**
             * @brief 규칙이 고른 클립이 원본에 모두 있었는지, 부가 데이터가 가리킨 클립이 모두 가져와졌는지 봅니다. 하나라도 어긋나면 이름을 남기고 false 입니다.
             * @details 부가 데이터에 남은 이름은 원본(또는 규칙이 고른 클립)에 없는 클립입니다. 철자가 틀린 알림이 조용히 사라지지 않도록 오류로 처리합니다.
             */
            [[nodiscard]] static bool checkClipNamesMatched( const ModelImportRule& rule, const vector<uint8>& listRequestedFound,
                                                             const unordered_map<string, ModelImportAnimation::ClipExtra>& mapLeftoverExtra, string_view sourcePath )
            {
                bool bValid = true;
                for ( size_t requestIndex = 0; requestIndex < rule._listClipName.size(); ++requestIndex )
                {
                    if ( listRequestedFound[requestIndex] == SW_TRUE )
                        continue;
                    SW_LOG_ERROR_ID( "ModelImporter.GlTFImportRuleAsks", "glTF '%#': import rule asks for clip '%#' which the source does not have", sourcePath, rule._listClipName[requestIndex].c_str() );
                    bValid = false;
                }
                for ( const auto& [clipName, extra] : mapLeftoverExtra )
                {
                    SW_LOG_ERROR_ID( "ModelImporter.ClipDataNamesClip", "Clip data for '%#' names clip '%#' which is not imported", sourcePath, clipName.c_str() );
                    bValid = false;
                }
                return bValid;
            }

            /** @brief cgltf 가 읽은 데이터의 소유자입니다 — 어느 반환 경로에서도 `cgltf_free` 가 불립니다(반환마다 손으로 풀면 새 경로에서 빠뜨린다). */
            struct GltfData
            {
                cgltf_data* _pData{ nullptr };

                GltfData()                             = default;
                GltfData( const GltfData& )            = delete;
                GltfData& operator=( const GltfData& ) = delete;
                ~GltfData() { cgltf_free( _pData ); }
            };

            /**
             * @brief 파일을 읽어 cgltf 로 파싱 · 버퍼 로드 · 검증합니다. 옳지 않은 부모를 가진 장면 루트는 최상위 조상으로 바꿉니다.
             * @param inoutBytes 파일 바이트입니다. cgltf 는 GLB 의 JSON 청크와 BIN 청크를 이 안에서 가리키므로 @p outData 보다 오래 살아야 한다.
             */
            [[nodiscard]] static bool parseGltf( string_view sourcePath, vector<uint8>& inoutBytes, GltfData& outData )
            {
                if ( FileUtil::readFile( sourcePath, inoutBytes ) == false || inoutBytes.empty() )
                {
                    SW_LOG_ERROR_ID( "ModelImporter.FailedReadGlTF", "Failed to read glTF '%#'", sourcePath );
                    return false;
                }
                const uint32 promotedCount = ModelImportGeometry::promoteSceneRootsToTopAncestor( inoutBytes );
                if ( promotedCount > 0 )
                    SW_LOG_WARNING_ID( "ModelImporter.GlTFSceneRootNode", "glTF '%#': %# scene root node(s) have a parent (non-conforming exporter) - using their top-most ancestor instead", sourcePath, promotedCount );

                const string        path    = string( sourcePath );
                const cgltf_options options = ModelImportUtil::makeOptions();
                cgltf_result        result  = cgltf_parse( &options, inoutBytes.data(), inoutBytes.size(), &outData._pData );
                if ( result == cgltf_result_success )
                    result = cgltf_load_buffers( &options, outData._pData, path.c_str() );
                if ( result == cgltf_result_success )
                    result = cgltf_validate( outData._pData );
                if ( result != cgltf_result_success )
                {
                    SW_LOG_ERROR_ID( "ModelImporter.FailedReadGlTFCgltf", "Failed to read glTF '%#' (cgltf result %#)", sourcePath, static_cast<uint32>( result ) );
                    return false;
                }
                return true;
            }

            /** @brief 스킨이 있는 모델을 읽습니다. 규칙의 이동 · 재중심이나 LOD 는 스킨드 모델에 맞지 않아 거절합니다. 읽었으면 아바타에 없는 본을 경고합니다. */
            [[nodiscard]] static bool readSkinnedAsset( const cgltf_data& data, const vector<const cgltf_node*>& listRoot, const vector<string>& listSectionMaterialName, const ModelImportRule& rule,
                                                        string_view sourcePath, ModelImportResult& outResult )
            {
                const bool bMoves = rule._recenter != ModelRecenter::None || rule._arrTranslation[0] != 0.0f || rule._arrTranslation[1] != 0.0f || rule._arrTranslation[2] != 0.0f;
                if ( bMoves )
                {
                    SW_LOG_ERROR_ID( "ModelImporter.GlTFSkinnedImportRule", "glTF '%#' is skinned - import rule translation / recenter would break its bind matrices", sourcePath );
                    return false;
                }
                if ( readSkinnedModel( data, listRoot, rule, sourcePath, listSectionMaterialName, outResult ) == false )
                    return false;
                ModelImportVrm::warnMissingAvatarBones( outResult, sourcePath );
                return true;
            }

            /**
             * @brief 스킨 없는 정적 모델을 읽습니다 — 장면 루트마다 메시를 합치고 규칙을 적용해 머티리얼 구간 또는 LOD 단계를 만든다.
             * @param listSectionMaterialName 구간을 나누는 glTF 머티리얼마다의 이름입니다(VRM · 머티리얼이 여럿인 텍스처 glTF). 비면 구간 없는 메시다.
             */
            [[nodiscard]] static bool readStaticModel( const cgltf_data& data, const vector<const cgltf_node*>& listRoot, const vector<string>& listSectionMaterialName,
                                                       const ModelImportRule& rule, string_view sourcePath, ModelImportResult& outResult )
            {
                const bool                  bSections = listSectionMaterialName.empty() == false;
                ModelImportUtil::MergedMesh merged;
                // 머티리얼(구간 머티리얼 · 텍스처 PBR 머티리얼 하나)이 색 계수를 가지면 정점에 굽지 않는다 — 두 번 곱한다
                merged._bBakeMaterialColor = bSections == false && ( rule._bImportMaterials == SW_FALSE || ModelImportMaterial::findTexturedPbrMaterial( data ) == nullptr );
                for ( const cgltf_node* pRoot : listRoot )
                {
                    if ( ModelImportGeometry::appendNode( *pRoot, merged ) == false )
                    {
                        SW_LOG_ERROR_ID( "ModelImporter.GlTFMeshUnderNode", "glTF '%#': a mesh under node '%#' has vertex data that cannot be read", sourcePath, ModelImportUtil::makeNodeName( data, *pRoot ).c_str() );
                        return false;
                    }
                }
                if ( data.animations_count > 0 )
                    SW_LOG_WARNING_ID( "ModelImporter.GlTFAnimationsButNo", "glTF '%#' has %# animations but no skin - node animations are not imported", sourcePath, data.animations_count );

                if ( merged._listIndex.empty() )
                {
                    SW_LOG_ERROR_ID( "ModelImporter.GlTFNoTrianglesDefault", "glTF '%#' has no triangles in its default scene (%# primitives skipped)", sourcePath, merged._skippedPrimitiveCount );
                    return false;
                }
                // 일반 glTF 의 PBR 머티리얼(텍스처)은 임포트가 모델 옆 머티리얼로 옮긴다(`ModelImportMaterial::writePbrMaterial`).
                if ( bSections == false && rule._bImportMaterials == SW_TRUE && ModelImportMaterial::readPbrMaterial( data, sourcePath, outResult ) == false ) // 스킨드 모델도 같은 규칙(readSkinnedModel)
                    return false;

                ModelImportGeometry::applyRule( rule, merged );
                // LOD 는 구간 · 모프 없는 메시만 받는다(`Mesh::setLODs`) — 규칙이 맞지 않는 모델에 걸렸으면 LOD 없이 쓰지 않고 임포트를 멈춘다.
                const bool bLODUnsupported = rule._listLOD.empty() == false && ( bSections || merged._listMorphName.empty() == false );
                if ( bLODUnsupported )
                {
                    SW_LOG_ERROR_ID( "ModelImporter.GlTFMaterialSectionsMorph", "glTF '%#' has material sections or morph targets - import rule '%#' asks for mesh LODs, which only plain static meshes take", sourcePath,
                                     rule._name.c_str() );
                    return false;
                }
                if ( bSections )
                {
                    ModelImportMaterial::populateMaterialSections( merged, data, listSectionMaterialName, outResult );
                    if ( outResult._listToonMaterial.empty() == false && ModelImportMaterial::collectSectionTextures( data, sourcePath, outResult ) == false )
                        return false;
                    ModelImportGeometry::makeMeshDataInOrder( merged, 0, outResult._mesh );
                    outResult._mesh._listSection = ModelImportVrm::makeMeshSections( outResult._listSection );
                    return true;
                }
                ModelImportGeometry::makeMeshData( merged, 0, outResult._mesh );
                return ModelImportGeometry::makeLODLevels( merged, rule, sourcePath, 0, outResult._mesh._listLOD );
            }

            /**
             * @brief 파쇄는 규칙에 `fracture` 가 있으면 `.mesh` 옆에 `.fracture` 를 쓰고, 없으면 이전 파일을 지웁니다. 규칙에서 빠진 파쇄가 남지 않게 하기 위해서다.
             * @return 스킨드 모델에 파쇄 규칙이 걸렸거나 쓰기 · 지우기에 실패하면 오류를 남기고 false 입니다.
             */
            [[nodiscard]] static bool writeFractureAsset( const ModelImportResult& result, const ModelImportRule& rule, string_view sourcePath, string_view outputPath )
            {
                const string fracturePath = FractureAsset::makePathForMesh( outputPath );
                if ( rule._bFracture != SW_TRUE )
                {
                    if ( FileUtil::exists( fracturePath ) && FileUtil::removeFile( fracturePath ) == false )
                    {
                        SW_LOG_ERROR_ID( "ModelImporter.FailedRemoveStaleFracture", "Failed to remove stale fracture asset %#", fracturePath.c_str() );
                        return false;
                    }
                    return true;
                }
                if ( result._bSkinned == SW_TRUE )
                {
                    SW_LOG_ERROR_ID( "ModelImporter.FractureRuleMatchedSkinned", "Fracture rule '%#' matched skinned model %# - only static meshes fracture", rule._name.c_str(), sourcePath );
                    return false;
                }
                FractureAsset fracture;
                string        error;
                if ( MeshFractureUtil::fracture( result._mesh._listVertex, result._mesh._listIndex, rule._fracture, fracture, error ) == false )
                {
                    SW_LOG_ERROR_ID( "ModelImporter.FractureFailed", "Fracture of %# failed: %#", sourcePath, error.c_str() );
                    return false;
                }
                const optional<AbsolutePath> fractureFile = FileUtil::requireAbsolutePath( fracturePath );
                if ( fractureFile.has_value() == false || fracture.saveToFile( *fractureFile ) == false )
                {
                    SW_LOG_ERROR_ID( "ModelImporter.FailedWriteFractureAsset", "Failed to write fracture asset %#", fracturePath.c_str() );
                    return false;
                }
                SW_LOG_INFO_ID( "ModelImporter.FracturedPiecesLinksLevels", "Fractured %# -> %# (%# pieces, %# links, %# levels, %# interior triangles)", sourcePath, fracturePath.c_str(), fracture.getPieceCount(),
                                fracture._graph._listLink.size(), fracture._graph.getDepthCount(), fracture.countTriangles( FractureSurfaceSlot::Interior ) );
                return true;
            }

            /** @brief 부착 메시를 옆 폴더에 쓰고 부착 표를 만듭니다(모델마다 다르므로 공용 스켈레톤이 아니라 모델 옆 파일에 든다). 쓰기에 실패하면 오류를 남기고 false 입니다. */
            [[nodiscard]] static bool writeAttachments( const AssetPaths& assetPaths, const ModelImportResult& result, const string& sideFolder, vector<SkeletonAttachment>& outListAttachment )
            {
                for ( const ModelImportAttachment& attachment : result._listAttachment )
                {
                    const string                 partPath = FileUtil::joinPath( FileUtil::joinPath( sideFolder, ModelImportUtil::kPartFolder ), attachment._fileStem + string( MeshAssetFormat::kExtension ) );
                    const optional<AbsolutePath> partFile = FileUtil::requireAbsolutePath( partPath );
                    if ( partFile.has_value() == false || MeshAssetFormat::saveToFile( *partFile, attachment._mesh ) == false )
                    {
                        SW_LOG_ERROR_ID( "ModelImporter.FailedWriteAttachmentMesh", "Failed to write attachment mesh %#", partPath.c_str() );
                        return false;
                    }
                    SkeletonAttachment entry{};
                    entry._name           = attachment._name;
                    entry._meshPath       = assetPaths.toAssetID( partPath );
                    entry._parentBone     = attachment._parentBone;
                    entry._localTransform = attachment._localTransform;
                    outListAttachment.push_back( entry );
                }
                return true;
            }

            /**
             * @brief 스켈레톤을 공용 폴더(`<x>/models/_skeletons/`)에 내용당 한 벌만 쓰고, 그 경로를 돌려줍니다. 이름이 내용의 지문이라 같은 내용의 모델은 같은 파일을 가리킵니다.
             * @details 언리얼이 임포트 때 기존 USkeleton 을 고르는 일을 내용 대조로 저절로 한다. 공용 폴더는 옆 폴더 밖이라 다시 임포트해도 지워지지 않는다.
             *          원본이 사라져 아무도 가리키지 않는 파일은 남는다(스켈레톤 하나는 작다).
             */
            [[nodiscard]] static bool writeSharedSkeleton( const Skeleton& skeleton, string_view outputPath, string& outSharedPath )
            {
                const string normalized = FileUtil::normalizeSeparators( outputPath );
                const string marker     = "/" + string( ModelImportUtil::kImportedModelFolder ) + "/";
                const size_t folderEnd  = normalized.rfind( marker );
                if ( folderEnd == string::npos )
                {
                    SW_LOG_ERROR_ID( "ModelImporter.ImportedMeshOutsideModels", "Imported mesh '%#' is not under a '%#' folder - the shared skeleton has no place", outputPath, ModelImportUtil::kImportedModelFolder );
                    return false;
                }
                const string text = skeleton.toXML();
                const uint64 hash = HashUtil::appendBytes64( HashUtil::kFNVOffset64, text.data(), text.size() );
                outSharedPath     = normalized.substr( 0, folderEnd + marker.size() ) + string( ModelImportUtil::kSharedSkeletonFolder ) + "/skeleton_" + to_string( hash ) + string( Skeleton::kExtension );

                // 잠그지 않는다: 이름이 내용 지문이라 워커 둘이 같은 파일을 써도 같은 글이고, `writeTextFile` 은 저마다의 임시 파일에 쓴 뒤 바꿔 끼운다.
                string existing;
                if ( FileUtil::exists( outSharedPath ) && FileUtil::readTextFile( outSharedPath, existing ) && existing == text )
                    return true;
                if ( FileUtil::ensureParentDirectoryExists( outSharedPath ) == false || FileUtil::writeTextFile( outSharedPath, text ) == false )
                {
                    SW_LOG_ERROR_ID( "ModelImporter.FailedWriteSkeleton", "Failed to write skeleton %#", outSharedPath.c_str() );
                    return false;
                }
                return true;
            }

            /**
             * @brief 클립을 공유 저장소에 한 벌만 쓰고(같은 내용 · 같은 설정이면 모델 수와 상관없이 파일 하나다) 이름 → 파일 표(`<옆 폴더>/<y>.animset.xml`)를 쓰며 원본 · 압축 바이트 합을 더합니다.
             * @return 쓰기에 실패하면 오류를 남기고 false 입니다.
             */
            [[nodiscard]] static bool writeClips( const AssetPaths& assetPaths, const ModelImportResult& result, const ModelImportRule& rule, string_view sourcePath, const string& sideFolder, string_view stem, uint64& inoutRawByteTotal,
                                                  uint64& inoutCompressedByteTotal )
            {
                if ( result._listClip.empty() )
                    return true;
                const string domain = ImportedAssetStore::findDomainFolder( sourcePath );
                if ( domain.empty() )
                {
                    SW_LOG_ERROR_ID( "ModelImporter.ClipSourceNotUnderRaw", "'%#' is not under %#/ - there is no shared store for its clips", sourcePath, ModelImportUtil::kRawModelFolder.data() );
                    return false;
                }
                const string settingsText = rule.makeClipSettingsText();
                AnimationSet animationSet;
                for ( const ModelImportClip& imported : result._listClip )
                {
                    vector<uint8> bytes;
                    imported._clip.makeBytes( bytes );
                    const string key = ImportedAssetStore::makeKey( bytes.data(), bytes.size(), settingsText );
                    // 이름을 앞에 붙인다 - 키만으로는 Quick Open · 브라우저 · 로그에서 어떤 클립인지 알 수 없다(언리얼 · 유니티도 에셋을 이름으로 찾는다). 이름까지 같아야 한 벌이다.
                    const string clipPath = ImportedAssetStore::makeStorePath( domain, ModelImportUtil::kClipFolder, imported._fileStem + "_" + key, AnimationClip::kExtension );
                    if ( ImportedAssetStore::writeIfAbsent( clipPath, bytes.data(), bytes.size() ) == false )
                    {
                        SW_LOG_ERROR_ID( "ModelImporter.FailedWriteAnimationClip", "Failed to write animation clip %#", clipPath.c_str() );
                        return false;
                    }
                    animationSet.setClip( imported._fileStem, assetPaths.toAssetID( clipPath ) );
                    inoutRawByteTotal += imported._stats._rawByteCount;
                    inoutCompressedByteTotal += imported._stats._compressedByteCount;
                    SW_LOG_INFO_ID( "ModelImporter.ClipBytesXMax", "Clip '%#' (%#): %# s, %# -> %# bytes (x%#), max error %# mm", imported._clip.getName().c_str(), rule._animationCodec.c_str(),
                                    imported._clip.getDuration(), imported._stats._rawByteCount, imported._stats._compressedByteCount, imported._stats.computeRatio(),
                                    imported._stats._maxError * 1000.0f );
                }
                const string setPath = FileUtil::joinPath( sideFolder, string( stem ) + string( AnimationSet::kExtension ) );
                if ( FileUtil::writeTextFile( setPath, animationSet.saveToString() ) == false )
                {
                    SW_LOG_ERROR_ID( "ModelImporter.FailedWriteAnimationSet", "Failed to write animation set %#", setPath.c_str() );
                    return false;
                }
                return true;
            }
        };

        /**
         * @class ModelRawImporter
         * @brief 모델 원본이 일괄 임포트에 제공하는 함수들입니다.
         */
        class ModelRawImporter final : public IRawAssetImporter
        {
        public:
            /** @brief @p assetPaths 는 부른 스레드가 찾아 넘긴 경로 상태다 — `importSource` 가 워커에서 돌아 서비스를 찾지 않는다. */
            ModelRawImporter( const AssetPaths& assetPaths, const ModelImportConfig& config )
                : _assetPaths{ assetPaths }
                , _config{ config }
            {
            }

            string_view getRawFolderName() const override { return ModelImportUtil::kRawModelFolder; }
            string_view getStampHeader() const override { return ModelImportUtil::kImportStampHeader; }
            string_view getImportedLabel() const override { return "메시"; }
            bool        isSourceFile( string_view path ) const override { return ModelImportUtil::isSourceModel( path ); }
            string      makeImportedPath( string_view sourcePath ) const override { return ModelImporter::makeImportedModelPath( sourcePath ); }
            uint64      computeSourceHash( string_view sourcePath, string_view assetPath ) const override
            {
                ModelImportRule rule;
                if ( findRule( assetPath, rule ) == false )
                    return 0; // 규칙이 겹치는 원본은 해시를 만들지 않는다(0 = 읽지 못함). 어긋남으로 잡히고 임포트도 같은 오류로 멈춘다
                return ModelImporter::computeSourceHash( sourcePath, rule );
            }
            uint64      computeImportedHash( string_view importedPath ) const override { return ModelImporter::computeImportedHash( importedPath ); }
            const utf8* findUnsupportedReason( string_view sourcePath ) const override
            {
                (void)sourcePath;
                return nullptr;
            }
            /** @brief 규칙에 몸 메시(`body_surface`)가 있으면 그 메시의 임포트 결과(`transferBodySurface` 가 `MeshCache` 로 읽는다)를 읽으므로 몸 메시를 임포트한 뒤에 직렬로 임포트합니다. */
            bool hasOrderDependency( string_view assetPath ) const override
            {
                const ModelImportRuleMatch match = _config.findMatchingRule( assetPath );
                return match.hasError() == false && match._rule._bodySurface.empty() == false; // 겹침 오류는 importSource 가 전한다
            }
            [[nodiscard]] bool importSource( string_view sourcePath, string_view importedPath, string_view assetPath ) const override
            {
                ModelImportRule rule;
                if ( findRule( assetPath, rule ) == false )
                    return false;
                return ModelImporter::importModel( _assetPaths, sourcePath, rule, importedPath );
            }

        private:
            /** @brief 리소스 경로에 맞는 규칙을 @p outRule 에 담습니다. 규칙이 겹치면 오류를 남기고 false 입니다. 맞는 규칙이 없으면 기본 규칙이고 true 입니다. */
            [[nodiscard]] bool findRule( string_view assetPath, ModelImportRule& outRule ) const
            {
                ModelImportRuleMatch match = _config.findMatchingRule( assetPath );
                if ( match.hasError() )
                {
                    SW_LOG_ERROR_ID( "ModelImporter.ImportRuleRejected", "%#", match._error.c_str() );
                    return false;
                }
                outRule = std::move( match._rule );
                return true;
            }

        private:
            const AssetPaths&        _assetPaths;
            const ModelImportConfig& _config;
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    bool ModelImporter::readModelAsset( string_view sourcePath, const ModelImportRule& rule, ModelImportResult& outResult )
    {
        using Internal = ModelImporterInternal;
        outResult      = ModelImportResult{};
        vector<uint8>      bytes; // 소유자(`holder`)보다 오래 산다
        Internal::GltfData holder;
        if ( Internal::parseGltf( sourcePath, bytes, holder ) == false )
            return false;
        const cgltf_data& data = *holder._pData;

        vector<const cgltf_node*> listRoot;
        ModelImportGeometry::collectSceneRoots( data, listRoot );
        if ( ModelImportMaterial::readToonMaterials( data, sourcePath, outResult._listToonMaterial, outResult._listIgnoredMaterialKey ) == false ||
             ModelImportVrm::readVrmAvatar( data, sourcePath, outResult ) == false )
            return false;
        const bool bPbrSections = outResult._listToonMaterial.empty() && rule._bImportMaterials == SW_TRUE;
        if ( bPbrSections && ModelImportMaterial::readSectionPbrMaterials( data, sourcePath, outResult ) == false )
            return false;
        // 구간을 나누는 머티리얼 표(툰 또는 PBR)의 이름 — 비어 있으면 구간 없는 메시다.
        vector<string> listSectionMaterialName;
        for ( const ToonMaterialDef& toon : outResult._listToonMaterial )
        {
            listSectionMaterialName.push_back( toon._name );
        }
        for ( const ModelImportPbrMaterial& pbr : outResult._listPbrMaterial )
        {
            listSectionMaterialName.push_back( pbr._name );
        }

        if ( data.skins_count > 0 )
            return Internal::readSkinnedAsset( data, listRoot, listSectionMaterialName, rule, sourcePath, outResult );
        return Internal::readStaticModel( data, listRoot, listSectionMaterialName, rule, sourcePath, outResult );
    }

    bool ModelImporter::transferBodySurface( string_view sourcePath, const ModelImportRule& rule, MeshAssetData& inoutMesh )
    {
        const shared_ptr<Mesh> body = MeshCache::acquire( rule._bodySurface );
        if ( body == nullptr )
        {
            SW_LOG_ERROR_ID( "ModelImporter.ImportRuleBodyMesh", "Import rule '%#': body mesh '%#' for the surface transfer of %# cannot be read", rule._name.c_str(), rule._bodySurface.c_str(), sourcePath );
            return false;
        }
        shared_ptr<Mesh> garment = Mesh::create();
        if ( garment->setGeometryWithTangents( vector<RHIVertex>( inoutMesh._listVertex ), vector<uint32>( inoutMesh._listIndex ) ) == false )
            return false; // 메시가 까닭을 남겼다(임포트가 만든 번호라 생기지 않아야 한다)
        if ( inoutMesh._listSkinVertex.empty() == false )
            garment->setSkin( inoutMesh._listSkinVertex, body->getSkinBinding() );
        garment->setMorphTargets( inoutMesh._listMorphTarget );
        const uint32 unboundCount = CharacterMeshUtil::transferBodySurface( *body, *garment, rule._bodySurfaceDistance );
        if ( unboundCount > 0 )
        {
            SW_LOG_ERROR_ID( "ModelImporter.ImportRuleVerticesFarther", "Import rule '%#': %# of %# vertices of %# are farther than %# m from body '%#' - move the garment onto the body or raise body_surface_distance",
                             rule._name.c_str(), unboundCount, garment->getVertexCount(), sourcePath, rule._bodySurfaceDistance, rule._bodySurface.c_str() );
            return false;
        }
        inoutMesh._listSkinVertex  = garment->getSkinVertices();
        inoutMesh._skinBinding     = garment->getSkinBinding(); // 가중치의 번호가 몸의 본이므로 바인드도 몸의 것이다
        inoutMesh._listMorphTarget = garment->getMorphTargets();
        SW_LOG_INFO_ID( "ModelImporter.TransferredSkinMorphTargets", "Transferred skin and %# morph targets of '%#' onto %#", static_cast<uint32>( inoutMesh._listMorphTarget.size() ), rule._bodySurface.c_str(), sourcePath );
        return true;
    }

    bool ModelImporter::importModel( const AssetPaths& assetPaths, string_view sourcePath, const ModelImportRule& rule, string_view outputPath )
    {
        using Internal = ModelImporterInternal;
        ModelImportResult result{};
        if ( readModelAsset( sourcePath, rule, result ) == false )
            return false;
        // 장비 메시 — 몸의 스킨 · 체형 모프를 옮겨 쓴다(쿠킹 단계 전이, 외형 피팅이 같은 체형을 장비에 베이크한다).
        if ( rule._bodySurface.empty() == false && transferBodySurface( sourcePath, rule, result._mesh ) == false )
            return false;

        // 옆 폴더는 임포트할 때마다 새로 쓴다. 규칙에서 빠진 클립과 사라진 부착 메시가 이전 파일로 남지 않게 하기 위해서다.
        const string sideFolder = makeImportedSideFolder( outputPath );
        if ( FileUtil::removeDirectory( sideFolder ) == false )
        {
            SW_LOG_ERROR_ID( "ModelImporter.FailedClearImportedModel", "Failed to clear the imported model folder %#", sideFolder.c_str() );
            return false;
        }
        const optional<AbsolutePath> outputFile = FileUtil::requireAbsolutePath( outputPath );
        if ( outputFile.has_value() == false || MeshAssetFormat::saveToFile( *outputFile, result._mesh ) == false )
        {
            SW_LOG_ERROR_ID( "ModelImporter.FailedWriteMeshAsset", "Failed to write mesh asset %#", outputPath );
            return false;
        }
        if ( ModelImportMaterial::writeMaterialSections( assetPaths, result, sourcePath, sideFolder ) == false )
            return false;
        if ( ModelImportMaterial::writePbrMaterial( assetPaths, result, sourcePath, sideFolder ) == false )
            return false;
        if ( Internal::writeFractureAsset( result, rule, sourcePath, outputPath ) == false )
            return false;
        const string stem = FileUtil::removeExtension( FileUtil::getFileNamePart( outputPath ) );
        if ( result._bSkinned == SW_FALSE )
        {
            if ( ModelImportVrm::writeSectionPrefab( assetPaths, result, outputPath, sideFolder, stem ) == false )
                return false;
            SW_LOG_INFO_ID( "ModelImporter.ImportedModelTriangles", "Imported model: %# -> %# (%# triangles)", sourcePath, outputPath, result._mesh._listIndex.size() / 3 );
            return true;
        }

        SkeletonVariant skeletonFileData{};
        if ( Internal::writeAttachments( assetPaths, result, sideFolder, skeletonFileData._listAttachment ) == false )
            return false;
        // 스켈레톤은 내용당 한 벌이고, 모델 옆 파일은 그것을 가리키는 차이 없는 변형이다 — 데이터가 적은 경로(프리팹 · 본 LOD · 리타깃 프로필)는 그대로다.
        string sharedSkeletonPath;
        if ( Internal::writeSharedSkeleton( result._skeleton, outputPath, sharedSkeletonPath ) == false )
            return false;
        skeletonFileData._base    = assetPaths.toAssetID( sharedSkeletonPath );
        const string skeletonPath = FileUtil::joinPath( sideFolder, stem + string( Skeleton::kExtension ) );
        if ( FileUtil::ensureParentDirectoryExists( skeletonPath ) == false || FileUtil::writeTextFile( skeletonPath, skeletonFileData.toXML() ) == false )
        {
            SW_LOG_ERROR_ID( "ModelImporter.FailedWriteSkeleton", "Failed to write skeleton %#", skeletonPath.c_str() );
            return false;
        }
        if ( ModelImportVrm::writeVrmAvatar( result, sourcePath, sideFolder, stem ) == false )
            return false;
        if ( ModelImportVrm::writeSectionPrefab( assetPaths, result, outputPath, sideFolder, stem ) == false )
            return false;
        [[maybe_unused]] uint64 rawByteTotal        = 0;
        [[maybe_unused]] uint64 compressedByteTotal = 0;
        if ( Internal::writeClips( assetPaths, result, rule, sourcePath, sideFolder, stem, rawByteTotal, compressedByteTotal ) == false )
            return false;
        SW_LOG_INFO_ID( "ModelImporter.ImportedSkinnedModelTriangles", "Imported skinned model: %# -> %# (%# triangles, %# bones, %# attachments, %# clips, clips %# -> %# bytes)", sourcePath, outputPath,
                        result._mesh._listIndex.size() / 3, result._skeleton.getBoneCount(), result._listAttachment.size(), result._listClip.size(), rawByteTotal,
                        compressedByteTotal );
        return true;
    }

    bool ModelImporter::importChangedSourceModel( const AssetPaths& assetPaths, string_view relativePath )
    {
        if ( MeshAssetFormat::isMeshAssetPath( relativePath ) )
            return false;

        const string& resourceRoot = assetPaths.getRootFolderPath();
        const string  normalized   = FileUtil::normalizeSeparators( FileUtil::joinPath( resourceRoot, relativePath ) );
        if ( makeImportedModelPath( normalized ).empty() )
        {
            SW_LOG_WARNING_ID( "ModelImporter.ModelSourcesMustUnder", "Model sources must be under `%#` to be imported: %#", ModelImportUtil::kRawModelFolder, relativePath );
            return true;
        }

        // 스탬프를 기록하는 경로가 하나여야 에디터에서 임포트한 것과 `App --import-models` 로 임포트한 것이 같은 판정을 받는다.
        // 설정 파일이 없으면 규칙 없이 임포트한다. 깨졌으면 기본 규칙으로 임포트하지 않는다(결과가 말없이 바뀐다) — 로드가 오류를 남겼다.
        ModelImportConfig config{};
        if ( config.loadFromFile( ModelImportConfig::makeDefaultConfigPath() ) == ConfigReadResult::Invalid )
            return true;
        // 바뀐 원본만 임포트한다(루트 전체를 훑지 않는다). 스탬프를 쓰는 길은 `importAllModels` 와 같은 `AssetImportStampUtil` 한 곳이다.
        const ModelRawImporter importer{ assetPaths, config };
        // 핫 리로드의 임포트 잡이 워커에서 부르므로 작업 시스템을 찾지 않는다(워커는 에디터 서비스 범위를 읽지 않는다) — 원본 하나라 직렬과 같다.
        const AssetImportSummary summary = AssetImportStampUtil::importSources( resourceRoot, importer, vector<string>{ string( relativePath ) }, nullptr );
        for ( const string& problem : summary._listProblem )
        {
            SW_LOG_ERROR_ID( "ModelImporter.ModelImportFailed", "Model import failed: %#", problem.c_str() );
        }
        SW_LOG_INFO_ID( "ModelImporter.ModelImportChangedImported", "Model import: %# changed -> %# imported", relativePath, summary._importedCount );
        return true;
    }

    AssetImportSummary ModelImporter::importAllModels( const AssetPaths& assetPaths, string_view resourceRoot, const ModelImportConfig& config, AssetImportMode mode )
    {
        const ModelRawImporter importer{ assetPaths, config };
        AssetImportSummary     summary = AssetImportStampUtil::importAll( resourceRoot, importer, mode, editor::findSubsystem<TaskManager>() ); // 선택: 없으면 이 스레드가 직렬로 임포트한다(계약)
        if ( mode == AssetImportMode::ImportStale )
            (void)ImportedAssetStore::removeUnreferenced( resourceRoot, summary._listProblem ); // 원본을 고치면 옛 키의 공유 파일이 남는다 — 전체를 다 본 지금 지운다(범위 임포트는 다른 원본의 참조를 몰라 하지 않는다)
        for ( const string& rule : AssetImportRuleUtil::findRulesMatchingNothing( config.getRules(), AssetImportStampUtil::collectSourceAssetPaths( resourceRoot, importer ) ) )
        {
            summary._listProblem.push_back( "ModelImportConfig: rule " + rule + " matches no model source - its patterns are dead (a moved folder?)" );
        }
        return summary;
    }

    string ModelImporter::makeImportedModelPath( string_view rawModelPath )
    {
        return AssetImportStampUtil::makeImportedPath( rawModelPath, ModelImportUtil::kRawModelFolder, ModelImportUtil::kImportedModelFolder,
                                                       MeshAssetFormat::kExtension );
    }

    string ModelImporter::makeImportedSideFolder( string_view importedMeshPath )
    {
        return FileUtil::removeExtension( importedMeshPath );
    }

    string ModelImporter::makeClipDataPath( string_view sourcePath )
    {
        return FileUtil::removeExtension( sourcePath ) + string( ModelImportUtil::kClipDataSuffix );
    }

    uint64 ModelImporter::computeSourceHash( string_view sourcePath, const ModelImportRule& rule )
    {
        vector<uint8> bytes;
        if ( FileUtil::readFile( sourcePath, bytes ) == false || bytes.empty() )
            return 0;

        StringBuilder<constant::buffer::kMax64> versionText;
        versionText.appendFormat( "importer=%#;format=%#;recenter=%#;clip=%#", ModelImportUtil::kImporterVersion, MeshAssetFormat::kVersion,
                                  static_cast<uint32>( rule._recenter ), AnimationClip::kVersion );
        uint64 hash = HashUtil::computeStringHash64( reinterpret_cast<const utf8*>( bytes.data() ), bytes.size(), false );
        hash        = HashUtil::computeStringHash64( versionText.c_str(), versionText.size(), false, hash );
        // 이동은 비트 그대로 섞는다. 문자열로 반올림하면 작은 변경이 같은 해시가 된다.
        hash                       = HashUtil::computeStringHash64( reinterpret_cast<const utf8*>( rule._arrTranslation ), sizeof( rule._arrTranslation ), false, hash );
        const string animationText = rule.makeAnimationHashText();
        hash                       = HashUtil::computeStringHash64( animationText.c_str(), animationText.size(), false, hash );
        // VRM 이면 아바타 파일 형식 버전도 섞는다. 그 형식만 바뀌었을 때 VRM 원본만 다시 임포트하게 한다(임포터 버전을 올리면 모든 모델이 어긋난다).
        const bool bVrm = ModelImportVrm::containsVrmExtension( bytes );
        if ( bVrm )
        {
            const string avatarText = formatToString( "avatar=%#", VrmAvatarDef::kVersion );
            hash                    = HashUtil::computeStringHash64( avatarText.c_str(), avatarText.size(), false, hash );
        }
        // 모델 옆 머티리얼을 찍어 내는 견본(VRM 은 툰, 그 밖은 PBR)도 섞는다 — 견본에 프로퍼티 · 스위치를 더하면 이미 쓴 머티리얼도 다시 써야 한다.
        const unique_ptr<EngineDefaultAssets> pDefaultAssets = EngineDefaultAssets::load();
        string                                templateText;
        if ( pDefaultAssets == nullptr ||
             AssetIO::readTextAsset( AssetID{ ( bVrm ? pDefaultAssets->_toonMaterialTemplate : pDefaultAssets->_pbrMaterialTemplate ).get() }, templateText ) == false )
        {
            SW_LOG_ERROR_ID( "ModelImporter.MaterialTemplateUnreadable", "Model '%#': the material template could not be read - the import stamp cannot be computed", sourcePath );
            return 0;
        }
        hash                      = HashUtil::computeStringHash64( templateText.c_str(), templateText.size(), false, hash );
        const string fractureText = rule.makeFractureHashText();
        if ( fractureText.empty() == false )
        {
            const string fractureVersion = formatToString( "%#;format=%#;algorithm=%#", fractureText, FractureAsset::kVersion, MeshFractureUtil::kAlgorithmVersion );
            hash                         = HashUtil::computeStringHash64( fractureVersion.c_str(), fractureVersion.size(), false, hash );
        }
        // LOD 규칙이 없으면 섞지 않는다 — LOD 를 쓰지 않는 모델의 해시가 그대로다(다시 임포트하지 않는다).
        const string lodText = rule.makeLODHashText();
        if ( lodText.empty() == false )
            hash = HashUtil::computeStringHash64( lodText.c_str(), lodText.size(), false, hash );
        // 부가 데이터(클립 반복, 알림, 커브)를 고쳐도 다시 임포트해야 한다.
        vector<uint8> clipDataBytes;
        const string  clipDataPath = makeClipDataPath( sourcePath );
        const bool    bHasClipData = FileUtil::exists( clipDataPath ) && FileUtil::readFile( clipDataPath, clipDataBytes ) && clipDataBytes.empty() == false;
        if ( bHasClipData )
            hash = HashUtil::computeStringHash64( reinterpret_cast<const utf8*>( clipDataBytes.data() ), clipDataBytes.size(), false, hash );

        // `.gltf` 는 버퍼를 옆 파일로 둘 수 있다. 그 바이트가 바뀌어도 어긋남이어야 한다. data URI 는 이미 본문에 있다.
        if ( FileUtil::hasExtension( sourcePath, ".gltf" ) == false )
            return hash;
        const string        path    = string( sourcePath );
        const cgltf_options options = ModelImportUtil::makeOptions();
        cgltf_data*         pData   = nullptr;
        if ( cgltf_parse_file( &options, path.c_str(), &pData ) != cgltf_result_success )
        {
            cgltf_free( pData );
            return hash;
        }
        const string directory = FileUtil::getDirectoryPart( sourcePath );
        for ( cgltf_size bufferIndex = 0; bufferIndex < pData->buffers_count; ++bufferIndex )
        {
            const utf8* pUri = pData->buffers[bufferIndex].uri;
            if ( pUri == nullptr || StringUtil::startsWith( pUri, "data:" ) )
                continue;
            vector<uint8> bufferBytes;
            const string  bufferPath  = FileUtil::joinPath( directory, pUri );
            const bool    bBufferRead = FileUtil::exists( bufferPath ) && FileUtil::readFile( bufferPath, bufferBytes ); // 해시는 있는 버퍼만 섞는다(없는 버퍼는 임포트가 오류로 알린다)
            if ( bBufferRead && bufferBytes.empty() == false )
                hash = HashUtil::computeStringHash64( reinterpret_cast<const utf8*>( bufferBytes.data() ), bufferBytes.size(), false, hash );
        }
        cgltf_free( pData );
        return hash;
    }

    uint64 ModelImporter::computeImportedHash( string_view importedMeshPath )
    {
        uint64 hash = AssetImportStampUtil::computeFileHash( importedMeshPath );
        if ( hash == 0 )
            return 0;
        // 옆의 `.fracture` 도 결과다. 지워지거나 바뀌면 어긋남이다.
        const string fracturePath = FractureAsset::makePathForMesh( importedMeshPath );
        if ( FileUtil::exists( fracturePath ) )
        {
            const uint64 fractureHash = AssetImportStampUtil::computeFileHash( fracturePath );
            hash                      = HashUtil::computeStringHash64( reinterpret_cast<const utf8*>( &fractureHash ), sizeof( fractureHash ), false, hash );
        }
        // 옆 폴더의 파일을 이름 순서로 섞는다. 이름도 섞으므로 파일이 사라지거나 바뀌면 다른 값이 된다.
        const string sideFolder = FileUtil::normalizeSeparators( makeImportedSideFolder( importedMeshPath ) );
        if ( FileUtil::isDirectory( sideFolder ) == false )
            return hash;
        vector<string> listFile;
        (void)FileUtil::collectFiles( sideFolder, "", listFile, true ); // 폴더는 위에서 확인했다 — 도중에 멈춘 목록은 collectFiles 가 알리고 해시가 달라져 다시 임포트한다
        for ( string& file : listFile )
        {
            file = FileUtil::normalizeSeparators( file );
        }
        VectorUtil::sort( listFile );
        for ( const string& file : listFile )
        {
            string relative = file;
            if ( relative.size() > sideFolder.size() )
                relative = relative.substr( sideFolder.size() );
            hash                  = HashUtil::computeStringHash64( relative.c_str(), relative.size(), false, hash );
            const uint64 fileHash = AssetImportStampUtil::computeFileHash( file );
            hash                  = HashUtil::computeStringHash64( reinterpret_cast<const utf8*>( &fileHash ), sizeof( fileHash ), false, hash );
            // 옆 폴더 파일이 가리키는 공유 파일도 결과다 — 지우거나 손대면 어긋남이다.
            string text;
            if ( ImportedAssetStore::isReferrer( file ) && FileUtil::readTextFile( file, text ) )
                hash = ImportedAssetStore::mixReferencedFiles( importedMeshPath, text, hash );
        }
        return hash;
    }
} // namespace sw::editor
