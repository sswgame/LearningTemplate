#include "pch.h"

#include "Engine/Animation/Scene/VertexAnimationCookList.h"

#include "Core/Log/Logger.h"

#include "Engine/Content/AssetID.h"
#include "Engine/Content/AssetIO.h"
#include "Engine/Content/AssetTypeRegistry.h"
#include "Engine/Graphics/Mesh/MeshAssetFormat.h"
#include "Engine/Serialization/XML/XMLDocument.h"
#include "Engine/Serialization/XML/XMLFields.h"
#include "Engine/Serialization/XML/XMLNameCheck.h"
#include "Engine/Serialization/XML/XMLReadUtil.h"

namespace sw
{
    bool VertexAnimationCookList::loadFromXMLText( string_view xml, string_view sourceLabel )
    {
        *this = VertexAnimationCookList{};
        XMLDocument document;
        XMLNode     root;
        if ( XMLReadUtil::parseRoot( document, xml, sourceLabel, "VertexAnimationCookList", root ) != XMLRootStatus::Found )
        {
            SW_LOG_ERROR_ID( "VertexAnimationCooker.VertexAnimationListMalformed", "Vertex animation list '%#': malformed XML (expected a <VertexAnimationCookList> root)", sourceLabel );
            return false;
        }
        if ( parseRoot( root, sourceLabel ) )
            return true;
        *this = VertexAnimationCookList{};
        return false;
    }

    bool VertexAnimationCookList::loadFromAsset( const AssetID& assetID )
    {
        const string_view path = assetID.view();
        string            text;
        if ( AssetIO::readTextAsset( assetID, text ) == false )
        {
            SW_LOG_ERROR_ID( "VertexAnimationCooker.VertexAnimationListCould", "Vertex animation list '%#' could not be read", path );
            *this = VertexAnimationCookList{};
            return false;
        }
        return loadFromXMLText( text, path );
    }

    bool VertexAnimationCookList::parseRoot( const XMLNode& root, string_view sourceLabel )
    {
        static constexpr const utf8* kArrRootChild[] = { "clips" };
        if ( XMLFields::readProperties( root, *this, sourceLabel ) == false || XMLNameCheck::reportUnknownChildren( root, kArrRootChild, sourceLabel ) == false )
            return false;
        const XMLNode clips                 = root.findChild( "clips" );
        const bool    bInvalidAnimationList = clips.isValid() == false || clips.findChild().isValid() == false;
        if ( bInvalidAnimationList )
        {
            SW_LOG_ERROR_ID( "VertexAnimationCooker.VertexAnimationListNeeds", "Vertex animation list '%#' needs a non-empty <clips> list", sourceLabel );
            return false;
        }
        for ( XMLNode clip = clips.findChild(); clip.isValid(); clip = clip.findNextSibling() )
        {
            if ( string_view{ clip.getName() } != "item" )
            {
                SW_LOG_ERROR_ID( "VertexAnimationCooker.VertexAnimationListClips", "Vertex animation list '%#': <clips> must hold <item> names", sourceLabel );
                return false;
            }
            _listClip.push_back( hashed_string( string_view{ clip.getText() } ) );
        }
        // 가리키는 파일이 모두 있어야 한다 — 이름이 틀린 목록이 쿠킹 때 조용히 빠지지 않게.
        if ( AssetIO::hasAsset( _meshPath.id() ) == false || AssetIO::hasAsset( _skeletonPath.id() ) == false )
        {
            SW_LOG_ERROR_ID( "VertexAnimationCooker.VertexAnimationListMesh", "Vertex animation list '%#': mesh '%#' or skeleton '%#' does not exist", sourceLabel, _meshPath.c_str(), _skeletonPath.c_str() );
            return false;
        }
        // 쿠킹 작업의 입력으로 기록되어야 하므로 공유 캐시가 아니라 이 자리에서 읽는다(`CookJob` — 캐시가 쥔 한 벌을 쓰면 입력이 빠진다).
        if ( _animationSet.loadCopyFromAsset( _animationSetPath.id() ) == false )
            return false; // 셋이 까닭을 남겼다
        for ( const hashed_string& clipName : _listClip )
        {
            const string clipPath = makeClipPath( clipName );
            if ( clipPath.empty() || AssetIO::hasAsset( AssetID{ clipPath } ) == false )
            {
                SW_LOG_ERROR_ID( "VertexAnimationCooker.VertexAnimationListClip", "Vertex animation list '%#': clip '%#' does not exist in '%#'", sourceLabel, clipName.c_str(), _animationSetPath.c_str() );
                return false;
            }
        }
        return true;
    }

    string VertexAnimationCookList::makeClipPath( const hashed_string& clipName ) const
    {
        const string* const pClipPath = _animationSet.findClipPath( clipName );
        return pClipPath != nullptr ? *pClipPath : string{};
    }
} // namespace sw

namespace sw
{

    /** @brief VAT 쿠킹 목록 데이터 파일 종류입니다(스키마 테스트와 카탈로그 편집기가 이 줄로 파일을 읽는다). */
    SW_ASSET_TYPE( VertexAnimationCookList, VertexAnimationCookList::kAssetType.withPathRule( AssetPathRule{ {}, {}, VertexAnimationCookList::kExtension } ).withLoader( AssetTypeLoader::of<VertexAnimationCookList>() ) );
} // namespace sw
