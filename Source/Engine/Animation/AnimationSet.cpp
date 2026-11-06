#include "pch.h"

#include "Engine/Animation/AnimationSet.h"

#include "Core/Container/StringUtil.h"
#include "Core/Log/Logger.h"
#include "Core/Memory/Memory.h"

#include "Engine/Asset/AssetFormat.h"
#include "Engine/Content/AssetIO.h"
#include "Engine/Serialization/XML/XMLDocument.h"
#include "Engine/Serialization/XML/XMLFields.h"

#include <algorithm>

namespace sw
{
    namespace
    {
        /** @brief 셋 XML 의 원소 이름입니다. */
        struct AnimationSetInternal
        {
            static constexpr const utf8* kRootName = "AnimationSet";
            static constexpr const utf8* kClipName = "clip";
        };
    } // namespace

    const string* AnimationSet::findClipPath( const hashed_string& name ) const
    {
        const string lowered = StringUtil::toLower( name.c_str() );
        const auto   it      = std::lower_bound( _listEntry.begin(), _listEntry.end(), lowered, []( const AnimationSetEntry& entry, const string& key )
               { return entry._name < key; } );
        return it != _listEntry.end() && it->_name == lowered ? &it->_clipPath : nullptr;
    }

    void AnimationSet::insertEntry( string name, string clipPath )
    {
        const auto it = std::lower_bound( _listEntry.begin(), _listEntry.end(), name, []( const AnimationSetEntry& entry, const string& key )
        { return entry._name < key; } );
        if ( it != _listEntry.end() && it->_name == name )
        {
            it->_clipPath = std::move( clipPath );
            return;
        }
        _listEntry.insert( it, AnimationSetEntry{ std::move( name ), std::move( clipPath ) } );
    }

    void AnimationSet::setClip( string_view name, string_view clipPath )
    {
        insertEntry( StringUtil::toLower( string( name ).c_str() ), string( clipPath ) );
    }

    string AnimationSet::saveToString() const
    {
        XMLDocument   document;
        const XMLNode root = document.appendRoot( AnimationSetInternal::kRootName );
        AssetXMLFormat::writeXMLVersion( root, kAssetType._formatVersion );
        for ( const AnimationSetEntry& entry : _listEntry )
        {
            const XMLNode clip = root.appendChild( AnimationSetInternal::kClipName );
            clip.appendAttribute( "name", string_view{ entry._name } );
            clip.appendAttribute( "path", string_view{ entry._clipPath } );
        }
        return document.saveToString();
    }

    bool AnimationSet::parseXML( string_view xml, string_view sourceLabel )
    {
        _listEntry.clear();
        if ( parseChained( xml, sourceLabel, 0 ) )
            return true;
        _listEntry.clear();
        return false;
    }

    bool AnimationSet::loadFromAsset( const AssetID& assetID )
    {
        SW_MEMORY_SCOPE( Animation );
        string text;
        if ( AssetIO::readTextAsset( assetID, text ) == false )
        {
            SW_LOG_ERROR_ID( "AnimationSet.CouldNotRead", "Animation set '%#' could not be read", assetID.view() );
            _listEntry.clear();
            return false;
        }
        return parseXML( text, assetID.view() );
    }

    bool AnimationSet::loadChained( string_view path, uint32 depth )
    {
        if ( depth >= kMaxChainDepth )
        {
            SW_LOG_ERROR_ID( "AnimationSet.ChainTooDeep", "Animation set chain is deeper than %# at '%#' (a loop?) - the set is not loaded", kMaxChainDepth, path );
            return false;
        }
        const AssetID assetID{ path };
        string        text;
        if ( AssetIO::readTextAsset( assetID, text ) == false )
        {
            SW_LOG_ERROR_ID( "AnimationSet.ChainNotFound", "Animation set '%#' could not be read - the set that points at it is not loaded", path );
            return false;
        }
        return parseChained( text, path, depth );
    }

    bool AnimationSet::parseChained( string_view xml, string_view sourceLabel, uint32 depth )
    {
        XMLDocument document;
        if ( document.parse( xml ) == false )
            return false;
        const XMLNode root = document.getRoot( AnimationSetInternal::kRootName );
        if ( root.isValid() == false )
        {
            SW_LOG_ERROR_ID( "AnimationSet.NotASet", "'%#' is not an animation set", sourceLabel );
            return false;
        }
        if ( AssetXMLFormat::isCurrentXMLVersion( kAssetType, root ) == false )
            return false;
        AnimationSetRootFields rootFields;
        if ( XMLFields::readFields( root, rootFields, sourceLabel, { "formatVersion" } ) == false )
            return false;
        // 바탕 셋을 먼저 펼치고 이 파일의 줄이 덮는다 — 같은 이름은 차이다.
        if ( rootFields._base.empty() == false && loadChained( rootFields._base, depth + 1 ) == false )
            return false;

        vector<string> listSeen;
        for ( XMLNode item = root.findChild( AnimationSetInternal::kClipName ); item; item = item.findNextSibling( AnimationSetInternal::kClipName ) )
        {
            AnimationSetClipFields fields;
            if ( XMLFields::readFields( item, fields, sourceLabel ) == false )
                return false;
            const string name = StringUtil::toLower( fields._name.c_str() );
            if ( StringUtil::contains( name, "/" ) )
            {
                SW_LOG_ERROR_ID( "AnimationSet.NameHasPath", "Animation set '%#': clip name '%#' holds a path - a name with '/' means a clip file to the animator", sourceLabel, name.c_str() );
                return false;
            }
            if ( std::find( listSeen.begin(), listSeen.end(), name ) != listSeen.end() )
            {
                SW_LOG_ERROR_ID( "AnimationSet.DuplicateClip", "Animation set '%#' lists clip '%#' twice", sourceLabel, name.c_str() );
                return false;
            }
            listSeen.push_back( name );
            if ( fields._path.empty() == fields._set.empty() )
            {
                SW_LOG_ERROR_ID( "AnimationSet.ClipNeedsOneSource", "Animation set '%#': clip '%#' must have exactly one of path and set", sourceLabel, name.c_str() );
                return false;
            }
            if ( fields._path.empty() == false )
            {
                insertEntry( name, fields._path );
                continue;
            }
            AnimationSet other;
            if ( other.loadChained( fields._set, depth + 1 ) == false )
                return false;
            const string        sourceName = fields._source.empty() ? name : StringUtil::toLower( fields._source.c_str() );
            const string* const pClipPath  = other.findClipPath( hashed_string( string_view{ sourceName } ) );
            if ( pClipPath == nullptr )
            {
                SW_LOG_ERROR_ID( "AnimationSet.ClipNotInSet", "Animation set '%#': clip '%#' is not in set '%#'", sourceLabel, sourceName.c_str(), fields._set.c_str() );
                return false;
            }
            insertEntry( name, *pClipPath );
        }
        return true;
    }

    SW_ASSET_TYPE( AnimationSet, AnimationSet::kAssetType );
} // namespace sw
