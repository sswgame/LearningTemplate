#include "pch.h"

#include "Editor/Panels/ProjectSettings/ProjectSettingsDocument.h"

#include "Core/Container/StringUtil.h"
#include "Core/Container/vector.h"
#include "Core/File/FileUtil.h"
#include "Core/Memory/Memory.h"

#include "Engine/Reflection/ReflectionCore.h"
#include "Engine/Reflection/ReflectionMetadata.h"
#include "Engine/Serialization/Format/JSONSerializer.h"
#include "Engine/Serialization/Format/XMLSerializer.h"
#include "Engine/Serialization/JSON/JSONDocument.h"
#include "Engine/Serialization/XML/XMLDocument.h"

namespace sw::editor
{
    SW_LOG_CALLER( "ProjectSettings" );

    namespace
    {
        struct ProjectSettingsDocumentInternal
        {
            /** @brief 요소 모드가 파일에 넣는 요소의 들여쓰기 한 단계입니다(팩 `gamesettings.xml` 이 공백 둘이다). */
            static constexpr string_view kIndent = "  ";

            static bool isEqualKey( const vector<string>& listEqual, const string& path )
            {
                for ( const string& equalPath : listEqual )
                {
                    if ( equalPath == path )
                        return true;
                }
                return false;
            }

            static string makePath( const string& prefix, const string& key )
            {
                return prefix.empty() ? key : string( prefix + "." + key );
            }

            /** @brief 구조체 칸처럼 안으로 들어가 볼 값이면 true 입니다(`ConfigManager::collectEqualKeys` 와 같은 규칙 — 비어 있지 않은 오브젝트). */
            static bool isNestedObject( const JSONValue& value )
            {
                return value.isObject() && value.getMemberNames().empty() == false;
            }

            /** @brief @p source 에서 @p listEqual 에 포함된 경로를 뺀 말단의 수입니다. */
            static uint32 countKept( const JSONValue& source, const string& prefix, const vector<string>& listEqual )
            {
                uint32 keptCount{ 0 };
                for ( const string& key : source.getMemberNames() )
                {
                    const string path = makePath( prefix, key );
                    if ( isEqualKey( listEqual, path ) )
                        continue;
                    const JSONValue child = source.get( key, false );
                    keptCount += isNestedObject( child ) ? countKept( child, path, listEqual ) : 1u;
                }
                return keptCount;
            }

            /**
             * @brief @p source 의 말단을 @p destination 으로 옮기되 @p listEqual 에 포함된 경로는 뺍니다.
             * @details 말단이 하나도 남지 않는 구조체 필드는 만들지 않는다. JSONValue 핸들은 형제를 더하면 무효가 되므로, 필드 하나를 다 채운 뒤에 다음 필드를 받는다.
             */
            static void copyDifference( const JSONValue& source, const JSONValue& destination, const string& prefix, const vector<string>& listEqual )
            {
                for ( const string& key : source.getMemberNames() )
                {
                    const string path = makePath( prefix, key );
                    if ( isEqualKey( listEqual, path ) )
                        continue;
                    const JSONValue child = source.get( key, false );
                    if ( isNestedObject( child ) )
                    {
                        if ( countKept( child, path, listEqual ) == 0 )
                            continue;
                        const JSONValue nested = destination.set( key, false );
                        nested.setObject();
                        copyDifference( child, nested, path, listEqual );
                        continue;
                    }
                    destination.set( key, false ).assignFrom( child );
                }
            }

            /** @brief 요소의 속성과 자식 수입니다(요소 모드의 "기본값과 다른 키" — 맵 항목은 자식 하나로 센다). */
            static uint32 countElementKeys( const XMLNode& element )
            {
                uint32 keyCount{ 0 };
                for ( XMLAttribute attr = element.getFirstAttribute(); attr.isValid(); attr = attr.getNext() )
                {
                    ++keyCount;
                }
                for ( XMLNode child = element.findChild(); child.isValid(); child = child.findNextSibling() )
                {
                    ++keyCount;
                }
                return keyCount;
            }

            /** @brief @p elementText(`XMLNode::toString`)를 루트 바로 아래 깊이로 들여 씁니다 — 줄 끝은 @p newline, 탭 한 단계는 `kIndent` 입니다. */
            static string indentElement( string_view elementText, string_view newline )
            {
                const string_view trimmed = StringUtil::trim( elementText );
                string            result;
                size_t            lineStart{ 0 };
                while ( lineStart <= trimmed.size() )
                {
                    const size_t lineBreak = trimmed.find( '\n', lineStart );
                    const size_t lineEnd   = lineBreak == string_view::npos ? trimmed.size() : lineBreak;
                    string_view  line      = trimmed.substr( lineStart, lineEnd - lineStart );
                    if ( line.empty() == false && line.back() == '\r' )
                        line.remove_suffix( 1 );
                    if ( lineStart > 0 )
                    {
                        result += newline;
                        result += kIndent;
                    }
                    size_t tabCount{ 0 };
                    while ( tabCount < line.size() && line[tabCount] == '\t' )
                    {
                        result += kIndent;
                        ++tabCount;
                    }
                    result += line.substr( tabCount );
                    if ( lineBreak == string_view::npos )
                        break;
                    lineStart = lineBreak + 1;
                }
                return result;
            }

            /**
             * @brief @p fileText 에서 @p element 의 글 범위 [outBegin, outEnd) 를 찾습니다. 위치를 모르면 false 입니다.
             * @details 시작은 파서가 아는 원문 위치에서 `<` 로 거슬러 간다. 끝은 시작 태그가 `/>` 로 닫히면 거기, 아니면 같은 이름의 닫는 태그다.
             */
            static bool findElementRange( string_view fileText, const XMLNode& element, string_view elementName, size_t& outBegin, size_t& outEnd )
            {
                const int64 offset = element.getSourceOffset();
                if ( offset < 0 || static_cast<size_t>( offset ) >= fileText.size() )
                    return false;
                const size_t begin = fileText.rfind( '<', static_cast<size_t>( offset ) );
                if ( begin == string_view::npos )
                    return false;
                const size_t tagEnd = fileText.find( '>', begin );
                if ( tagEnd == string_view::npos )
                    return false;
                outBegin = begin;
                if ( fileText[tagEnd - 1] == '/' )
                {
                    outEnd = tagEnd + 1;
                    return true;
                }
                string closeTag( "</" );
                closeTag.append( elementName );
                closeTag.append( ">" );
                const size_t close = fileText.find( closeTag, tagEnd );
                if ( close == string_view::npos )
                    return false;
                outEnd = close + closeTag.size();
                return true;
            }

            /**
             * @brief @p fileText 의 루트 아래 @p elementName 요소를 @p elementText 로 바꾼 글을 만듭니다. 요소가 없으면 루트 시작 태그 바로 뒤에 넣습니다.
             * @return 파일을 읽지 못했거나 위치를 찾지 못하면 false 입니다.
             */
            static bool spliceElement( string_view fileText, string_view elementName, string_view elementText, string& outText )
            {
                XMLDocument document;
                if ( document.parse( fileText ) == false )
                    return false;
                const XMLNode root = document.getRoot();
                if ( root.isValid() == false )
                    return false;
                const string_view newline  = fileText.find( "\r\n" ) != string_view::npos ? string_view( "\r\n" ) : string_view( "\n" );
                const string      indented = indentElement( elementText, newline );
                const string      name( elementName );
                const XMLNode     existing = root.findChild( name.c_str(), false );
                size_t            begin{ 0 };
                size_t            end{ 0 };
                if ( existing.isValid() )
                {
                    if ( findElementRange( fileText, existing, elementName, begin, end ) == false )
                        return false;
                    outText.assign( fileText.substr( 0, begin ) );
                    outText.append( indented );
                    outText.append( fileText.substr( end ) );
                    return true;
                }
                // 요소가 없다 — 루트 시작 태그 바로 뒤 줄에 넣는다.
                size_t rootEnd{ 0 };
                if ( findElementRange( fileText, root, root.getName(), begin, rootEnd ) == false )
                    return false;
                const size_t rootTagEnd = fileText.find( '>', begin ) + 1;
                outText.assign( fileText.substr( 0, rootTagEnd ) );
                outText.append( newline );
                outText.append( kIndent );
                outText.append( indented );
                outText.append( fileText.substr( rootTagEnd ) );
                return true;
            }
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    ProjectSettingsDocument::ProjectSettingsDocument()
        : _pType{ nullptr }
        , _pCurrent{ nullptr }
        , _pDefault{ nullptr }
        , _path{}
        , _elementName{}
        , _savedText{}
        , _readResult{ ConfigReadResult::Missing }
    {
    }

    ProjectSettingsDocument::~ProjectSettingsDocument()
    {
        shutdown();
    }

    void* ProjectSettingsDocument::createInstance( const TypeInfo& type )
    {
        const FunctionInfo* pConstructor      = type.findMethod( hashed_string( "$ctor" ) );
        const bool          bNotConstructible = pConstructor == nullptr || pConstructor->_invoker.isBound() == false || type._destroyInstance == nullptr;
        if ( bNotConstructible )
            return nullptr;
        void* pInstance = Memory::allocateAligned( type._size, alignof( std::max_align_t ) );
        pConstructor->_invoker( pInstance, TaskArgs{} );
        return pInstance;
    }

    void ProjectSettingsDocument::destroyInstance( const TypeInfo& type, void* pInstance )
    {
        if ( pInstance == nullptr )
            return;
        type._destroyInstance( pInstance );
        Memory::freeAligned( pInstance );
    }

    ConfigReadResult ProjectSettingsDocument::open( const TypeInfo& type, string_view absolutePath, string_view elementName )
    {
        shutdown();
        _pType       = &type;
        _path        = string( absolutePath );
        _elementName = string( elementName );
        _pCurrent    = createInstance( type );
        _pDefault    = createInstance( type );
        if ( _pCurrent == nullptr || _pDefault == nullptr )
        {
            SW_LOG_ERROR( "Project settings: %# has no default constructor in the reflection table", type._name.c_str() );
            shutdown();
            _readResult = ConfigReadResult::Invalid;
            return _readResult;
        }
        return revert();
    }

    void ProjectSettingsDocument::shutdown()
    {
        if ( _pType != nullptr )
        {
            destroyInstance( *_pType, _pCurrent );
            destroyInstance( *_pType, _pDefault );
        }
        _pType      = nullptr;
        _pCurrent   = nullptr;
        _pDefault   = nullptr;
        _readResult = ConfigReadResult::Missing;
        _path.clear();
        _elementName.clear();
        _savedText.clear();
    }

    ConfigReadResult ProjectSettingsDocument::readInto( void* pInstance, string_view fileText ) const
    {
        if ( isElementMode() == false )
            return ConfigManager::readConfigJSON( pInstance, *_pType, fileText, _path.c_str() ) ? ConfigReadResult::Loaded : ConfigReadResult::Invalid;
        XMLDocument document;
        if ( document.parse( fileText, _path ) == false || document.getRoot().isValid() == false )
        {
            SW_LOG_ERROR( "Project settings: %# is not XML - %#", _path.c_str(), document.getLastError().c_str() );
            return ConfigReadResult::Invalid;
        }
        const XMLNode element = document.getRoot().findChild( _elementName.c_str(), false );
        if ( element.isValid() == false )
            return ConfigReadResult::Missing;
        return ConfigManager::readConfigXMLElement( pInstance, *_pType, element.toString(), _path.c_str() ) ? ConfigReadResult::Loaded : ConfigReadResult::Invalid;
    }

    ConfigReadResult ProjectSettingsDocument::revert()
    {
        if ( _pType == nullptr || _pCurrent == nullptr )
            return ConfigReadResult::Invalid;
        if ( FileUtil::isRegularFile( _path ) == false )
        {
            resetToDefault();
            _readResult = ConfigReadResult::Missing;
            _savedText  = makeDifferenceText();
            return _readResult;
        }
        string fileText;
        if ( FileUtil::readTextFile( _path, fileText ) == false )
        {
            _readResult = ConfigReadResult::Invalid;
            return _readResult;
        }
        // 새 인스턴스에 읽어 성공했을 때만 바꾼다 — 틀린 파일이 현재 편집을 반쯤 덮지 않게.
        void* pFresh = createInstance( *_pType );
        if ( pFresh == nullptr )
        {
            _readResult = ConfigReadResult::Invalid;
            return _readResult;
        }
        const ConfigReadResult result = readInto( pFresh, fileText );
        if ( result != ConfigReadResult::Loaded )
        {
            destroyInstance( *_pType, pFresh );
            // 요소 모드에서 요소가 없으면 기본값으로 시작한다 — 저장하면 그 요소를 넣는다.
            if ( result == ConfigReadResult::Missing )
                resetToDefault();
            _readResult = result;
            _savedText  = makeDifferenceText();
            return _readResult;
        }
        destroyInstance( *_pType, _pCurrent );
        _pCurrent   = pFresh;
        _readResult = ConfigReadResult::Loaded;
        _savedText  = makeDifferenceText();
        return _readResult;
    }

    bool ProjectSettingsDocument::isBaseLayer() const
    {
        return _pType != nullptr && _pType->_metadata->findCustomMeta( hashed_string( kBaseLayerMeta ) ) != nullptr;
    }

    void ProjectSettingsDocument::resetToDefault()
    {
        if ( _pType == nullptr || _pCurrent == nullptr )
            return;
        // 바탕 층은 값의 원본이 파일이다 — 기본값으로 되돌리기는 파일 값으로 되돌리기다(헤더의 형식은 값이 아니다).
        const bool bRevertToFile = isBaseLayer() && FileUtil::isRegularFile( _path );
        if ( bRevertToFile )
        {
            (void)revert(); // 읽지 못하면 revert 가 상태를 Invalid 로 남기고 저장을 막는다
            return;
        }
        void* pFresh = createInstance( *_pType );
        if ( pFresh == nullptr )
            return;
        destroyInstance( *_pType, _pCurrent );
        _pCurrent = pFresh;
        // 읽지 못한 파일은 기본값으로 덮어 고치라는 뜻이다 — 저장을 막던 상태를 해제한다.
        if ( _readResult == ConfigReadResult::Invalid )
            _readResult = ConfigReadResult::Missing;
    }

    string ProjectSettingsDocument::makeDifferenceText() const
    {
        if ( _pType == nullptr || _pCurrent == nullptr )
            return {};
        return isElementMode() ? makeDifferenceElement() : makeDifferenceJSON();
    }

    string ProjectSettingsDocument::makeDifferenceJSON() const
    {
        const string fullText = JSONSerializer::serialize( _pCurrent, *_pType );
        JSONDocument fullDocument;
        if ( fullDocument.parse( fullText ) == false || fullDocument.getRoot().isObject() == false )
            return {};
        // 바탕 층(`ConfigBaseLayer`)은 파일이 값의 원본이라 모든 키를 쓴다(언리얼 `BaseEngine.ini` 와 같은 층).
        if ( isBaseLayer() )
            return fullDocument.dump( 4 );
        vector<string> listEqual;
        if ( _pDefault != nullptr && ConfigManager::collectEqualKeys( _pCurrent, _pDefault, *_pType, fullText, listEqual ) == false )
            return {};
        JSONDocument    document;
        const JSONValue root = document.makeObject();
        ProjectSettingsDocumentInternal::copyDifference( fullDocument.getRoot(), root, string(), listEqual );
        return document.dump( 4 );
    }

    string ProjectSettingsDocument::makeDifferenceElement() const
    {
        // 지금 값과 기본값을 같은 직렬화기로 쓰고, 기본값과 글이 같은 속성 · 자식을 뺀다(JSON 쪽 `collectEqualKeys` 와 같은 규칙).
        XMLDocument current;
        XMLDocument defaults;
        if ( current.parse( XMLSerializer::serialize( _pCurrent, *_pType ) ) == false )
            return {};
        const bool    bHasDefault = _pDefault != nullptr && defaults.parse( XMLSerializer::serialize( _pDefault, *_pType ) );
        const XMLNode currentRoot = current.getRoot();
        const XMLNode defaultRoot = bHasDefault ? defaults.getRoot() : XMLNode{};
        XMLDocument   difference;
        const XMLNode element = difference.appendRoot( _elementName.c_str() );
        for ( XMLAttribute attr = currentRoot.getFirstAttribute(); attr.isValid(); attr = attr.getNext() )
        {
            const utf8* pDefaultValue = defaultRoot.isValid() ? defaultRoot.findAttribute( attr.getName(), false ) : nullptr;
            if ( pDefaultValue != nullptr && StringUtil::equals( pDefaultValue, attr.getValue() ) )
                continue;
            element.appendAttribute( attr.getName(), attr.getValue() );
        }
        for ( XMLNode child = currentRoot.findChild(); child.isValid(); child = child.findNextSibling() )
        {
            const XMLNode defaultChild = defaultRoot.isValid() ? defaultRoot.findChild( child.getName(), false ) : XMLNode{};
            if ( defaultChild.isValid() && defaultChild.toString() == child.toString() )
                continue;
            (void)element.appendClone( child );
        }
        return element.toString();
    }

    uint32 ProjectSettingsDocument::countNonDefaultKeys() const
    {
        if ( _pType == nullptr || _pCurrent == nullptr )
            return 0;
        if ( isElementMode() )
        {
            XMLDocument element;
            if ( element.parse( makeDifferenceElement() ) == false || element.getRoot().isValid() == false )
                return 0;
            return ProjectSettingsDocumentInternal::countElementKeys( element.getRoot() );
        }
        JSONDocument document;
        if ( document.parse( makeDifferenceJSON() ) == false || document.getRoot().isObject() == false )
            return 0;
        return ProjectSettingsDocumentInternal::countKept( document.getRoot(), string(), {} );
    }

    bool ProjectSettingsDocument::isModified() const
    {
        return _pCurrent != nullptr && makeDifferenceText() != _savedText;
    }

    bool ProjectSettingsDocument::saveElement( const string& elementText ) const
    {
        // 요소는 남의 파일(팩 gamesettings.xml) 안에 산다 — 파일을 만들지 않는다.
        string fileText;
        if ( FileUtil::isRegularFile( _path ) == false || FileUtil::readTextFile( _path, fileText ) == false )
        {
            SW_LOG_WARNING( "Project settings: %# is missing - the <%#> element lives in that file", _path.c_str(), _elementName.c_str() );
            return false;
        }
        // 쓸 요소가 다음 기동에서 읽히는지 먼저 본다 — 필수 필드가 빠진 요소(Reset to Default 뒤)는 쓰지 않는다.
        void* pCheck = createInstance( *_pType );
        if ( pCheck == nullptr )
            return false;
        const bool bReadable = ConfigManager::readConfigXMLElement( pCheck, *_pType, elementText, _path.c_str() );
        destroyInstance( *_pType, pCheck );
        if ( bReadable == false )
        {
            SW_LOG_WARNING( "Project settings: the edited <%#> would not load - not saved", _elementName.c_str() );
            return false;
        }
        string newText;
        if ( ProjectSettingsDocumentInternal::spliceElement( fileText, _elementName, elementText, newText ) == false )
        {
            SW_LOG_WARNING( "Project settings: <%#> could not be placed in %# - not saved", _elementName.c_str(), _path.c_str() );
            return false;
        }
        // 바꾼 글을 다시 읽어 같은 요소인지 본다 — 원문에서 위치를 잘못 잡았으면(속성 값 안의 `>` 등) 파일을 쓰지 않는다.
        void* pReread = createInstance( *_pType );
        if ( pReread == nullptr )
            return false;
        const bool bSame = readInto( pReread, newText ) == ConfigReadResult::Loaded && XMLSerializer::serialize( pReread, *_pType ) == XMLSerializer::serialize( _pCurrent, *_pType );
        destroyInstance( *_pType, pReread );
        if ( bSame == false )
        {
            SW_LOG_WARNING( "Project settings: rewriting <%#> in %# did not round-trip - not saved", _elementName.c_str(), _path.c_str() );
            return false;
        }
        return FileUtil::writeTextFile( _path, newText );
    }

    bool ProjectSettingsDocument::save()
    {
        if ( _pCurrent == nullptr || isLoadFailed() )
            return false;
        const string text   = makeDifferenceText();
        bool         bSaved = false;
        if ( isElementMode() )
        {
            bSaved = saveElement( text );
        }
        else
        {
            FileUtil::ensureParentDirectoryExists( _path );
            bSaved = FileUtil::writeTextFile( _path, text );
        }
        if ( bSaved == false )
        {
            SW_LOG_WARNING( "Project settings could not be saved to %#", _path.c_str() );
            return false;
        }
        _savedText  = text;
        _readResult = ConfigReadResult::Loaded;
        SW_LOG_INFO( "Project settings saved: %# key(s) -> %#", countNonDefaultKeys(), _path.c_str() );
        return true;
    }
} // namespace sw::editor
