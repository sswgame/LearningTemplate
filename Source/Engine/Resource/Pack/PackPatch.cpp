#include "pch.h"

#include "Engine/Resource/Pack/PackPatch.h"

#include "Core/Common/StdHeaders.h"
#include "Core/Container/unordered_map.h"
#include "Core/Log/Logger.h"

#include "Engine/Serialization/XML/XMLDocument.h"

namespace sw
{
    SW_LOG_CALLER( "PackPatch" );

    namespace
    {
        struct PackPatchInternal
        {
            static constexpr const utf8* kRootName    = "PackPatch";
            static constexpr const utf8* kRemovedName = "Removed";

            /** @brief 경로 → 내용 해시 표를 만듭니다. 같은 경로가 두 번이면 오류를 남기고 false 입니다. */
            static bool makePathTable( const vector<PackContentEntry>& listEntry, const utf8* pWhich, unordered_map<string, uint64>& outMapPathToHash )
            {
                for ( const PackContentEntry& entry : listEntry )
                {
                    if ( outMapPathToHash.emplace( entry._path, entry._contentHash ).second == false )
                    {
                        SW_LOG_ERROR( "Pack delta: the %# build lists '%#' twice - cannot tell which content is right", pWhich, entry._path );
                        return false;
                    }
                }
                return true;
            }
        };
    } // namespace

    bool PackPatchUtil::computeDelta( const vector<PackContentEntry>& listBase, const vector<PackContentEntry>& listTarget, PackDelta& outDelta )
    {
        outDelta = PackDelta{};
        unordered_map<string, uint64> mapBase;
        unordered_map<string, uint64> mapTarget;
        if ( PackPatchInternal::makePathTable( listBase, "base", mapBase ) == false || PackPatchInternal::makePathTable( listTarget, "target", mapTarget ) == false )
            return false;
        for ( const PackContentEntry& entry : listTarget )
        {
            const auto baseIt = mapBase.find( entry._path );
            if ( baseIt == mapBase.end() || baseIt->second != entry._contentHash )
                outDelta._listChangedPath.push_back( entry._path );
        }
        for ( const PackContentEntry& entry : listBase )
        {
            if ( mapTarget.find( entry._path ) == mapTarget.end() )
                outDelta._listRemovedPath.push_back( entry._path );
        }
        std::sort( outDelta._listChangedPath.begin(), outDelta._listChangedPath.end() );
        std::sort( outDelta._listRemovedPath.begin(), outDelta._listRemovedPath.end() );
        return true;
    }

    bool PackPatchUtil::parseManifest( string_view xmlText, string_view sourceName, PackPatchManifest& outManifest )
    {
        outManifest = PackPatchManifest{};
        XMLDocument document;
        if ( document.parse( xmlText, sourceName ) == false )
        {
            SW_LOG_ERROR( "Pack patch manifest is not valid XML: %#", document.getLastError() );
            return false;
        }
        const XMLNode root = document.getRoot( PackPatchInternal::kRootName );
        if ( root.isValid() == false )
        {
            SW_LOG_ERROR( "Pack patch manifest %# has no <%#> root", sourceName, PackPatchInternal::kRootName );
            return false;
        }
        const std::optional<string> fromBuild     = root.readRequiredAttribute<string>( "from" );
        const std::optional<string> toBuild       = root.readRequiredAttribute<string>( "to" );
        const std::optional<string> packFile      = root.readRequiredAttribute<string>( "pack" );
        const bool                  bMissingField = fromBuild.has_value() == false || toBuild.has_value() == false || packFile.has_value() == false;
        if ( bMissingField )
            return false; // 읽기가 원소 · 속성 이름으로 오류를 남겼다
        if ( *fromBuild == *toBuild )
        {
            SW_LOG_ERROR( "Pack patch manifest %# goes from build '%#' to itself", sourceName, *fromBuild );
            return false;
        }
        outManifest._fromBuild    = *fromBuild;
        outManifest._toBuild      = *toBuild;
        outManifest._packFileName = *packFile;
        for ( XMLNode removed = root.findChild( PackPatchInternal::kRemovedName ); removed.isValid(); removed = removed.findNextSibling( PackPatchInternal::kRemovedName ) )
        {
            const std::optional<string> path = removed.readRequiredAttribute<string>( "path" );
            if ( path.has_value() == false )
                return false;
            outManifest._listRemovedPath.push_back( *path );
        }
        return true;
    }

    string PackPatchUtil::makeManifestText( const PackPatchManifest& manifest )
    {
        XMLDocument   document;
        const XMLNode root = document.appendRoot( PackPatchInternal::kRootName );
        root.appendAttribute( "from", string_view{ manifest._fromBuild } );
        root.appendAttribute( "to", string_view{ manifest._toBuild } );
        root.appendAttribute( "pack", string_view{ manifest._packFileName } );
        for ( const string& removedPath : manifest._listRemovedPath )
        {
            root.appendChild( PackPatchInternal::kRemovedName ).appendAttribute( "path", string_view{ removedPath } );
        }
        return document.saveToString();
    }

    bool PackPatchUtil::orderPatchChain( string_view installedBuild, string_view targetBuild, const vector<PackPatchManifest>& listManifest, vector<size_t>& outListOrder )
    {
        outListOrder.clear();
        string currentBuild{ installedBuild };
        while ( currentBuild != targetBuild )
        {
            if ( outListOrder.size() >= listManifest.size() )
            {
                SW_LOG_ERROR( "Pack patch chain from build '%#' loops before it reaches '%#'", installedBuild, targetBuild );
                return false;
            }
            size_t nextIndex  = listManifest.size();
            int32  startCount = 0;
            for ( size_t manifestIndex = 0; manifestIndex < listManifest.size(); ++manifestIndex )
            {
                if ( listManifest[manifestIndex]._fromBuild != currentBuild )
                    continue;
                nextIndex = manifestIndex;
                ++startCount;
            }
            if ( startCount != 1 )
            {
                SW_LOG_ERROR( "Pack patch chain: %# patches start at build '%#' (need exactly one to reach '%#')", startCount, currentBuild, targetBuild );
                return false;
            }
            outListOrder.push_back( nextIndex );
            currentBuild = listManifest[nextIndex]._toBuild;
        }
        return true;
    }
} // namespace sw
