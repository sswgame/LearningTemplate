#include "pch.h"

#include "Engine/Animation/Skeletal/SkeletonBoneLOD.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"
#include "Core/Memory/Memory.h"
#include "Core/String/StringUtil.h"

#include "Engine/Animation/Skeletal/Skeleton.h"
#include "Engine/Asset/Cook/AssetCompanion.h"
#include "Engine/Content/AssetIO.h"
#include "Engine/Graphics/Mesh/Mesh.h"
#include "Engine/Serialization/Inherit/DataInheritance.h"
#include "Engine/Serialization/Inherit/ResourceComposer.h"
#include "Engine/Serialization/XML/XMLDocument.h"
#include "Engine/Serialization/XML/XMLFields.h"
#include "Engine/Serialization/XML/XMLReadUtil.h"

namespace sw
{
    namespace
    {
        struct SkeletonBoneLODInternal
        {
            /** @brief 남는 본의 부모 사슬을 남깁니다 — 자식이 뒤에 있으므로 뒤에서 앞으로 한 번 훑는다. */
            static void keepAncestors( const Skeleton& skeleton, vector<uint8>& inoutMask )
            {
                for ( uint32 boneIndex = skeleton.getBoneCount(); boneIndex-- > 0; )
                {
                    const int32 parentIndex = skeleton.getBone( boneIndex )._parentIndex;
                    if ( inoutMask[boneIndex] == SW_TRUE && parentIndex >= 0 )
                        inoutMask[static_cast<uint32>( parentIndex )] = SW_TRUE;
                }
            }

            static void markBone( const Skeleton& skeleton, const hashed_string& name, vector<uint8>& inoutMask )
            {
                const optional<uint32> boneIndex = skeleton.findBoneIndex( name );
                if ( boneIndex.has_value() )
                    inoutMask[*boneIndex] = SW_TRUE;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    bool SkeletonBoneLOD::loadFromXMLText( string_view xml, string_view sourceLabel )
    {
        DataInheritanceResourceSource source;
        return loadFromXMLText( xml, sourceLabel, source );
    }

    bool SkeletonBoneLOD::loadFromXMLText( string_view xml, string_view sourceLabel, IDataInheritanceSource& source )
    {
        _listLevel.clear();
        _listCoreBone.clear();
        // 변형은 부모 파일을 가리키고 차이만 적는다 — 펼친 한 벌을 읽는다(부모 없이 자식 칸만으로 읽지 않는다).
        if ( DataInheritance::findBasePath( xml ).empty() )
            return parseFlat( xml, sourceLabel );
        string flatText;
        if ( DataInheritance::flatten( xml, sourceLabel, makeInheritanceSchema(), source, flatText ) == false )
            return false;
        return parseFlat( flatText, sourceLabel );
    }

    bool SkeletonBoneLOD::parseFlat( string_view xml, string_view sourceLabel )
    {
        XMLDocument document;
        XMLNode     root;
        if ( XMLReadUtil::parseRoot( document, xml, sourceLabel, "SkeletonBoneLOD", root ) != XMLRootStatus::Found )
        {
            SW_LOG_ERROR_ID( "SkeletonBoneLOD.BoneLODMalformedXML", "Bone LOD '%#': malformed XML (expected a <SkeletonBoneLOD> root)", sourceLabel );
            return false;
        }
        if ( parseRoot( root, sourceLabel ) )
        {
            _revision = IdentityToken::make(); // 번호 발급기 대신 정체성 토큰 — 마스크를 만든 쪽이 들고 비교한다
            return true;
        }
        _listLevel.clear();
        _listCoreBone.clear();
        return false;
    }

    bool SkeletonBoneLOD::loadFromAsset( const AssetID& assetID )
    {
        const string_view path = assetID.view();
        SW_MEMORY_SCOPE( Animation );
        string text;
        if ( AssetIO::readTextAsset( assetID, text ) == false )
        {
            SW_LOG_ERROR_ID( "SkeletonBoneLOD.BoneLODCouldNot", "Bone LOD '%#' could not be read", path );
            _listLevel.clear();
            _listCoreBone.clear();
            return false;
        }
        // 리소스 id 로 합친다 — 부모 · 조건에 더해 덮어쓰기 층 · 다시 읽기 기록 · 접속 대조를 받는다(글 입구 `loadFromXMLText` 는 id 가 없다).
        _listLevel.clear();
        _listCoreBone.clear();
        string flatText;
        bool   bComposed = false;
        if ( ResourceComposer::compose( text, path, ResourceContext::forSchema( &SkeletonBoneLOD::makeInheritanceSchema ).withAsset( assetID ), flatText, bComposed ) == false )
            return false;
        return parseFlat( bComposed ? string_view{ flatText } : string_view{ text }, path );
    }

    bool SkeletonBoneLOD::parseRoot( const XMLNode& root, string_view sourceLabel )
    {
        if ( XMLReadUtil::hasOnlyKnownNames( root, {}, { "coreBones", "levels" }, sourceLabel ) == false )
            return false;
        const XMLNode coreBones = root.findChild( "coreBones" );
        for ( XMLNode bone = coreBones.isValid() ? coreBones.findChild() : XMLNode{}; bone.isValid(); bone = bone.findNextSibling() )
        {
            const hashed_string name = XMLReadUtil::readOptionalName( bone, "name" );
            if ( string_view{ bone.getName() } != "bone" || XMLReadUtil::hasOnlyKnownNames( bone, { "name" }, {}, sourceLabel ) == false || name.empty() )
            {
                SW_LOG_ERROR_ID( "SkeletonBoneLOD.CoreBonesHolds", "Bone LOD '%#': <coreBones> holds only <bone name=\"...\"/> elements", sourceLabel );
                return false;
            }
            _listCoreBone.push_back( name );
        }
        const XMLNode levels = root.findChild( "levels" );
        size_t        levelIndex{ 0 };
        for ( XMLNode level = levels.isValid() ? levels.findChild() : XMLNode{}; level.isValid(); level = level.findNextSibling(), ++levelIndex )
        {
            SkeletonBoneLODLevel entry{};
            if ( string_view{ level.getName() } != "level" || XMLFields::readFields( level, entry, sourceLabel ) == false ||
                 XMLReadUtil::hasOnlyKnownNames( level, { "max_screen_size" }, { "remove" }, sourceLabel ) == false )
            {
                SW_LOG_ERROR_ID( "SkeletonBoneLOD.BoneLODLevelsHolds", "Bone LOD '%#': <levels> holds only <level> elements", sourceLabel );
                return false;
            }
            const XMLNode removed          = level.findChild( "remove" );
            const bool    bInvalidLODLevel = removed.isValid() == false || removed.findChild().isValid() == false;
            if ( bInvalidLODLevel )
            {
                SW_LOG_ERROR_ID( "SkeletonBoneLOD.BoneLODLevelNeeds", "Bone LOD '%#': level %# needs a non-empty <remove> list", sourceLabel, levelIndex );
                return false;
            }
            if ( _listLevel.empty() == false && entry._maxScreenSize >= _listLevel.back()._maxScreenSize )
            {
                SW_LOG_ERROR_ID( "SkeletonBoneLOD.BoneLODLevelMax", "Bone LOD '%#': level %# max_screen_size %# must be smaller than the previous level's", sourceLabel, levelIndex, entry._maxScreenSize );
                return false;
            }
            for ( XMLNode bone = removed.findChild(); bone.isValid(); bone = bone.findNextSibling() )
            {
                if ( string_view{ bone.getName() } != "item" )
                {
                    SW_LOG_ERROR_ID( "SkeletonBoneLOD.BoneLODLevelRemove", "Bone LOD '%#': level %# <remove> must hold <item> bone names", sourceLabel, levelIndex );
                    return false;
                }
                const hashed_string boneName( string_view{ bone.getText() } );
                // 같은 파일이 선언한 핵심 본을 빼면 읽기에서 멈춘다(부모 사슬 · 사용처의 본은 스켈레톤을 알아야 해서 `computeMasks` 가 본다).
                if ( VectorUtil::contains( _listCoreBone, boneName ) )
                {
                    SW_LOG_ERROR_ID( "SkeletonBoneLOD.CoreBoneRemoved", "Bone LOD '%#': level %# removes core bone '%#'", sourceLabel, levelIndex, boneName.c_str() );
                    return false;
                }
                entry._listRemovedBone.push_back( boneName );
            }
            _listLevel.push_back( std::move( entry ) );
        }
        return true;
    }

    uint32 SkeletonBoneLOD::selectLevel( float32 screenSize ) const
    {
        uint32 level = 0;
        for ( uint32 levelIndex = 0; levelIndex < static_cast<uint32>( _listLevel.size() ); ++levelIndex )
        {
            if ( screenSize <= _listLevel[levelIndex]._maxScreenSize )
                level = levelIndex + 1;
        }
        return level;
    }

    bool SkeletonBoneLOD::computeMasks( const Skeleton& skeleton, const vector<uint8>& requiredMask, vector<vector<uint8>>& outListMask, string_view sourceLabel ) const
    {
        outListMask.clear();
        const uint32  boneCount = skeleton.getBoneCount();
        vector<uint8> mask( boneCount, SW_TRUE );
        for ( const SkeletonBoneLODLevel& level : _listLevel )
        {
            for ( const hashed_string& boneName : level._listRemovedBone )
            {
                const optional<uint32> boneIndex = skeleton.findBoneIndex( boneName );
                if ( boneIndex.has_value() == false )
                {
                    SW_LOG_ERROR_ID( "SkeletonBoneLOD.BoneLODBoneNot", "Bone LOD '%#': bone '%#' is not in the skeleton", sourceLabel, boneName.c_str() );
                    outListMask.clear();
                    return false;
                }
                mask[*boneIndex] = SW_FALSE;
            }
            // 부모가 앞에 있으므로 한 번 훑으면 뺀 본의 자손이 모두 빠진다.
            for ( uint32 boneIndex = 0; boneIndex < boneCount; ++boneIndex )
            {
                const int32 parentIndex = skeleton.getBone( boneIndex )._parentIndex;
                if ( parentIndex >= 0 && mask[static_cast<uint32>( parentIndex )] == SW_FALSE )
                    mask[boneIndex] = SW_FALSE;
            }
            // 손 파일이 핵심 본(사용처가 가리키는 본 · 그 부모 사슬)을 빼면 조용히 넘기지 않는다 — 소켓 · IK 대상이 레퍼런스 포즈로 굳는다.
            for ( uint32 boneIndex = 0; boneIndex < boneCount && boneIndex < requiredMask.size(); ++boneIndex )
            {
                if ( requiredMask[boneIndex] == SW_TRUE && mask[boneIndex] == SW_FALSE )
                {
                    SW_LOG_ERROR_ID( "SkeletonBoneLOD.CoreBoneRemoved", "Bone LOD '%#': a level removes '%#', a core bone or the parent of one (sockets, rig, physics, notifies, <coreBones>) - the skeletalMesh keeps every bone",
                                     sourceLabel, skeleton.getBone( boneIndex )._name.c_str() );
                    outListMask.clear();
                    return false;
                }
            }
            outListMask.push_back( mask );
        }
        return true;
    }

    bool SkeletonBoneLOD::computeRequiredMask( const Skeleton& skeleton, const SkeletonBoneLOD* pBoneLOD, const vector<hashed_string>& listUserBone, vector<uint8>& outRequiredMask, string_view sourceLabel )
    {
        using Internal = SkeletonBoneLODInternal;
        outRequiredMask.assign( skeleton.getBoneCount(), SW_FALSE );
        bool bValid = true;
        if ( pBoneLOD != nullptr )
        {
            for ( const hashed_string& name : pBoneLOD->_listCoreBone )
            {
                if ( skeleton.findBoneIndex( name ).has_value() == false )
                {
                    SW_LOG_ERROR_ID( "SkeletonBoneLOD.CoreBoneUnknown", "Bone LOD '%#': core bone '%#' is not in the skeleton", sourceLabel, name.c_str() );
                    bValid = false;
                    continue;
                }
                Internal::markBone( skeleton, name, outRequiredMask );
            }
        }
        // 스켈레톤 자체의 소켓(변형이 더한 자리)과 임포트 어태치먼트는 데이터에서 바로 나온다.
        for ( const SkeletonAttachment& attachment : skeleton.getAttachments() )
        {
            Internal::markBone( skeleton, attachment._parentBone, outRequiredMask );
        }
        for ( const SkeletonSocket& socket : skeleton.getSockets() )
        {
            Internal::markBone( skeleton, socket._parentBone, outRequiredMask );
        }
        for ( const hashed_string& name : listUserBone )
        {
            Internal::markBone( skeleton, name, outRequiredMask );
        }
        Internal::keepAncestors( skeleton, outRequiredMask );
        if ( bValid == false )
            outRequiredMask.clear();
        return bValid;
    }

    void SkeletonBoneLOD::computeMeshMasks( const Skeleton& skeleton, const Mesh& mesh, const vector<uint8>& requiredMask, vector<vector<uint8>>& outListMask )
    {
        outListMask.clear();
        const uint32                  boneCount = skeleton.getBoneCount();
        const vector<MeshSkinVertex>& listSkin  = mesh.getLODSkinVertices();
        for ( const MeshLOD& lod : mesh.getLODs() )
        {
            vector<uint8> mask = requiredMask;
            mask.resize( boneCount, SW_FALSE );
            const size_t first = static_cast<size_t>( lod._firstVertex - mesh.getVertexCount() ); // 낮은 단계 정점은 LOD 0 뒤에 이어진다(`Mesh::getLODSkinVertices`)
            for ( size_t vertex = first; vertex < first + lod._vertexCount && vertex < listSkin.size(); ++vertex )
            {
                for ( uint32 slot = 0; slot < 4u; ++slot )
                {
                    const uint16 joint = listSkin[vertex]._arrJoint[slot];
                    if ( listSkin[vertex]._arrWeight[slot] > 0.0f && joint < boneCount )
                        mask[joint] = SW_TRUE;
                }
            }
            SkeletonBoneLODInternal::keepAncestors( skeleton, mask );
            outListMask.push_back( std::move( mask ) );
        }
    }

    DataInheritanceSchema SkeletonBoneLOD::makeInheritanceSchema()
    {
        DataInheritanceSchema schema{ "SkeletonBoneLOD" };
        DataInheritanceSchema coreBones{ "coreBones" };
        coreBones._keyAttribute = "name";
        schema._listField.push_back( std::move( coreBones ) );
        return schema; // `levels` 는 규칙에 없어 자식이 적으면 통째로 바뀐다(단계 표는 한 벌로 맞춘다)
    }

    string SkeletonBoneLOD::makePathForSkeleton( string_view skeletonPath )
    {
        if ( StringUtil::endsWith( skeletonPath, Skeleton::kExtension ) == false )
            return string{};
        const string_view stemPath = skeletonPath.substr( 0, skeletonPath.size() - Skeleton::kExtension.size() );
        string_view       folder;
        string_view       stem;
        FileUtil::splitPath( stemPath, folder, stem );
        if ( folder.empty() == false )
            folder.remove_suffix( 1 ); // 끝 구분자
        string_view folderName;
        FileUtil::getFileNamePart( folder, folderName );
        // 임포트 옆 폴더(폴더 이름 = 파일 이름)는 다시 임포트할 때 지워진다. 그 밖의 메시 옆에 둔다.
        string path = ( folder.empty() == false && folderName == stem ) ? string{ folder } : string{ stemPath };
        path.append( kExtension.data(), kExtension.size() );
        return path;
    }

    void SkeletonBoneLOD::makeSkeletonCandidatePaths( string_view boneLODPath, string& outImportedPath, string& outSiblingPath )
    {
        outImportedPath.clear();
        outSiblingPath.clear();
        if ( StringUtil::endsWith( boneLODPath, kExtension ) == false )
            return;
        const string_view stemPath = boneLODPath.substr( 0, boneLODPath.size() - kExtension.size() );
        string_view       stem;
        FileUtil::getFileNamePart( stemPath, stem );
        outImportedPath = string{ stemPath } + "/" + string{ stem } + string{ Skeleton::kExtension };
        outSiblingPath  = string{ stemPath } + string{ Skeleton::kExtension };
    }
} // namespace sw

namespace sw
{
    SW_DATA_INHERITANCE_SCHEMA( SkeletonBoneLOD, &SkeletonBoneLOD::makeInheritanceSchema );
} // namespace sw

// 스켈레톤 옆의 본 LOD(`SkeletalMeshComponent` 가 스켈레톤 경로에서 이 이름으로 찾는다) — 스켈레톤이 아니면 빈 글이다.
SW_ASSET_COMPANION( SkeletonBoneLOD, &::sw::SkeletonBoneLOD::makePathForSkeleton );
