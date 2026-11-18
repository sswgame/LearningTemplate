#include "pch.h"

#include "Engine/Content/Pack/PackChunkManifest.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"
#include "Core/String/StringUtil.h"

#include "Engine/Serialization/XML/XMLDocument.h"

#include <algorithm>

namespace sw
{
    namespace
    {
        struct PackChunkManifestInternal
        {
            static constexpr const utf8* kRootName  = "PackChunks";
            static constexpr const utf8* kChunkName = "Chunk";
            static constexpr const utf8* kPackName  = "Pack";
            static constexpr const utf8* kSceneName = "Scene";
            static constexpr const utf8* kNameAttr  = "name";
            static constexpr const utf8* kIDAttr    = "id";

            /** @brief 팩은 목록과 같은 폴더의 파일이다 — 폴더를 벗어나는 이름을 받지 않는다. */
            static bool isPlainFileName( string_view name ) { return name.empty() == false && name.find_first_of( "/\\" ) == string_view::npos && name != ".."; }
        };
    } // namespace
} // namespace sw

namespace sw
{
    bool PackChunkManifest::parse( string_view xmlText, string_view sourceName, PackChunkManifest& outManifest )
    {
        using Internal = PackChunkManifestInternal;
        outManifest    = PackChunkManifest{};
        XMLDocument document;
        if ( document.parse( xmlText, sourceName ) == false )
        {
            SW_LOG_ERROR_ID( "PackChunkManifest.NotXML", "Pack chunk manifest %# is not valid XML: %#", sourceName, document.getLastError() );
            return false;
        }
        const XMLNode root = document.getRoot( Internal::kRootName );
        if ( root.isValid() == false )
        {
            SW_LOG_ERROR_ID( "PackChunkManifest.NoRoot", "Pack chunk manifest %# has no <%#> root", sourceName, Internal::kRootName );
            return false;
        }
        for ( XMLNode chunk = root.findChild( Internal::kChunkName ); chunk.isValid(); chunk = chunk.findNextSibling( Internal::kChunkName ) )
        {
            PackChunkFiles& files = outManifest._listChunk.emplace_back();
            files._name           = string( chunk.getAttributeText( Internal::kNameAttr ) );
            for ( XMLNode pack = chunk.findChild( Internal::kPackName ); pack.isValid(); pack = pack.findNextSibling( Internal::kPackName ) )
            {
                const string_view packName = pack.getText();
                if ( Internal::isPlainFileName( packName ) == false )
                {
                    SW_LOG_ERROR_ID( "PackChunkManifest.PackName", "Pack chunk manifest %#: chunk '%#' names pack '%#' - it must be a plain file name in the pack folder", sourceName, files._name, packName );
                    return false;
                }
                files._listPackFileName.emplace_back( packName );
            }
            if ( files._name.empty() || files._listPackFileName.empty() )
            {
                SW_LOG_ERROR_ID( "PackChunkManifest.EmptyChunk", "Pack chunk manifest %#: a chunk needs a name and at least one pack ('%#')", sourceName, files._name );
                return false;
            }
        }
        for ( XMLNode scene = root.findChild( Internal::kSceneName ); scene.isValid(); scene = scene.findNextSibling( Internal::kSceneName ) )
        {
            PackSceneEntry& entry = outManifest._listScene.emplace_back();
            entry._sceneID        = FileUtil::normalizePath( scene.getAttributeText( Internal::kIDAttr ) ).str();
            for ( XMLNode chunk = scene.findChild( Internal::kChunkName ); chunk.isValid(); chunk = chunk.findNextSibling( Internal::kChunkName ) )
            {
                entry._listChunk.emplace_back( chunk.getText() ); // 다른 도메인의 청크일 수 있다 — 합친 뒤 씬을 열 때 본다(`appendScenePackFiles`)
            }
        }
        return true;
    }

    string PackChunkManifest::makeText( const PackChunkManifest& manifest )
    {
        using Internal = PackChunkManifestInternal;
        XMLDocument   document;
        const XMLNode root = document.appendRoot( Internal::kRootName );
        for ( const PackChunkFiles& files : manifest._listChunk )
        {
            const XMLNode chunk = root.appendChild( Internal::kChunkName );
            chunk.appendAttribute( Internal::kNameAttr, string_view( files._name ) );
            for ( const string& packName : files._listPackFileName )
            {
                chunk.appendChild( Internal::kPackName, packName );
            }
        }
        for ( const PackSceneEntry& entry : manifest._listScene )
        {
            const XMLNode scene = root.appendChild( Internal::kSceneName );
            scene.appendAttribute( Internal::kIDAttr, string_view( entry._sceneID ) );
            for ( const string& chunkName : entry._listChunk )
            {
                scene.appendChild( Internal::kChunkName, chunkName );
            }
        }
        return document.saveToString();
    }

    string PackChunkManifest::makeDomainStem( string_view domain )
    {
        string stem = FileUtil::trimTrailingSlashes( FileUtil::normalizePath( domain ).str() );
        std::replace( stem.begin(), stem.end(), '/', '_' );
        return stem;
    }

    string PackChunkManifest::makeFileName( string_view domain )
    {
        return string( kFilePrefix ) + makeDomainStem( domain ) + string( kFileSuffix );
    }

    bool PackChunkManifest::isFileName( string_view fileName )
    {
        return fileName.size() > kFilePrefix.size() + kFileSuffix.size() && StringUtil::startsWith( fileName, kFilePrefix, true ) && StringUtil::endsWith( fileName, kFileSuffix, true );
    }

    bool PackChunkManifest::merge( const PackChunkManifest& other, string_view sourceName )
    {
        for ( const PackChunkFiles& files : other._listChunk )
        {
            for ( const PackChunkFiles& existing : _listChunk )
            {
                if ( existing._name == files._name )
                {
                    SW_LOG_ERROR_ID( "PackChunkManifest.DuplicateChunk", "Pack chunk manifest %# lists chunk '%#' that another manifest in the folder also lists", sourceName, files._name );
                    return false;
                }
            }
        }
        for ( const PackSceneEntry& entry : other._listScene )
        {
            for ( const PackSceneEntry& existing : _listScene )
            {
                if ( existing._sceneID == entry._sceneID )
                {
                    SW_LOG_ERROR_ID( "PackChunkManifest.DuplicateScene", "Pack chunk manifest %# lists scene '%#' that another manifest in the folder also lists", sourceName, entry._sceneID );
                    return false;
                }
            }
        }
        _listChunk.insert( _listChunk.end(), other._listChunk.begin(), other._listChunk.end() );
        _listScene.insert( _listScene.end(), other._listScene.begin(), other._listScene.end() );
        VectorUtil::sort( _listChunk, []( const PackChunkFiles& left, const PackChunkFiles& right )
        { return left._name < right._name; } );
        VectorUtil::sort( _listScene, []( const PackSceneEntry& left, const PackSceneEntry& right )
        { return left._sceneID < right._sceneID; } );
        return true;
    }

    bool PackChunkManifest::isDeferredPack( string_view packFileName ) const
    {
        for ( const PackChunkFiles& files : _listChunk )
        {
            for ( const string& name : files._listPackFileName )
            {
                if ( StringUtil::equals( name, packFileName, true ) )
                    return true;
            }
        }
        return false;
    }

    bool PackChunkManifest::appendScenePackFiles( string_view sceneID, vector<string>& outListPackFileName ) const
    {
        const string normalized = FileUtil::normalizePath( sceneID ).str();
        for ( const PackSceneEntry& entry : _listScene )
        {
            if ( entry._sceneID != normalized )
                continue;
            for ( const string& chunkName : entry._listChunk )
            {
                const PackChunkFiles* pFiles = nullptr;
                for ( const PackChunkFiles& files : _listChunk )
                {
                    if ( files._name == chunkName )
                        pFiles = &files;
                }
                if ( pFiles == nullptr )
                {
                    SW_LOG_ERROR_ID( "PackChunkManifest.UnknownChunk", "Scene '%#' needs pack chunk '%#' that no chunk manifest in the pack folder lists - cook its domain again", normalized, chunkName );
                    return false;
                }
                outListPackFileName.insert( outListPackFileName.end(), pFiles->_listPackFileName.begin(), pFiles->_listPackFileName.end() );
            }
            return true;
        }
        return true;
    }
} // namespace sw
