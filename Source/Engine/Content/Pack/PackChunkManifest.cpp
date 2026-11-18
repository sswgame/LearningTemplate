#include "pch.h"

#include "Engine/Content/Pack/PackChunkManifest.h"

#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"
#include "Core/String/StringUtil.h"

#include "Engine/Serialization/XML/XMLDocument.h"

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
                const string_view name   = chunk.getText();
                bool              bKnown = false;
                for ( const PackChunkFiles& files : outManifest._listChunk )
                {
                    bKnown = bKnown || files._name == name;
                }
                if ( bKnown == false )
                {
                    SW_LOG_ERROR_ID( "PackChunkManifest.UnknownChunk", "Pack chunk manifest %#: scene '%#' needs chunk '%#' that the manifest does not list", sourceName, entry._sceneID, name );
                    return false;
                }
                entry._listChunk.emplace_back( name );
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

    void PackChunkManifest::appendScenePackFiles( string_view sceneID, vector<string>& outListPackFileName ) const
    {
        const string normalized = FileUtil::normalizePath( sceneID ).str();
        for ( const PackSceneEntry& entry : _listScene )
        {
            if ( entry._sceneID != normalized )
                continue;
            for ( const string& chunkName : entry._listChunk )
            {
                for ( const PackChunkFiles& files : _listChunk )
                {
                    if ( files._name == chunkName )
                        outListPackFileName.insert( outListPackFileName.end(), files._listPackFileName.begin(), files._listPackFileName.end() );
                }
            }
            return;
        }
    }
} // namespace sw
