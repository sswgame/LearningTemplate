#include "pch.h"

#include "Engine/Character/AnimationNotify/AnimationNotifyTable.h"

#include "Core/Container/StringUtil.h"
#include "Core/Container/VectorUtil.h"

#include "Engine/Character/AnimationNotify/AnimationNotifyHandlers.h"
#include "Engine/Character/CharacterDataReader.h"
#include "Engine/Serialization/XML/XMLDocument.h"

namespace sw
{
    namespace
    {
        struct AnimationNotifyTableInternal
        {
            /** @brief 처리기 인자 선언에서 이름의 것을 찾습니다. */
            static const AnimationNotifyParamDef* findParamDef( vector_reference<const AnimationNotifyParamDef> listDef, const utf8* pName )
            {
                for ( const AnimationNotifyParamDef& def : listDef )
                {
                    if ( StringUtil::equals( def._pName, pName ) )
                        return &def;
                }
                return nullptr;
            }

            /** @brief 적지 않은 선택 인자 하나를 선언의 "적지 않음" 값으로 만듭니다. */
            static AnimationNotifyParam makeAbsentParam( const AnimationNotifyParamDef& def )
            {
                AnimationNotifyParam param;
                param._name   = hashed_string( def._pName );
                param._kind   = def._kind;
                param._number = def._absentNumber;
                param._bValue = def._bAbsentBool;
                return param;
            }

            /** @brief 인자 하나를 선언된 종류로 읽습니다. 값이 틀리면 리더에 오류가 쌓이고 로드가 실패합니다. */
            static AnimationNotifyParam readParam( const XMLNode& node, const AnimationNotifyParamDef& def, CharacterDataReader& reader )
            {
                AnimationNotifyParam param;
                param._name = hashed_string( def._pName );
                param._kind = def._kind;
                switch ( def._kind )
                {
                    case AnimationNotifyParamKind::Name:
                    {
                        param._nameValue = reader.readName( node, def._pName, true );
                        break;
                    }
                    case AnimationNotifyParamKind::Text:
                    {
                        param._text = string( node.getAttributeText( def._pName ) );
                        if ( param._text.empty() )
                            reader.addError( "Character.NotifyValueEmpty", node, string( "has an empty '" ) + def._pName + "'" );
                        break;
                    }
                    case AnimationNotifyParamKind::Float:
                    {
                        param._number = reader.readFloat( node, def._pName );
                        break;
                    }
                    case AnimationNotifyParamKind::Vector:
                    {
                        param._vector = reader.readFloat3( node, def._pName );
                        break;
                    }
                    case AnimationNotifyParamKind::Bool:
                    {
                        param._bValue = reader.readBool( node, def._pName );
                        break;
                    }
                }
                return param;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    const AnimationNotifyParam* AnimationNotifyEntry::findParam( const hashed_string& name ) const
    {
        return VectorUtil::findBy<&AnimationNotifyParam::_name>( _listParam, name );
    }

    const AnimationNotifyParam* AnimationNotifyEntry::findDeclaredParam( const hashed_string& name ) const
    {
        const AnimationNotifyParam* pParam = findParam( name );
        SW_LOG_ASSERT( pParam != nullptr, "Handler '%#' asked for argument '%#' it does not declare", _handlerName.c_str(), name.c_str() );
        return pParam;
    }

    hashed_string AnimationNotifyEntry::getNameParam( const hashed_string& name ) const
    {
        const AnimationNotifyParam* pParam = findDeclaredParam( name );
        return pParam != nullptr ? pParam->_nameValue : hashed_string{};
    }

    string AnimationNotifyEntry::getTextParam( const hashed_string& name ) const
    {
        const AnimationNotifyParam* pParam = findDeclaredParam( name );
        return pParam != nullptr ? pParam->_text : string{};
    }

    float32 AnimationNotifyEntry::getFloatParam( const hashed_string& name ) const
    {
        const AnimationNotifyParam* pParam = findDeclaredParam( name );
        return pParam != nullptr ? pParam->_number : 0.0f;
    }

    float3 AnimationNotifyEntry::getVectorParam( const hashed_string& name ) const
    {
        const AnimationNotifyParam* pParam = findDeclaredParam( name );
        return pParam != nullptr ? pParam->_vector : float3{};
    }

    bool AnimationNotifyEntry::getBoolParam( const hashed_string& name ) const
    {
        const AnimationNotifyParam* pParam = findDeclaredParam( name );
        return pParam != nullptr && pParam->_bValue;
    }
} // namespace sw

namespace sw
{
    bool AnimationNotifyTable::loadFromXMLText( string_view xmlText, string_view sourceName, const AnimationNotifyHandlerRegistry& registry )
    {
        _listEntry.clear();
        _listFootContact.clear();
        CharacterDataReader reader( sourceName );
        XMLDocument         document;
        XMLNode             root;
        if ( reader.parseRoot( document, xmlText, "AnimationNotifies", root ) )
            readRoot( root, registry, reader );
        return reader.finish();
    }

    bool AnimationNotifyTable::loadFromResource( string_view path, const AnimationNotifyHandlerRegistry& registry )
    {
        _listEntry.clear();
        _listFootContact.clear();
        CharacterDataReader reader( path );
        XMLDocument         document;
        XMLNode             root;
        if ( reader.loadRoot( document, path, "AnimationNotifies", root ) )
            readRoot( root, registry, reader );
        return reader.finish();
    }

    const AnimationNotifyEntry* AnimationNotifyTable::findEntry( const hashed_string& notify ) const
    {
        return VectorUtil::findBy<&AnimationNotifyEntry::_notify>( _listEntry, notify );
    }

    void AnimationNotifyTable::readRoot( const XMLNode& root, const AnimationNotifyHandlerRegistry& registry, CharacterDataReader& reader )
    {
        reader.reportUnexpectedAttributes( root );
        for ( XMLNode child = root.findChild(); child; child = child.findNextSibling() )
        {
            if ( StringUtil::equals( child.getName(), "Notify" ) )
                readEntry( child, registry, reader );
            else if ( StringUtil::equals( child.getName(), "FootContact" ) )
                readFootContact( child, reader );
            else
                reader.reportUnknownElement( child );
        }
        // 발 닿음은 테이블의 알림을 낸다 — 줄 순서와 상관없이 다 읽은 뒤에 이름을 맞춘다.
        for ( const AnimationFootContactDef& def : _listFootContact )
        {
            if ( findEntry( def._notify ) == nullptr )
                reader.addError( "Character.FootContactNotifyMissing", string( "FootContact names notify '" ) + def._notify.c_str() + "' that has no <Notify> entry" );
        }
    }

    bool AnimationNotifyTable::isFootContactNotify( const hashed_string& notify ) const
    {
        return VectorUtil::containsBy<&AnimationFootContactDef::_notify>( _listFootContact, notify );
    }

    void AnimationNotifyTable::readFootContact( const XMLNode& node, CharacterDataReader& reader )
    {
        static constexpr const utf8* kArrKnown[] = { "notify", "bone", "contact_height", "release_height", "max_vertical_speed" };
        reader.reportUnknownAttributes( node, kArrKnown );
        AnimationFootContactDef def{};
        def._notify           = reader.readName( node, "notify", true );
        def._bone             = reader.readName( node, "bone", true );
        def._contactHeight    = reader.readFloat( node, "contact_height" );
        def._releaseHeight    = reader.readFloat( node, "release_height" );
        def._maxVerticalSpeed = reader.readFloat( node, "max_vertical_speed" );
        if ( def._releaseHeight <= def._contactHeight )
            reader.addError( "Character.FootContactHeightOrder", node, "needs 'release_height' above 'contact_height'" );
        if ( def._maxVerticalSpeed <= 0.0f )
            reader.addError( "Character.FootContactSpeedInvalid", node, "needs a positive 'max_vertical_speed'" );
        if ( isFootContactNotify( def._notify ) )
        {
            reader.addError( "Character.FootContactRepeated", node, string( "repeats foot contact notify '" ) + def._notify.c_str() + "'" );
            return;
        }
        _listFootContact.push_back( def );
    }

    void AnimationNotifyTable::readEntry( const XMLNode& node, const AnimationNotifyHandlerRegistry& registry, CharacterDataReader& reader )
    {
        AnimationNotifyEntry entry;
        entry._notify      = reader.readName( node, "name", true );
        entry._handlerName = reader.readName( node, "handler", true );
        if ( entry._notify.empty() || entry._handlerName.empty() )
            return;
        entry._pHandler = registry.findHandler( entry._handlerName );
        if ( entry._pHandler == nullptr )
        {
            reader.addError( "Character.NotifyHandlerUnknown", node, string( "names unknown handler '" ) + entry._handlerName.c_str() + "'" );
            return;
        }
        const vector_reference<const AnimationNotifyParamDef> listDef = entry._pHandler->getParams();
        // 테이블 항목은 처리기가 선언한 인자만 쓸 수 있다. 모르는 필드는 오타이므로 오류다.
        for ( XMLAttribute attribute = node.getFirstAttribute(); attribute; attribute = attribute.getNext() )
        {
            const utf8* pName      = attribute.getName();
            const bool  bBaseField = StringUtil::equals( pName, "name" ) || StringUtil::equals( pName, "handler" );
            if ( bBaseField == false && AnimationNotifyTableInternal::findParamDef( listDef, pName ) == nullptr )
                reader.addError( "Character.NotifyArgumentUnknown", node, string( "has unknown argument '" ) + pName + "' for handler '" + entry._handlerName.c_str() + "'" );
        }
        // 선언한 인자는 모두 채운다 — 적지 않은 선택 인자는 선언의 "적지 않음" 값이고, 처리기는 대신할 값을 따로 들지 않는다.
        for ( const AnimationNotifyParamDef& def : listDef )
        {
            if ( node.hasAttribute( def._pName ) )
                entry._listParam.push_back( AnimationNotifyTableInternal::readParam( node, def, reader ) );
            else if ( def._bRequired )
                reader.addError( "Character.NotifyArgumentMissing", node, string( "needs argument '" ) + def._pName + "' for handler '" + entry._handlerName.c_str() + "'" );
            else
                entry._listParam.push_back( AnimationNotifyTableInternal::makeAbsentParam( def ) );
        }
        if ( findEntry( entry._notify ) != nullptr )
        {
            reader.addError( "Character.NotifyRepeated", node, string( "repeats notify '" ) + entry._notify.c_str() + "'" );
            return;
        }
        _listEntry.push_back( std::move( entry ) );
    }
} // namespace sw

namespace sw
{
    AnimationNotifyHandlerRegistry::AnimationNotifyHandlerRegistry()
        : _registry{}
    {
    }

    AnimationNotifyHandlerRegistry::~AnimationNotifyHandlerRegistry() = default;

    const AnimationNotifyHandlerRegistry& AnimationNotifyHandlerRegistry::getDefault()
    {
        static AnimationNotifyHandlerRegistry s_registry;
        static const bool                     s_bRegistered = AnimationNotifyHandlerUtil::registerBuiltInHandlers( s_registry );
        (void)s_bRegistered;
        return s_registry;
    }

    void AnimationNotifyHandlerRegistry::registerHandler( const hashed_string& name, unique_ptr<IAnimationNotifyHandler> handler )
    {
        if ( name.empty() || handler == nullptr )
            return;
        _registry.addOrReplace( name, std::move( handler ), "Animation notify handler" );
    }

    const IAnimationNotifyHandler* AnimationNotifyHandlerRegistry::findHandler( const hashed_string& name ) const
    {
        const unique_ptr<IAnimationNotifyHandler>* pHandler = _registry.find( name );
        return pHandler == nullptr ? nullptr : pHandler->get();
    }
} // namespace sw
