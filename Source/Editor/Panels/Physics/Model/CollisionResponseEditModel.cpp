#include "pch.h"

#include "Editor/Panels/Physics/Model/CollisionResponseEditModel.h"

#include "Core/File/FileUtil.h"
#include "Core/String/StringUtil.h"

#include "Editor/Common/Workspace/Undo/DocumentEditRecorder.h"
#include "Editor/Panels/Catalog/Model/CatalogXMLText.h"

#include "Engine/Reflection/EnumName.h"

namespace sw::editor
{
    namespace
    {
        struct CollisionResponseEditModelInternal
        {
            /** @brief 이름이 @p elementName 이고 속성 @p attribute 가 @p value 인 원소 번호입니다. @p parent 가 있으면 그 자식만 봅니다. 없으면 비어 있습니다. */
            static optional<int32> findElement( const CatalogXMLText& xml, optional<int32> parent, string_view elementName, string_view attribute, string_view value )
            {
                const vector<CatalogXMLElement>& listElement = xml.getElements();
                for ( size_t index = 0; index < listElement.size(); ++index )
                {
                    const CatalogXMLElement& element = listElement[index];
                    if ( element._name != elementName || ( parent.has_value() && element._parent != parent ) )
                        continue;
                    if ( attribute.empty() )
                        return static_cast<int32>( index );
                    const optional<uint32> attributeIndex = element.findAttribute( attribute );
                    if ( attributeIndex.has_value() && element._listAttribute[*attributeIndex]._value == value )
                        return static_cast<int32>( index );
                }
                return nullopt;
            }
        };
    } // namespace

    CollisionResponseEditModel::CollisionResponseEditModel()
        : _text{}
        , _settings{}
        , _status{}
    {
    }

    ToolAssetLoadResult CollisionResponseEditModel::loadDocument( const AbsolutePath& diskPath )
    {
        string text;
        if ( FileUtil::readTextFile( diskPath, text ) == false )
        {
            _status = "The physics settings file could not be read";
            return ToolAssetLoadResult::Missing;
        }
        return loadText( text ) ? ToolAssetLoadResult::Loaded : ToolAssetLoadResult::Malformed;
    }

    bool CollisionResponseEditModel::loadText( string_view text )
    {
        _text.clear();
        _settings = PhysicsSettings{};
        return acceptText( string( text ) );
    }

    bool CollisionResponseEditModel::saveDocument( const AbsolutePath& diskPath )
    {
        if ( FileUtil::writeTextFile( diskPath, _text ) == false )
        {
            _status = "Not saved: the file could not be written (see the log)";
            return false;
        }
        _status = "Saved";
        return true;
    }

    void CollisionResponseEditModel::applyText( string_view text )
    {
        if ( acceptText( string( text ) ) == false )
        {
            SW_LOG_WARNING_ID( "CollisionResponseEditModel.UndoSnapshot", "Collision response undo snapshot could not be read - showing an empty table" );
            _text.clear();
            _settings = PhysicsSettings{};
        }
    }

    CollisionResponse CollisionResponseEditModel::getResponse( uint32 profileIndex, uint32 channelIndex ) const
    {
        if ( profileIndex >= _settings._listProfile.size() || channelIndex >= _settings._listChannel.size() )
            return CollisionResponse::Ignore;
        return _settings.getProfileResponse( _settings._listProfile[profileIndex], _settings._listChannel[channelIndex]._name );
    }

    bool CollisionResponseEditModel::isDefaultResponse( uint32 profileIndex, uint32 channelIndex ) const
    {
        if ( profileIndex >= _settings._listProfile.size() || channelIndex >= _settings._listChannel.size() )
            return true;
        const hashed_string& channel = _settings._listChannel[channelIndex]._name;
        for ( const CollisionResponseDef& response : _settings._listProfile[profileIndex]._listResponse )
        {
            if ( response._channel == channel )
                return false;
        }
        return true;
    }

    bool CollisionResponseEditModel::setResponse( uint32 profileIndex, uint32 channelIndex, CollisionResponse response )
    {
        using Internal = CollisionResponseEditModelInternal;
        if ( profileIndex >= _settings._listProfile.size() || channelIndex >= _settings._listChannel.size() || getResponse( profileIndex, channelIndex ) == response )
            return false;
        const CollisionProfileDef& profile  = _settings._listProfile[profileIndex];
        const CollisionChannelDef& channel  = _settings._listChannel[channelIndex];
        const bool                 bWritten = response != channel._defaultResponse; // 기본과 같은 값은 줄을 두지 않는다

        CatalogXMLText xml;
        if ( xml.parse( _text ).has_value() == false )
        {
            _status = "The file is not well-formed XML";
            return false;
        }
        const optional<int32> profileElement = Internal::findElement( xml, nullopt, "CollisionProfileDef", "_name", profile._name.view() );
        if ( profileElement.has_value() == false )
        {
            _status = "The profile is not written in the file";
            return false;
        }
        optional<int32>  listElement = Internal::findElement( xml, profileElement, "_listResponse", {}, {} );
        optional<int32>  lineElement = listElement.has_value() ? Internal::findElement( xml, listElement, "CollisionResponseDef", "_channel", channel._name.view() ) : nullopt;
        expected<string> edited      = string{};
        const string     value       = string( getEnumName( response ) ); // 파일에 적는 철자는 반사 열거 이름이다
        if ( lineElement.has_value() )
        {
            edited = bWritten ? CatalogXMLText::setAttributeValue( _text, xml.getElement( *lineElement ), "_response", value ) : CatalogXMLText::removeElement( _text, xml.getElement( *lineElement ) );
        }
        else
        {
            string text = _text;
            if ( listElement.has_value() == false )
            {
                const expected<string> withList = CatalogXMLText::appendChildElement( text, xml.getElement( *profileElement ), xml.getElements(), "_listResponse", {} );
                if ( withList.has_value() == false || xml.parse( *withList ).has_value() == false )
                {
                    _status = "The response list could not be added to the profile";
                    return false;
                }
                text        = *withList;
                listElement = Internal::findElement( xml, Internal::findElement( xml, nullopt, "CollisionProfileDef", "_name", profile._name.view() ), "_listResponse", {}, {} );
                if ( listElement.has_value() == false )
                    return false;
            }
            const vector<CatalogXMLNewAttribute> listAttribute = {
                CatalogXMLNewAttribute{ "_channel", string( channel._name.view() )},
                CatalogXMLNewAttribute{"_response",                          value}
            };
            edited = CatalogXMLText::appendChildElement( text, xml.getElement( *listElement ), xml.getElements(), "CollisionResponseDef", listAttribute );
        }
        if ( edited.has_value() == false || acceptText( std::move( *edited ) ) == false )
        {
            _status = "The response could not be written";
            return false;
        }
        if ( _pEditRecorder != nullptr ) // 테스트는 창구 없이 부른다
            _pEditRecorder->notifyEdited( "Set Collision Response" );
        return true;
    }

    const utf8* CollisionResponseEditModel::formatResponse( CollisionResponse response )
    {
        return getEnumName( response );
    }

    bool CollisionResponseEditModel::parseResponse( string_view text, CollisionResponse& outResponse )
    {
        const string_view trimmed = StringUtil::trim( text );
        for ( const CollisionResponse response : { CollisionResponse::Ignore, CollisionResponse::Overlap, CollisionResponse::Block } )
        {
            if ( trimmed != string_view{ formatResponse( response ) } )
                continue;
            outResponse = response;
            return true;
        }
        return false;
    }

    bool CollisionResponseEditModel::cycleResponse( uint32 profileIndex, uint32 channelIndex )
    {
        const CollisionResponse current = getResponse( profileIndex, channelIndex );
        const CollisionResponse next    = current == CollisionResponse::Block   ? CollisionResponse::Overlap
                                        : current == CollisionResponse::Overlap ? CollisionResponse::Ignore
                                                                                : CollisionResponse::Block;
        return setResponse( profileIndex, channelIndex, next );
    }

    bool CollisionResponseEditModel::resetResponse( uint32 profileIndex, uint32 channelIndex )
    {
        if ( isDefaultResponse( profileIndex, channelIndex ) ) // 범위 밖도 기본으로 답한다
            return false;
        return setResponse( profileIndex, channelIndex, _settings._listChannel[channelIndex]._defaultResponse );
    }

    bool CollisionResponseEditModel::acceptText( string text )
    {
        PhysicsSettings settings;
        if ( settings.loadFromXMLText( text ) == false )
        {
            _status = "The physics settings do not read (see the log)";
            return false;
        }
        _text     = std::move( text );
        _settings = std::move( settings );
        return true;
    }
} // namespace sw::editor
