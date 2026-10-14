#include "pch.h"

#include "Editor/Panels/ProjectSettings/ProjectSettingsDocument.h"

#include "Core/Container/vector.h"
#include "Core/File/FileUtil.h"
#include "Core/Memory/Memory.h"

#include "Engine/Reflection/ReflectionCore.h"
#include "Engine/Serialization/Format/JSONSerializer.h"
#include "Engine/Serialization/JSON/JSONDocument.h"

namespace sw::editor
{
    SW_LOG_CALLER( "ProjectSettings" );

    namespace
    {
        struct ProjectSettingsDocumentInternal
        {
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

            /** @brief @p source 에서 @p listEqual 에 든 경로를 뺀 말단의 수입니다. */
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
             * @brief @p source 의 말단을 @p destination 으로 옮기되 @p listEqual 에 든 경로는 뺍니다.
             * @details 말단이 하나도 남지 않는 구조체 칸은 만들지 않는다. JSONValue 핸들은 형제를 더하면 무효가 되므로, 칸 하나를 다 채운 뒤에 다음 칸을 받는다.
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

    ConfigReadResult ProjectSettingsDocument::open( const TypeInfo& type, string_view absolutePath )
    {
        shutdown();
        _pType    = &type;
        _path     = string( absolutePath );
        _pCurrent = createInstance( type );
        _pDefault = createInstance( type );
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
        _savedText.clear();
    }

    ConfigReadResult ProjectSettingsDocument::revert()
    {
        if ( _pType == nullptr || _pCurrent == nullptr )
            return ConfigReadResult::Invalid;
        if ( FileUtil::isRegularFile( _path ) == false )
        {
            resetToDefault();
            _readResult = ConfigReadResult::Missing;
            _savedText  = makeDifferenceJSON();
            return _readResult;
        }
        string jsonText;
        if ( FileUtil::readTextFile( _path, jsonText ) == false )
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
        if ( ConfigManager::readConfigJSON( pFresh, *_pType, jsonText, _path.c_str() ) == false )
        {
            destroyInstance( *_pType, pFresh );
            _readResult = ConfigReadResult::Invalid;
            return _readResult;
        }
        destroyInstance( *_pType, _pCurrent );
        _pCurrent   = pFresh;
        _readResult = ConfigReadResult::Loaded;
        _savedText  = makeDifferenceJSON();
        return _readResult;
    }

    void ProjectSettingsDocument::resetToDefault()
    {
        if ( _pType == nullptr || _pCurrent == nullptr )
            return;
        void* pFresh = createInstance( *_pType );
        if ( pFresh == nullptr )
            return;
        destroyInstance( *_pType, _pCurrent );
        _pCurrent = pFresh;
        // 읽지 못한 파일은 기본값으로 덮어 고치라는 뜻이다 — 저장을 막던 상태를 푼다.
        if ( _readResult == ConfigReadResult::Invalid )
            _readResult = ConfigReadResult::Missing;
    }

    string ProjectSettingsDocument::makeDifferenceJSON() const
    {
        if ( _pType == nullptr || _pCurrent == nullptr )
            return {};
        const string fullText = JSONSerializer::serialize( _pCurrent, *_pType );
        JSONDocument fullDocument;
        if ( fullDocument.parse( fullText ) == false || fullDocument.getRoot().isObject() == false )
            return {};
        vector<string> listEqual;
        if ( _pDefault != nullptr && ConfigManager::collectEqualKeys( _pCurrent, _pDefault, *_pType, fullText, listEqual ) == false )
            return {};
        JSONDocument    document;
        const JSONValue root = document.makeObject();
        ProjectSettingsDocumentInternal::copyDifference( fullDocument.getRoot(), root, string(), listEqual );
        return document.dump( 4 );
    }

    uint32 ProjectSettingsDocument::countNonDefaultKeys() const
    {
        if ( _pType == nullptr || _pCurrent == nullptr )
            return 0;
        JSONDocument document;
        if ( document.parse( makeDifferenceJSON() ) == false || document.getRoot().isObject() == false )
            return 0;
        return ProjectSettingsDocumentInternal::countKept( document.getRoot(), string(), {} );
    }

    bool ProjectSettingsDocument::isModified() const
    {
        return _pCurrent != nullptr && makeDifferenceJSON() != _savedText;
    }

    bool ProjectSettingsDocument::save()
    {
        if ( _pCurrent == nullptr || isLoadFailed() )
            return false;
        const string text = makeDifferenceJSON();
        FileUtil::ensureParentDirectoryExists( _path );
        if ( FileUtil::writeTextFile( _path, text ) == false )
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
