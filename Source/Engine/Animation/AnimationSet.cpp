#include "pch.h"

#include "Engine/Animation/AnimationSet.h"

#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"
#include "Core/Memory/Memory.h"
#include "Core/String/StringUtil.h"

#include "Engine/Asset/AssetFormat.h"
#include "Engine/Content/AssetIO.h"
#include "Engine/Serialization/Inherit/DataInheritance.h"
#include "Engine/Serialization/Inherit/ResourceComposer.h"
#include "Engine/Serialization/XML/XMLDocument.h"
#include "Engine/Serialization/XML/XMLFields.h"
#include "Engine/Serialization/XML/XMLReadUtil.h"

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

            /** @brief 상속 규칙입니다. 루트의 자식이 곧 이름을 열쇠로 하는 목록입니다. */
            static DataInheritanceSchema makeInheritanceSchema()
            {
                DataInheritanceSchema schema{ kRootName };
                schema._keyAttribute = "name";
                schema._listKeptAttribute.emplace_back( "formatVersion" );
                return schema;
            }
        };
        SW_DATA_INHERITANCE_SCHEMA( AnimationSet, &AnimationSetInternal::makeInheritanceSchema ); // 팩 쿠커가 루트 이름으로 찾는다
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

    bool AnimationSet::loadFromXMLText( string_view xml, string_view sourceLabel )
    {
        _listEntry.clear();
        if ( parseChained( xml, sourceLabel, 0, ResourceContext::forSchema( &AnimationSetInternal::makeInheritanceSchema ) ) )
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
        _listEntry.clear();
        if ( parseChained( text, assetID.view(), 0, ResourceContext::forSchema( &AnimationSetInternal::makeInheritanceSchema ).withAsset( assetID ) ) )
            return true;
        _listEntry.clear();
        return false;
    }

    bool AnimationSet::loadChained( string_view path, uint32 depth )
    {
        if ( depth >= kMaxChainDepth )
        {
            SW_LOG_ERROR_ID( "AnimationSet.ChainTooDeep", "Animation set 'set' chain is deeper than %# at '%#' (a loop?) - the set is not loaded", kMaxChainDepth, path );
            return false;
        }
        const AssetID assetID{ path };
        string        text;
        if ( AssetIO::readTextAsset( assetID, text ) == false )
        {
            SW_LOG_ERROR_ID( "AnimationSet.ChainNotFound", "Animation set '%#' could not be read - the set that points at it is not loaded", path );
            return false;
        }
        return parseChained( text, path, depth, ResourceContext::forSchema( &AnimationSetInternal::makeInheritanceSchema ).withAsset( assetID ) );
    }

    bool AnimationSet::parseChained( string_view xml, string_view sourceLabel, uint32 depth, const ResourceContext& context )
    {
        // 바탕은 공통 상속(`_base`)이 펼친다 — 같은 이름은 합치고(`_op="Replace"` 는 바꾸고) `_op="Remove"` 는 뺀다. 쿠킹본은 이미 펼친 한 벌이다.
        string flatText;
        bool   bComposed = false; // 펼치지 않으면 flatText 는 빈 채로 남는다
        if ( ResourceComposer::compose( xml, sourceLabel, context, flatText, bComposed ) == false )
            return false;
        XMLDocument document;
        if ( document.parse( flatText.empty() ? xml : string_view{ flatText } ) == false )
            return false;
        const XMLNode root = document.getRoot( AnimationSetInternal::kRootName );
        if ( root.isValid() == false )
        {
            SW_LOG_ERROR_ID( "AnimationSet.NotASet", "'%#' is not an animation set", sourceLabel );
            return false;
        }
        if ( AssetXMLFormat::upgradeToCurrentXMLVersion( kAssetType, root ) == false )
            return false;
        if ( XMLReadUtil::hasOnlyKnownNames( root, { "formatVersion" }, { AnimationSetInternal::kClipName }, sourceLabel ) == false )
            return false;

        vector<string> listSeen;
        for ( XMLNode item = root.findChild( AnimationSetInternal::kClipName ); item; item = item.findNextSibling( AnimationSetInternal::kClipName ) )
        {
            AnimationSetClipParams params;
            if ( XMLFields::readProperties( item, params, sourceLabel ) == false )
                return false;
            const string name = StringUtil::toLower( params._name.c_str() );
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
            if ( params._path.empty() == params._set.empty() )
            {
                SW_LOG_ERROR_ID( "AnimationSet.ClipNeedsOneSource", "Animation set '%#': clip '%#' must have exactly one of path and set (a line of the other kind over a base line needs _op=\"Replace\")", sourceLabel, name.c_str() );
                return false;
            }
            if ( params._path.empty() == false && params._retarget.empty() == false )
            {
                SW_LOG_ERROR_ID( "AnimationSet.RetargetNeedsSet", "Animation set '%#': clip '%#' has retarget with a path - name the source clip by set (and source) so the import can bake it", sourceLabel, name.c_str() );
                return false;
            }
            if ( params._path.empty() == false )
            {
                insertEntry( name, params._path );
                continue;
            }
            AnimationSet other;
            if ( other.loadChained( params._set, depth + 1 ) == false )
                return false;
            const string        sourceName = params._source.empty() ? name : StringUtil::toLower( params._source.c_str() );
            const string* const pClipPath  = other.findClipPath( hashed_string( string_view{ sourceName } ) );
            if ( pClipPath == nullptr )
            {
                SW_LOG_ERROR_ID( "AnimationSet.ClipNotInSet", "Animation set '%#': clip '%#' is not in set '%#'", sourceLabel, sourceName.c_str(), params._set.c_str() );
                return false;
            }
            if ( params._retarget.empty() )
            {
                insertEntry( name, *pClipPath );
                continue;
            }
            insertEntry( name, makeRetargetedClipPath( params._retarget, *pClipPath ) );
            AnimationSetEntry& entry  = *std::find_if( _listEntry.begin(), _listEntry.end(), [&name]( const AnimationSetEntry& item )
             { return item._name == name; } );
            entry._retargetProfile    = params._retarget;
            entry._retargetSourceClip = *pClipPath;
        }
        return true;
    }

    string AnimationSet::makeRetargetedClipPath( string_view profilePath, string_view sourceClipPath )
    {
        string profileName = FileUtil::getFileNamePart( profilePath );
        profileName        = profileName.substr( 0, profileName.find( '.' ) ); // `kaykit_to_ubc.retarget.xml` → `kaykit_to_ubc`
        return FileUtil::joinPath( FileUtil::joinPath( "common/models/_retargeted", profileName ), FileUtil::getFileNamePart( sourceClipPath ) );
    }

} // namespace sw
