/**
 * @file ModelImporter.cpp
 * @brief 모델 임포트에서 스킨드 모델과 클립 읽기, 공개 API(`ModelImporter`), 일괄 임포트를 구현합니다.
 */
#include "pch.h"

#include "Editor/Common/Asset/ModelImporter.h"

#include "Core/Common/FourCcUtil.h"
#include "Core/Common/HashUtil.h"
#include "Core/Container/StringUtil.h"
#include "Core/Container/unordered_map.h"
#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"
#include "Core/Math/VectorMath.h"
#include "Core/Memory/Memory.h"
#include "Core/String/StringBuilder.h"

#include "Editor/Common/Asset/AssetImportStamp.h"
#include "Editor/Common/Asset/ModelImportConfig.h"
#include "Editor/Common/Asset/ModelImportUtil.h"
#include "Editor/Common/Asset/TextureImporter.h"
#include "Editor/Common/Asset/VrmMaterialImporter.h"
#include "Editor/Common/EditorUtil.h"
#include "Editor/Common/Workspace/EditorService.h"

#include "Engine/Animation/AnimationJSONUtil.h"
#include "Engine/Animation/Codec/AnimationCodec.h"
#include "Engine/Config/ConfigManager.h"
#include "Engine/Destruction/FractureAsset.h"
#include "Engine/Destruction/MeshFracture.h"
#include "Engine/Graphics/Mesh/MeshAssetFormat.h"
#include "Engine/Graphics/RHI/RHITypes.h"
#include "Engine/Resource/ResourcePaths.h"
#include "Engine/Serialization/JSON/JSONDocument.h"

#define CGLTF_IMPLEMENTATION
#include <cgltf.h>

namespace sw::editor
{
    SW_LOG_CALLER( "ModelImporter" );

    namespace
    {
        /**
         * @struct ModelImporterInternal
         * @brief 스킨드 모델과 클립 읽기를 담당합니다. 나머지 도우미는 `ModelImportUtil`(조각 파일들)에 있습니다.
         */
        struct ModelImporterInternal : public ModelImportUtil
        {
            /** @brief 노드 메시의 정점 접근자를 읽지 못했다고 알리고 false 를 돌려줍니다(임포트 실패). */
            static bool reportUnreadableMesh( string_view sourcePath, const cgltf_node& node, const cgltf_data& data )
            {
                SW_LOG_ERROR( "glTF '%#': the mesh of node '%#' has vertex data that cannot be read", sourcePath, makeNodeName( data, node ).c_str() );
                return false;
            }

            /**
             * @brief 모든 스킨의 관절(노드가 같으면 한 본)로 스켈레톤을 만듭니다. 부모가 앞에 오도록 깊이 우선 순서로 정렬하고, 관절 위의 비관절 조상의
             *        변환은 루트 본에 합칩니다. 역 바인드는 그 관절을 처음 가진 스킨의 것입니다. 스킨끼리 다르면 경고합니다.
             * @param outMapJointBone 관절 노드에서 본 번호로의 대응입니다.
             * @param outListRootParent 본마다 루트에 합친 행렬(엔진 공간)입니다. 루트가 아니면 단위 행렬이며, 클립 샘플에도 같은 행렬을 곱합니다.
             */
            static bool buildSkeleton( const cgltf_data& data, Skeleton& outSkeleton, unordered_map<const cgltf_node*, int32>& outMapJointBone,
                                       vector<float4x4>& outListRootParent, string_view sourcePath )
            {
                outSkeleton.clear();
                outMapJointBone.clear();
                outListRootParent.clear();
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
                            if ( readFloats( skin.inverse_bind_matrices, jointIndex, arrMatrix, 16 ) == false )
                            {
                                SW_LOG_ERROR( "glTF '%#': inverse bind matrix %# of a skin cannot be read", sourcePath, static_cast<uint32>( jointIndex ) );
                                return false;
                            }
                            inverseBind = convertMatrixToEngineSpace( arrMatrix );
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
                            SW_LOG_WARNING( "glTF '%#': joint '%#' has different bind matrices in two skins - the first is used", sourcePath,
                                            makeNodeName( data, *skin.joints[jointIndex] ).c_str() );
                    }
                }

                // 깊이 우선으로 순회한다. 관절이 아닌 노드도 지나가지만 본은 관절만 만든다. 부모 본은 가장 가까운 관절 조상이다.
                vector<const cgltf_node*> listStack;
                for ( size_t jointIndex = listJoint.size(); jointIndex > 0; --jointIndex )
                {
                    const cgltf_node* pJoint = listJoint[jointIndex - 1];
                    if ( findJointAncestorBySlot( *pJoint, mapJointSlot ) == nullptr )
                        listStack.push_back( pJoint );
                }
                while ( listStack.empty() == false )
                {
                    const cgltf_node* pNode = listStack.back();
                    listStack.pop_back();
                    const auto itSlot = mapJointSlot.find( pNode );
                    if ( itSlot != mapJointSlot.end() && outMapJointBone.find( pNode ) == outMapJointBone.end() )
                    {
                        const cgltf_node* pParentJoint = findJointAncestorBySlot( *pNode, mapJointSlot );
                        const int32       parentBone   = pParentJoint != nullptr ? outMapJointBone[pParentJoint] : -1;
                        BoneTransform     local        = convertTransformToEngineSpace( readNodeLocal( *pNode ) );
                        float4x4          rootParent   = float4x4::Identity;
                        if ( parentBone < 0 && pNode->parent != nullptr )
                        {
                            float32 arrParentWorld[16]{};
                            cgltf_node_transform_world( pNode->parent, arrParentWorld );
                            rootParent = convertMatrixToEngineSpace( arrParentWorld );
                            local      = BoneTransform::makeFromMatrix( local.toMatrix() * rootParent );
                        }
                        const int32 boneIndex = outSkeleton.addBone( hashed_string( makeNodeName( data, *pNode ) ), parentBone, local, listInverseBind[itSlot->second] );
                        if ( boneIndex < 0 )
                            return false;
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
                    SW_LOG_ERROR( "glTF '%#': skin joints are not one connected hierarchy reachable from their roots", sourcePath );
                    return false;
                }
                if ( listJoint.size() > 0xFFFFu )
                {
                    SW_LOG_ERROR( "glTF '%#': %# joints do not fit 16-bit bone indices", sourcePath, listJoint.size() );
                    return false;
                }
                return true;
            }

            /**
             * @brief 스킨드 모델을 읽습니다. 모든 스킨이 한 스켈레톤을 쓰며, 관절 노드가 같으면 한 본입니다.
             * @param listToon VRM 이면 glTF 머티리얼마다의 툰 머티리얼입니다. 본 메시를 머티리얼 구간으로도 나눕니다.
             */
            [[nodiscard]] static bool readSkinnedModel( const cgltf_data& data, const vector<const cgltf_node*>& listRoot, const ModelImportRule& rule, string_view sourcePath,
                                                        const vector<ToonMaterialDesc>& listToon, ModelImportResult& outResult )
            {
                unordered_map<const cgltf_node*, int32> mapJointBone;
                vector<float4x4>                        listRootParent;
                if ( buildSkeleton( data, outResult._skeleton, mapJointBone, listRootParent, sourcePath ) == false )
                    return false;
                const uint32 boneCount = outResult._skeleton.getBoneCount();

                vector<SkinBinding> listBinding( data.skins_count );
                for ( cgltf_size skinIndex = 0; skinIndex < data.skins_count; ++skinIndex )
                {
                    const cgltf_skin& skin    = data.skins[skinIndex];
                    SkinBinding&      binding = listBinding[skinIndex];
                    binding._pSkin            = &skin;
                    binding._listJointToBone.resize( skin.joints_count, -1 );
                    for ( cgltf_size jointIndex = 0; jointIndex < skin.joints_count; ++jointIndex )
                    {
                        binding._listJointToBone[jointIndex] = mapJointBone[skin.joints[jointIndex]];
                    }
                }
                // 스킨 없는 메시를 몸에 합칠 때 쓰는 고정 본 매핑이다. 루트 본 0 에 가중치 1 을 준다.
                SkinBinding rigidBinding{};

                // 노드를 순회하며 메시를 나눈다. 이 스킨을 쓰는 메시는 몸, 관절 아래의 스킨 없는 메시는 부착, 그 밖의 스킨 없는 메시는 몸(루트 본에 가중치 1)이다.
                MergedMesh body;
                body._bBakeMaterialColor = listToon.empty();
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
                        const SkinBinding& binding = listBinding[static_cast<size_t>( pNode->skin - data.skins )];
                        for ( cgltf_size primitiveIndex = 0; primitiveIndex < pNode->mesh->primitives_count; ++primitiveIndex )
                        {
                            if ( appendPrimitive( pNode->mesh->primitives[primitiveIndex], pNode->mesh, arrIdentity, &binding, body ) == false )
                                return reportUnreadableMesh( sourcePath, *pNode, data );
                        }
                        continue;
                    }
                    const cgltf_node* pJoint = findJointAncestor( *pNode, mapJointBone );
                    if ( pJoint == nullptr || rule._bImportAttachments == SW_FALSE )
                    {
                        float32 arrWorld[16]{};
                        cgltf_node_transform_world( pNode, arrWorld );
                        for ( cgltf_size primitiveIndex = 0; primitiveIndex < pNode->mesh->primitives_count; ++primitiveIndex )
                        {
                            if ( appendPrimitive( pNode->mesh->primitives[primitiveIndex], pNode->mesh, arrWorld, &rigidBinding, body ) == false )
                                return reportUnreadableMesh( sourcePath, *pNode, data );
                        }
                        continue;
                    }

                    // 부착은 메시가 노드 로컬 공간에 있고, 변환은 가장 가까운 관절을 기준으로 한다. 사이의 비관절 노드는 곱해서 합친다.
                    ModelImportAttachment attachment{};
                    attachment._name       = makeNodeName( data, *pNode );
                    attachment._parentBone = outResult._skeleton.getBone( static_cast<uint32>( mapJointBone[pJoint] ) )._name;
                    float4x4 local         = float4x4::Identity;
                    for ( const cgltf_node* pStep = pNode; pStep != nullptr && pStep != pJoint; pStep = pStep->parent )
                    {
                        float32 arrLocal[16]{};
                        cgltf_node_transform_local( pStep, arrLocal );
                        local = local * convertMatrixToEngineSpace( arrLocal );
                    }
                    attachment._localTransform = BoneTransform::makeFromMatrix( local );
                    const string baseStem      = makeFileStem( attachment._name );
                    const uint32 useCount      = mapStemUse[baseStem]++;
                    attachment._fileStem       = makeNumberedStem( baseStem, useCount );
                    MergedMesh part;
                    for ( cgltf_size primitiveIndex = 0; primitiveIndex < pNode->mesh->primitives_count; ++primitiveIndex )
                    {
                        if ( appendPrimitive( pNode->mesh->primitives[primitiveIndex], pNode->mesh, arrIdentity, nullptr, part ) == false )
                            return reportUnreadableMesh( sourcePath, *pNode, data );
                    }
                    expandTriangles( part, 0, attachment._mesh );
                    if ( attachment._mesh._listVertex.empty() == false )
                        outResult._listAttachment.push_back( std::move( attachment ) );
                }
                if ( body._listIndex.empty() )
                {
                    SW_LOG_ERROR( "glTF '%#' has a skin but no triangles bound to it", sourcePath );
                    return false;
                }
                if ( listToon.empty() == false )
                {
                    splitMaterialSections( body, data, listToon, boneCount, outResult );
                    if ( collectSectionTextures( data, sourcePath, outResult ) == false )
                        return false;
                }
                expandTriangles( body, boneCount, outResult._mesh );
                outResult._bSkinned = SW_TRUE;
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
                    SW_LOG_ERROR( "glTF '%#': unknown animation codec '%#'", sourcePath, rule._animationCodec.c_str() );
                    return false;
                }
                unordered_map<string, ClipExtra> mapExtra;
                if ( readClipData( sourcePath, mapExtra ) == false )
                    return false;

                const Skeleton& skeleton       = inoutResult._skeleton;
                int32           rootMotionBone = -1;
                if ( rule._rootMotionBone.empty() == false )
                {
                    rootMotionBone = skeleton.findBoneIndex( hashed_string( rule._rootMotionBone ) );
                    if ( rootMotionBone < 0 )
                    {
                        SW_LOG_ERROR( "glTF '%#': root_motion_bone '%#' is not a joint", sourcePath, rule._rootMotionBone.c_str() );
                        return false;
                    }
                }
                // 관절의 레스트 TRS(glTF 공간, 루트에 합치기 전)다. 채널이 없는 성분은 이 값을 쓴다.
                vector<BoneTransform> listRest( skeleton.getBoneCount() );
                for ( const auto& [pNode, boneIndex] : mapJointBone )
                {
                    listRest[static_cast<uint32>( boneIndex )] = readNodeLocal( *pNode );
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
                        const auto itRequested = std::find( rule._listClipName.begin(), rule._listClipName.end(), clipName );
                        if ( itRequested == rule._listClipName.end() )
                            continue;
                        listRequestedFound[static_cast<size_t>( itRequested - rule._listClipName.begin() )] = SW_TRUE;
                    }

                    // 모프 가중치 채널은 타깃 이름의 커브가 된다. 가중치만 움직이는 클립(말하기)도 그 길이를 가진다.
                    vector<AnimationClipCurve> listMorphCurve;
                    float32                    morphDuration = 0.0f;
                    AnimationRawClip           rawClip;
                    const bool                 bChannelsRead = readMorphWeightCurves( animation, rule._animationSampleRate, listMorphCurve, morphDuration ) &&
                                               resampleAnimation( animation, skeleton, mapJointBone, listRest, listRootParent, rule._animationSampleRate, morphDuration, rawClip );
                    if ( bChannelsRead == false )
                    {
                        SW_LOG_ERROR( "glTF '%#': clip '%#' has a channel whose keys cannot be read", sourcePath, clipName.c_str() );
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
                        if ( itExtra->second._loopOverride >= 0 )
                            imported._clip.setLooping( itExtra->second._loopOverride != 0 );
                        for ( const AnimationNotifyEvent& event : itExtra->second._listNotify )
                        {
                            imported._clip.addNotify( event );
                        }
                        for ( const AnimationClipCurve& curve : itExtra->second._listCurve )
                        {
                            imported._clip.addCurve( curve );
                        }
                        mapExtra.erase( itExtra );
                    }
                    if ( imported._clip.compressFrom( rawClip, *pCodec, settings, &imported._stats ) == false )
                    {
                        SW_LOG_ERROR( "glTF '%#': clip '%#' could not be compressed with '%#'", sourcePath, clipName.c_str(), pCodec->getName() );
                        return false;
                    }
                    const string baseStem = makeFileStem( clipName );
                    const uint32 useCount = mapStemUse[baseStem]++;
                    imported._fileStem    = makeNumberedStem( baseStem, useCount );
                    inoutResult._listClip.push_back( std::move( imported ) );
                }

                bool bValid = true;
                for ( size_t requestIndex = 0; requestIndex < rule._listClipName.size(); ++requestIndex )
                {
                    if ( listRequestedFound[requestIndex] == SW_TRUE )
                        continue;
                    SW_LOG_ERROR( "glTF '%#': import rule asks for clip '%#' which the source does not have", sourcePath, rule._listClipName[requestIndex].c_str() );
                    bValid = false;
                }
                // 부가 데이터에 남은 이름은 원본(또는 규칙이 고른 클립)에 없는 클립이다. 철자가 틀린 알림이 조용히 사라지지 않도록 오류로 처리한다.
                for ( const auto& [clipName, extra] : mapExtra )
                {
                    SW_LOG_ERROR( "Clip data for '%#' names clip '%#' which is not imported", sourcePath, clipName.c_str() );
                    bValid = false;
                }
                return bValid;
            }
        };

        /**
         * @class ModelRawImporter
         * @brief 모델 원본이 일괄 임포트에 제공하는 함수들입니다.
         */
        class ModelRawImporter final : public IRawAssetImporter
        {
        public:
            explicit ModelRawImporter( const ModelImportConfig& config )
                : _config{ config }
            {
            }

            string_view getRawFolderName() const override { return ModelImporterInternal::kRawModelFolder; }
            string_view getStampHeader() const override { return ModelImporterInternal::kImportStampHeader; }
            string_view getImportedLabel() const override { return "메시"; }
            bool        isSourceFile( string_view path ) const override { return ModelImporterInternal::isSourceModel( path ); }
            string      makeImportedPath( string_view sourcePath ) const override { return ModelImporter::makeImportedModelPath( sourcePath ); }
            uint64      computeSourceHash( string_view sourcePath, string_view resourcePath ) const override
            {
                return ModelImporter::computeSourceHash( sourcePath, _config.findMatchingRule( resourcePath ) );
            }
            uint64      computeImportedHash( string_view importedPath ) const override { return ModelImporter::computeImportedHash( importedPath ); }
            const utf8* findUnsupportedReason( string_view sourcePath ) const override
            {
                (void)sourcePath;
                return nullptr;
            }
            [[nodiscard]] bool importSource( string_view sourcePath, string_view importedPath, string_view resourcePath ) const override
            {
                return ModelImporter::importModel( sourcePath, _config.findMatchingRule( resourcePath ), importedPath );
            }

        private:
            const ModelImportConfig& _config;
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    bool ModelImporter::readModel( string_view sourcePath, const ModelImportRule& rule, vector<RHIVertex>& outListVertex )
    {
        ModelImportResult result{};
        const bool        bRead = readModelAsset( sourcePath, rule, result );
        outListVertex           = std::move( result._mesh._listVertex );
        return bRead;
    }

    bool ModelImporter::readModelAsset( string_view sourcePath, const ModelImportRule& rule, ModelImportResult& outResult )
    {
        outResult = ModelImportResult{};
        vector<uint8> bytes;
        if ( FileUtil::readFile( sourcePath, bytes ) == false || bytes.empty() )
        {
            SW_LOG_ERROR( "Failed to read glTF '%#'", sourcePath );
            return false;
        }
        const uint32 promotedCount = ModelImporterInternal::promoteSceneRootsToTopAncestor( bytes );
        if ( promotedCount > 0 )
            SW_LOG_WARNING( "glTF '%#': %# scene root node(s) have a parent (non-conforming exporter) - using their top-most ancestor instead", sourcePath,
                            promotedCount );

        // cgltf 는 GLB 의 JSON 청크와 BIN 청크를 @p bytes 안에서 가리킨다. cgltf_free 까지 bytes 가 살아 있어야 한다.
        const string        path    = string( sourcePath );
        const cgltf_options options = ModelImporterInternal::makeOptions();
        cgltf_data*         pData   = nullptr;
        cgltf_result        result  = cgltf_parse( &options, bytes.data(), bytes.size(), &pData );
        if ( result == cgltf_result_success )
            result = cgltf_load_buffers( &options, pData, path.c_str() );
        if ( result == cgltf_result_success )
            result = cgltf_validate( pData );
        if ( result != cgltf_result_success )
        {
            SW_LOG_ERROR( "Failed to read glTF '%#' (cgltf result %#)", sourcePath, static_cast<uint32>( result ) );
            cgltf_free( pData );
            return false;
        }

        vector<const cgltf_node*> listRoot;
        ModelImporterInternal::collectSceneRoots( *pData, listRoot );
        vector<ToonMaterialDesc> listToon;
        if ( ModelImporterInternal::readToonMaterials( *pData, sourcePath, listToon, outResult._listIgnoredMaterialKey ) == false ||
             ModelImporterInternal::readVrmAvatar( *pData, sourcePath, outResult ) == false )
        {
            cgltf_free( pData );
            return false;
        }
        bool bRead = true;
        if ( pData->skins_count > 0 )
        {
            const bool bMoves = rule._recenter != ModelRecenter::None || rule._arrTranslation[0] != 0.0f || rule._arrTranslation[1] != 0.0f || rule._arrTranslation[2] != 0.0f;
            if ( bMoves )
            {
                SW_LOG_ERROR( "glTF '%#' is skinned - import rule translation / recenter would break its bind matrices", sourcePath );
                bRead = false;
            }
            else
            {
                bRead = ModelImporterInternal::readSkinnedModel( *pData, listRoot, rule, sourcePath, listToon, outResult );
            }
            cgltf_free( pData );
            if ( bRead )
                ModelImporterInternal::warnMissingAvatarBones( outResult, sourcePath );
            return bRead;
        }

        ModelImporterInternal::MergedMesh merged;
        merged._bBakeMaterialColor = listToon.empty();
        for ( const cgltf_node* pRoot : listRoot )
        {
            if ( ModelImporterInternal::appendNode( *pRoot, merged ) == false )
            {
                SW_LOG_ERROR( "glTF '%#': a mesh under node '%#' has vertex data that cannot be read", sourcePath, ModelImporterInternal::makeNodeName( *pData, *pRoot ).c_str() );
                cgltf_free( pData );
                return false;
            }
        }
        if ( pData->animations_count > 0 )
            SW_LOG_WARNING( "glTF '%#' has %# animations but no skin - node animations are not imported", sourcePath, pData->animations_count );

        if ( merged._listIndex.empty() )
        {
            SW_LOG_ERROR( "glTF '%#' has no triangles in its default scene (%# primitives skipped)", sourcePath, merged._skippedPrimitiveCount );
            cgltf_free( pData );
            return false;
        }
        if ( merged._baseColorTextureUri.empty() == false && listToon.empty() )
            SW_LOG_INFO( "glTF '%#' uses base color texture '%#' - assign it through the material (PrimitiveLook)", sourcePath, merged._baseColorTextureUri.c_str() );

        ModelImporterInternal::applyRule( rule, merged );
        if ( listToon.empty() == false )
        {
            ModelImporterInternal::splitMaterialSections( merged, *pData, listToon, 0, outResult );
            if ( ModelImporterInternal::collectSectionTextures( *pData, sourcePath, outResult ) == false )
            {
                cgltf_free( pData );
                return false;
            }
        }
        cgltf_free( pData );
        ModelImporterInternal::expandTriangles( merged, 0, outResult._mesh );
        return true;
    }

    bool ModelImporter::importModel( string_view sourcePath, const ModelImportRule& rule, string_view outputPath )
    {
        ModelImportResult result{};
        if ( readModelAsset( sourcePath, rule, result ) == false )
            return false;

        // 옆 폴더는 임포트할 때마다 새로 쓴다. 규칙에서 빠진 클립과 사라진 부착 메시가 이전 파일로 남지 않게 하기 위해서다.
        const string sideFolder = makeImportedSideFolder( outputPath );
        if ( FileUtil::removeDirectory( sideFolder ) == false )
        {
            SW_LOG_ERROR( "Failed to clear the imported model folder %#", sideFolder.c_str() );
            return false;
        }
        if ( MeshAssetFormat::saveToFile( outputPath, result._mesh ) == false )
        {
            SW_LOG_ERROR( "Failed to write mesh asset %#", outputPath );
            return false;
        }
        if ( ModelImporterInternal::writeMaterialSections( result, sourcePath, sideFolder ) == false )
            return false;
        // 파쇄는 규칙에 `fracture` 가 있으면 `.mesh` 옆에 `.fracture` 를 쓰고, 없으면 이전 파일을 지운다. 규칙에서 빠진 파쇄가 남지 않게 하기 위해서다.
        const string fracturePath = FractureAsset::makePathForMesh( outputPath );
        if ( rule._bFracture == SW_TRUE )
        {
            if ( result._bSkinned == SW_TRUE )
            {
                SW_LOG_ERROR( "Fracture rule '%#' matched skinned model %# - only static meshes fracture", rule._name.c_str(), sourcePath );
                return false;
            }
            FractureAsset fracture;
            string        error;
            if ( MeshFractureUtil::fracture( result._mesh._listVertex, rule._fracture, fracture, error ) == false )
            {
                SW_LOG_ERROR( "Fracture of %# failed: %#", sourcePath, error.c_str() );
                return false;
            }
            if ( fracture.saveToFile( fracturePath ) == false )
            {
                SW_LOG_ERROR( "Failed to write fracture asset %#", fracturePath.c_str() );
                return false;
            }
            SW_LOG_INFO( "Fractured %# -> %# (%# pieces, %# links, %# levels, %# interior triangles)", sourcePath, fracturePath.c_str(), fracture.getPieceCount(),
                         fracture._graph._listLink.size(), fracture._graph.getDepthCount(), fracture.countTriangles( FractureSurfaceSlot::Interior ) );
        }
        else if ( FileUtil::exists( fracturePath ) && FileUtil::removeFile( fracturePath ) == false )
        {
            SW_LOG_ERROR( "Failed to remove stale fracture asset %#", fracturePath.c_str() );
            return false;
        }
        if ( result._bSkinned == SW_FALSE )
        {
            SW_LOG_INFO( "Imported model: %# -> %# (%# triangles)", sourcePath, outputPath, result._mesh._listVertex.size() / 3 );
            return true;
        }

        for ( const ModelImportAttachment& attachment : result._listAttachment )
        {
            const string partPath = FileUtil::joinPath( FileUtil::joinPath( sideFolder, ModelImporterInternal::kPartFolder ), attachment._fileStem + string( MeshAssetFormat::kExtension ) );
            if ( MeshAssetFormat::saveToFile( partPath, attachment._mesh ) == false )
            {
                SW_LOG_ERROR( "Failed to write attachment mesh %#", partPath.c_str() );
                return false;
            }
            SkeletonAttachment entry{};
            entry._name           = attachment._name;
            entry._meshPath       = ResourcePaths::toResourceID( partPath );
            entry._parentBone     = attachment._parentBone;
            entry._localTransform = attachment._localTransform;
            result._skeleton.addAttachment( entry );
        }
        const string stem         = FileUtil::removeExtension( FileUtil::getFileNamePart( outputPath ) );
        const string skeletonPath = FileUtil::joinPath( sideFolder, stem + string( Skeleton::kExtension ) );
        if ( result._skeleton.saveToFile( skeletonPath ) == false )
        {
            SW_LOG_ERROR( "Failed to write skeleton %#", skeletonPath.c_str() );
            return false;
        }
        if ( ModelImporterInternal::writeVrmAvatar( result, sourcePath, sideFolder, stem ) == false )
            return false;
        [[maybe_unused]] uint64 rawByteTotal        = 0;
        [[maybe_unused]] uint64 compressedByteTotal = 0;
        for ( const ModelImportClip& imported : result._listClip )
        {
            const string clipPath = FileUtil::joinPath( FileUtil::joinPath( sideFolder, ModelImporterInternal::kClipFolder ), imported._fileStem + string( AnimationClip::kExtension ) );
            if ( imported._clip.saveToFile( clipPath ) == false )
            {
                SW_LOG_ERROR( "Failed to write animation clip %#", clipPath.c_str() );
                return false;
            }
            rawByteTotal += imported._stats._rawByteCount;
            compressedByteTotal += imported._stats._compressedByteCount;
            SW_LOG_INFO( "Clip '%#' (%#): %# s, %# -> %# bytes (x%#), max error %# mm", imported._clip.getName().c_str(), rule._animationCodec.c_str(),
                         imported._clip.getDuration(), imported._stats._rawByteCount, imported._stats._compressedByteCount, imported._stats.computeRatio(),
                         imported._stats._maxError * 1000.0f );
        }
        SW_LOG_INFO( "Imported skinned model: %# -> %# (%# triangles, %# bones, %# attachments, %# clips, clips %# -> %# bytes)", sourcePath, outputPath,
                     result._mesh._listVertex.size() / 3, result._skeleton.getBoneCount(), result._listAttachment.size(), result._listClip.size(), rawByteTotal,
                     compressedByteTotal );
        return true;
    }

    bool ModelImporter::importChangedSourceModel( string_view relativePath )
    {
        if ( MeshAssetFormat::isMeshAssetPath( relativePath ) )
            return false;

        const string& resourceRoot = ResourcePaths::getRootFolderPath();
        const string  normalized   = FileUtil::normalizeSeparators( FileUtil::joinPath( resourceRoot, relativePath ) );
        if ( makeImportedModelPath( normalized ).empty() )
        {
            SW_LOG_WARNING( "모델 원본은 `%#` 아래에 있어야 임포트됩니다: %#", ModelImporterInternal::kRawModelFolder, relativePath );
            return true;
        }

        // 스탬프를 기록하는 경로가 하나여야 에디터에서 임포트한 것과 `App --import-models` 로 임포트한 것이 같은 판정을 받는다.
        // 설정 파일이 없으면 규칙 없이 임포트한다. 깨졌으면 기본 규칙으로 임포트하지 않는다(결과가 말없이 바뀐다) — 로드가 오류를 남겼다.
        ModelImportConfig config{};
        if ( config.loadFromFile( ModelImportConfig::makeDefaultConfigPath() ) == ConfigReadResult::Invalid )
            return true;
        const AssetImportSummary summary = importAllModels( resourceRoot, config, AssetImportMode::ImportStale );
        for ( const string& problem : summary._listProblem )
        {
            SW_LOG_ERROR( "모델 임포트 실패: %#", problem.c_str() );
        }
        SW_LOG_INFO( "모델 일괄 임포트: %# 바뀜 -> %#개 임포트함", relativePath, summary._importedCount );
        return true;
    }

    AssetImportSummary ModelImporter::importAllModels( string_view resourceRoot, const ModelImportConfig& config, AssetImportMode mode )
    {
        const ModelRawImporter importer{ config };
        return AssetImportStampUtil::importAll( resourceRoot, importer, mode );
    }

    string ModelImporter::makeImportedModelPath( string_view rawModelPath )
    {
        return AssetImportStampUtil::makeImportedPath( rawModelPath, ModelImporterInternal::kRawModelFolder, ModelImporterInternal::kImportedModelFolder,
                                                       MeshAssetFormat::kExtension );
    }

    string ModelImporter::makeImportedSideFolder( string_view importedMeshPath )
    {
        return FileUtil::removeExtension( importedMeshPath );
    }

    string ModelImporter::makeClipDataPath( string_view sourcePath )
    {
        return FileUtil::removeExtension( sourcePath ) + string( ModelImporterInternal::kClipDataSuffix );
    }

    uint64 ModelImporter::computeSourceHash( string_view sourcePath, const ModelImportRule& rule )
    {
        vector<uint8> bytes;
        if ( FileUtil::readFile( sourcePath, bytes ) == false || bytes.empty() )
            return 0;

        StringBuilder<constant::kMaxBuffer64> versionText;
        versionText.appendFormat( "importer=%#;format=%#;recenter=%#;clip=%#", ModelImporterInternal::kImporterVersion, MeshAssetFormat::kVersion,
                                  static_cast<uint32>( rule._recenter ), AnimationClip::kVersion );
        uint64 hash = HashUtil::computeStringHash64( reinterpret_cast<const utf8*>( bytes.data() ), bytes.size(), false );
        hash        = HashUtil::computeStringHash64( versionText.c_str(), versionText.size(), false, hash );
        // 이동은 비트 그대로 섞는다. 문자열로 반올림하면 작은 변경이 같은 해시가 된다.
        hash                       = HashUtil::computeStringHash64( reinterpret_cast<const utf8*>( rule._arrTranslation ), sizeof( rule._arrTranslation ), false, hash );
        const string animationText = rule.makeAnimationHashText();
        hash                       = HashUtil::computeStringHash64( animationText.c_str(), animationText.size(), false, hash );
        // VRM 이면 아바타 파일 형식 버전도 섞는다. 그 형식만 바뀌었을 때 VRM 원본만 다시 임포트하게 한다(임포터 버전을 올리면 모든 모델이 어긋난다).
        if ( ModelImporterInternal::containsVrmExtension( bytes ) )
        {
            const string avatarText = "avatar=" + to_string( VrmAvatarImporter::kVersion );
            hash                    = HashUtil::computeStringHash64( avatarText.c_str(), avatarText.size(), false, hash );
        }
        const string fractureText = rule.makeFractureHashText();
        if ( fractureText.empty() == false )
        {
            const string fractureVersion = fractureText + ";format=" + to_string( FractureAsset::kVersion ) + ";algorithm=" + to_string( MeshFractureUtil::kAlgorithmVersion );
            hash                         = HashUtil::computeStringHash64( fractureVersion.c_str(), fractureVersion.size(), false, hash );
        }
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
        const cgltf_options options = ModelImporterInternal::makeOptions();
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
            if ( FileUtil::readFile( FileUtil::joinPath( directory, pUri ), bufferBytes ) && bufferBytes.empty() == false )
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
        (void)FileUtil::collectFiles( sideFolder, "", listFile, true );
        for ( string& file : listFile )
        {
            file = FileUtil::normalizeSeparators( file );
        }
        std::sort( listFile.begin(), listFile.end() );
        for ( const string& file : listFile )
        {
            string relative = file;
            if ( relative.size() > sideFolder.size() )
                relative = relative.substr( sideFolder.size() );
            hash                  = HashUtil::computeStringHash64( relative.c_str(), relative.size(), false, hash );
            const uint64 fileHash = AssetImportStampUtil::computeFileHash( file );
            hash                  = HashUtil::computeStringHash64( reinterpret_cast<const utf8*>( &fileHash ), sizeof( fileHash ), false, hash );
        }
        return hash;
    }
} // namespace sw::editor
