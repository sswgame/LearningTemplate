#include "pch.h"

#include "Engine/Character/AnimNotify/AnimNotifyTable.h"

#include "Core/String/StringUtil.h"

#include "Engine/Character/AnimNotify/AnimNotifyHandlers.h"
#include "Engine/Character/CharacterDataReader.h"
#include "Engine/Utility/Xml/XmlDocument.h"

namespace sw
{
    namespace
    {
        struct AnimNotifyTableInternal
        {
            /** @brief 처리기 인자 선언에서 이름의 것을 찾습니다. */
            static const AnimNotifyParamDef* findParamDef( vector_reference<const AnimNotifyParamDef> listDef, const utf8* pName )
            {
                for ( const AnimNotifyParamDef& def : listDef )
                {
                    if ( StringUtil::equals( def._pName, pName, true ) )
                        return &def;
                }
                return nullptr;
            }

            /** @brief 인자 하나를 선언된 종류로 읽습니다. */
            static AnimNotifyParam readParam( const XmlNode& node, const AnimNotifyParamDef& def, CharacterDataReader& reader )
            {
                AnimNotifyParam param;
                param._name = hashed_string( def._pName );
                param._kind = def._kind;
                switch ( def._kind )
                {
                    case AnimNotifyParamKind::Name:
                    {
                        param._nameValue = reader.readName( node, def._pName, true );
                        break;
                    }
                    case AnimNotifyParamKind::Text:
                    {
                        param._text = string( node.getAttributeText( def._pName ) );
                        if ( param._text.empty() )
                            reader.addError( node, string( "has an empty '" ) + def._pName + "'" );
                        break;
                    }
                    case AnimNotifyParamKind::Float:
                    {
                        param._number = reader.readFloat( node, def._pName, 0.0f );
                        break;
                    }
                    case AnimNotifyParamKind::Vector:
                    {
                        param._vector = reader.readFloat3( node, def._pName, float3{} );
                        break;
                    }
                    case AnimNotifyParamKind::Bool:
                    {
                        param._bValue = reader.readBool( node, def._pName, false );
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
    const AnimNotifyParam* AnimNotifyEntry::findParam( const hashed_string& name ) const
    {
        for ( const AnimNotifyParam& param : _listParam )
        {
            if ( param._name == name )
                return &param;
        }
        return nullptr;
    }

    hashed_string AnimNotifyEntry::getNameParam( const hashed_string& name, const hashed_string& fallback ) const
    {
        const AnimNotifyParam* pParam = findParam( name );
        return pParam != nullptr ? pParam->_nameValue : fallback;
    }

    string AnimNotifyEntry::getTextParam( const hashed_string& name ) const
    {
        const AnimNotifyParam* pParam = findParam( name );
        return pParam != nullptr ? pParam->_text : string{};
    }

    float32 AnimNotifyEntry::getFloatParam( const hashed_string& name, float32 fallback ) const
    {
        const AnimNotifyParam* pParam = findParam( name );
        return pParam != nullptr ? pParam->_number : fallback;
    }

    float3 AnimNotifyEntry::getVectorParam( const hashed_string& name, const float3& fallback ) const
    {
        const AnimNotifyParam* pParam = findParam( name );
        return pParam != nullptr ? pParam->_vector : fallback;
    }

    bool AnimNotifyEntry::getBoolParam( const hashed_string& name, bool fallback ) const
    {
        const AnimNotifyParam* pParam = findParam( name );
        return pParam != nullptr ? pParam->_bValue : fallback;
    }
} // namespace sw

namespace sw
{
    bool AnimNotifyTable::loadFromXmlText( string_view xmlText, string_view sourceName, const AnimNotifyHandlerRegistry& registry )
    {
        _listEntry.clear();
        CharacterDataReader reader( sourceName );
        XmlDocument         document;
        XmlNode             root;
        if ( reader.parseRoot( document, xmlText, "AnimNotifies", root ) )
            readRoot( root, registry, reader );
        return reader.finish();
    }

    bool AnimNotifyTable::loadFromResource( string_view path, const AnimNotifyHandlerRegistry& registry )
    {
        _listEntry.clear();
        CharacterDataReader reader( path );
        XmlDocument         document;
        XmlNode             root;
        if ( reader.loadRoot( document, path, "AnimNotifies", root ) )
            readRoot( root, registry, reader );
        return reader.finish();
    }

    const AnimNotifyEntry* AnimNotifyTable::findEntry( const hashed_string& notify ) const
    {
        for ( const AnimNotifyEntry& entry : _listEntry )
        {
            if ( entry._notify == notify )
                return &entry;
        }
        return nullptr;
    }

    void AnimNotifyTable::readRoot( const XmlNode& root, const AnimNotifyHandlerRegistry& registry, CharacterDataReader& reader )
    {
        reader.reportUnexpectedAttributes( root );
        for ( XmlNode child = root.findChild(); child; child = child.findNextSibling() )
        {
            if ( StringUtil::equals( child.getName(), "Notify", true ) )
                readEntry( child, registry, reader );
            else
                reader.reportUnknownElement( child );
        }
    }

    void AnimNotifyTable::readEntry( const XmlNode& node, const AnimNotifyHandlerRegistry& registry, CharacterDataReader& reader )
    {
        AnimNotifyEntry entry;
        entry._notify      = reader.readName( node, "name", true );
        entry._handlerName = reader.readName( node, "handler", true );
        if ( entry._notify.empty() || entry._handlerName.empty() )
            return;
        entry._pHandler = registry.findHandler( entry._handlerName );
        if ( entry._pHandler == nullptr )
        {
            reader.addError( node, string( "names unknown handler '" ) + entry._handlerName.c_str() + "'" );
            return;
        }
        const vector_reference<const AnimNotifyParamDef> listDef = entry._pHandler->getParams();
        // 표의 칸은 처리기가 선언한 인자만 — 모르는 칸은 오타이므로 오류다.
        for ( XmlAttribute attribute = node.getFirstAttribute(); attribute; attribute = attribute.getNext() )
        {
            const utf8* pName      = attribute.getName();
            const bool  bBaseField = StringUtil::equals( pName, "name", true ) || StringUtil::equals( pName, "handler", true );
            if ( bBaseField == false && AnimNotifyTableInternal::findParamDef( listDef, pName ) == nullptr )
                reader.addError( node, string( "has unknown argument '" ) + pName + "' for handler '" + entry._handlerName.c_str() + "'" );
        }
        for ( const AnimNotifyParamDef& def : listDef )
        {
            if ( CharacterDataReader::hasAttribute( node, def._pName ) )
                entry._listParam.push_back( AnimNotifyTableInternal::readParam( node, def, reader ) );
            else if ( def._bRequired )
                reader.addError( node, string( "needs argument '" ) + def._pName + "' for handler '" + entry._handlerName.c_str() + "'" );
        }
        if ( findEntry( entry._notify ) != nullptr )
        {
            reader.addError( node, string( "repeats notify '" ) + entry._notify.c_str() + "'" );
            return;
        }
        _listEntry.push_back( std::move( entry ) );
    }
} // namespace sw

namespace sw
{
    AnimNotifyHandlerRegistry::AnimNotifyHandlerRegistry()
        : _listEntry{}
    {
    }

    AnimNotifyHandlerRegistry::~AnimNotifyHandlerRegistry() = default;

    AnimNotifyHandlerRegistry& AnimNotifyHandlerRegistry::getDefault()
    {
        static AnimNotifyHandlerRegistry s_registry;
        static const bool                s_bRegistered = AnimNotifyHandlerUtil::registerBuiltInHandlers( s_registry );
        (void)s_bRegistered;
        return s_registry;
    }

    void AnimNotifyHandlerRegistry::registerHandler( const hashed_string& name, unique_ptr<IAnimNotifyHandler> handler )
    {
        if ( name.empty() || handler == nullptr )
            return;
        for ( Entry& entry : _listEntry )
        {
            if ( entry._name == name )
            {
                entry._handler = std::move( handler );
                return;
            }
        }
        _listEntry.push_back( Entry{ name, std::move( handler ) } );
    }

    const IAnimNotifyHandler* AnimNotifyHandlerRegistry::findHandler( const hashed_string& name ) const
    {
        for ( const Entry& entry : _listEntry )
        {
            if ( entry._name == name )
                return entry._handler.get();
        }
        return nullptr;
    }

    void AnimNotifyHandlerRegistry::collectHandlerNames( vector<hashed_string>& outListName ) const
    {
        outListName.clear();
        for ( const Entry& entry : _listEntry )
        {
            outListName.push_back( entry._name );
        }
    }
} // namespace sw
