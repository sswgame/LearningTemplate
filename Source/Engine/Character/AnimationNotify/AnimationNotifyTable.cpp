#include "pch.h"

#include "Engine/Character/AnimationNotify/AnimationNotifyTable.h"

#include "Core/Container/StringUtil.h"

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
                    if ( StringUtil::equals( def._pName, pName, true ) )
                        return &def;
                }
                return nullptr;
            }

            /** @brief 인자 하나를 선언된 종류로 읽습니다. */
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
                            reader.addError( node, string( "has an empty '" ) + def._pName + "'" );
                        break;
                    }
                    case AnimationNotifyParamKind::Float:
                    {
                        param._number = reader.readFloat( node, def._pName, 0.0f );
                        break;
                    }
                    case AnimationNotifyParamKind::Vector:
                    {
                        param._vector = reader.readFloat3( node, def._pName, float3{} );
                        break;
                    }
                    case AnimationNotifyParamKind::Bool:
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
    const AnimationNotifyParam* AnimationNotifyEntry::findParam( const hashed_string& name ) const
    {
        for ( const AnimationNotifyParam& param : _listParam )
        {
            if ( param._name == name )
                return &param;
        }
        return nullptr;
    }

    hashed_string AnimationNotifyEntry::getNameParam( const hashed_string& name, const hashed_string& fallback ) const
    {
        const AnimationNotifyParam* pParam = findParam( name );
        return pParam != nullptr ? pParam->_nameValue : fallback;
    }

    string AnimationNotifyEntry::getTextParam( const hashed_string& name ) const
    {
        const AnimationNotifyParam* pParam = findParam( name );
        return pParam != nullptr ? pParam->_text : string{};
    }

    float32 AnimationNotifyEntry::getFloatParam( const hashed_string& name, float32 fallback ) const
    {
        const AnimationNotifyParam* pParam = findParam( name );
        return pParam != nullptr ? pParam->_number : fallback;
    }

    float3 AnimationNotifyEntry::getVectorParam( const hashed_string& name, const float3& fallback ) const
    {
        const AnimationNotifyParam* pParam = findParam( name );
        return pParam != nullptr ? pParam->_vector : fallback;
    }

    bool AnimationNotifyEntry::getBoolParam( const hashed_string& name, bool fallback ) const
    {
        const AnimationNotifyParam* pParam = findParam( name );
        return pParam != nullptr ? pParam->_bValue : fallback;
    }
} // namespace sw

namespace sw
{
    bool AnimationNotifyTable::loadFromXMLText( string_view xmlText, string_view sourceName, const AnimationNotifyHandlerRegistry& registry )
    {
        _listEntry.clear();
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
        CharacterDataReader reader( path );
        XMLDocument         document;
        XMLNode             root;
        if ( reader.loadRoot( document, path, "AnimationNotifies", root ) )
            readRoot( root, registry, reader );
        return reader.finish();
    }

    const AnimationNotifyEntry* AnimationNotifyTable::findEntry( const hashed_string& notify ) const
    {
        for ( const AnimationNotifyEntry& entry : _listEntry )
        {
            if ( entry._notify == notify )
                return &entry;
        }
        return nullptr;
    }

    void AnimationNotifyTable::readRoot( const XMLNode& root, const AnimationNotifyHandlerRegistry& registry, CharacterDataReader& reader )
    {
        reader.reportUnexpectedAttributes( root );
        for ( XMLNode child = root.findChild(); child; child = child.findNextSibling() )
        {
            if ( StringUtil::equals( child.getName(), "Notify", true ) )
                readEntry( child, registry, reader );
            else
                reader.reportUnknownElement( child );
        }
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
            reader.addError( node, string( "names unknown handler '" ) + entry._handlerName.c_str() + "'" );
            return;
        }
        const vector_reference<const AnimationNotifyParamDef> listDef = entry._pHandler->getParams();
        // 테이블 항목은 처리기가 선언한 인자만 쓸 수 있다. 모르는 필드는 오타이므로 오류다.
        for ( XMLAttribute attribute = node.getFirstAttribute(); attribute; attribute = attribute.getNext() )
        {
            const utf8* pName      = attribute.getName();
            const bool  bBaseField = StringUtil::equals( pName, "name", true ) || StringUtil::equals( pName, "handler", true );
            if ( bBaseField == false && AnimationNotifyTableInternal::findParamDef( listDef, pName ) == nullptr )
                reader.addError( node, string( "has unknown argument '" ) + pName + "' for handler '" + entry._handlerName.c_str() + "'" );
        }
        for ( const AnimationNotifyParamDef& def : listDef )
        {
            if ( CharacterDataReader::hasAttribute( node, def._pName ) )
                entry._listParam.push_back( AnimationNotifyTableInternal::readParam( node, def, reader ) );
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
    AnimationNotifyHandlerRegistry::AnimationNotifyHandlerRegistry()
        : _registry{}
    {
    }

    AnimationNotifyHandlerRegistry::~AnimationNotifyHandlerRegistry() = default;

    AnimationNotifyHandlerRegistry& AnimationNotifyHandlerRegistry::getDefault()
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
        _registry.addOrReplace( name, std::move( handler ) );
    }

    const IAnimationNotifyHandler* AnimationNotifyHandlerRegistry::findHandler( const hashed_string& name ) const
    {
        const unique_ptr<IAnimationNotifyHandler>* pHandler = _registry.find( name );
        return pHandler == nullptr ? nullptr : pHandler->get();
    }

    void AnimationNotifyHandlerRegistry::collectHandlerNames( vector<hashed_string>& outListName ) const
    {
        outListName.clear();
        for ( uint32 index = 0; index < _registry.getCount(); ++index )
        {
            outListName.push_back( _registry.getNameAt( index ) );
        }
    }
} // namespace sw
