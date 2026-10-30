#include "pch.h"

#include "Editor/Common/Config/EditorSettingsRegistry.h"

#include "Core/File/FileUtil.h"
#include "Core/Foundation/StdHeaders.h"
#include "Core/Log/Logger.h"

#include "Editor/Common/Config/EditorConfig.h"
#include "Editor/Common/Config/EditorPaths.h"
#include "Editor/Common/Config/EditorUserFile.h"

#include "Engine/Reflection/ReflectionTypes.h"
#include "Engine/Serialization/JSON/JSONDocument.h"
#include "Engine/Serialization/Serializer/SerializerUtil.h"

namespace sw::editor
{
    SW_LOG_CALLER( "EditorPreferences" );

    namespace
    {
        struct EditorSettingsRegistryInternal
        {
            using SettingsRegistry = EditorExtensionRegistry<EditorSettingsRegistration>;

            /** @brief @p pCurrent 가 기본과 다른 프로퍼티를 @p object 에 담습니다. 담은 수입니다. */
            static uint32 writeDifference( const JSONValue& object, const TypeInfo& type, const void* pCurrent, const void* pDefault )
            {
                uint32 writtenCount{ 0 };
                for ( const PropertyInfo& prop : type.getPropertiesWithBase() )
                {
                    if ( EditorPreferencesStore::isPropertyModified( prop, ObjectAddress{ pCurrent }, ObjectAddress{ pDefault } ) == false )
                        continue;
                    object.set( prop._name.c_str() ).setString( SerializerUtil::formatPropertyText( prop, ObjectAddress{ pCurrent }, SerializeContext::getDefault( SerializePurpose::Authoring ) ) );
                    ++writtenCount;
                }
                return writtenCount;
            }

            /** @brief JSON 오브젝트 값을 인스턴스에 입힙니다. 모르는 키는 @p outListUnknownKey 에 담습니다. */
            static void applyObject( const JSONValue& object, const TypeInfo& type, void* pInstance, vector<string>& outListUnknownKey )
            {
                for ( const string& key : object.getMemberNames() )
                {
                    const PropertyInfo* pProperty = type.findPropertyInHierarchy( hashed_string( key.c_str() ) );
                    if ( pProperty == nullptr )
                    {
                        outListUnknownKey.push_back( key );
                        continue;
                    }
                    const string text = object.get( key ).asString();
                    if ( SerializerUtil::applyPropertyText( *pProperty, MutableObjectAddress{ pInstance }, text, SerializeContext::getDefault( SerializePurpose::Authoring ) ) == false )
                        SW_LOG_WARNING( "Editor preference %#.%# = '%#' could not be applied - kept the current value", type._name.c_str(), key.c_str(), text.c_str() );
                }
            }

            /**
             * @brief 파일 값을 입힌 섹션인지입니다. 기록은 항목 자신(`_bFileValueApplied`)이 든다 — 항목은 모듈이 올라올 때 새로 생겨 저절로 거짓이다. 메인 스레드만 씁니다.
             * @details 확장 모듈이 다시 올라오면 그 섹션의 값은 기본값이다. 아직 입히지 않은 섹션만 `loadNewSections` 가 읽어, 사용자가 바꾼 값을 두 번 덮지 않는다.
             */
            static bool isLoaded( const EditorSettingsRegistration& registration ) { return registration._bFileValueApplied == SW_TRUE; }

            static void markLoaded( const EditorSettingsRegistration& registration ) { registration._bFileValueApplied = SW_TRUE; }

            /** @brief `open` 과 `close` 사이인지입니다. 소멸자 없는 값이라 모듈이 내려가는 정적 해제 중에 읽어도 된다. 메인 스레드만 씁니다. */
            static bool& isStoreOpen()
            {
                static bool s_bOpen = false;
                return s_bOpen;
            }

            /** @brief 섹션 하나에 파일 루트의 값을 입히고 `_pfnOnChanged` 를 부릅니다. */
            static void applySection( const JSONValue& root, const string& sectionID, const EditorSettingsRegistration& registration )
            {
                vector<string> listUnknownKey;
                applyObject( root.get( sectionID ), *registration._pfnGetType(), registration._pInstance, listUnknownKey );
                for ( const string& key : listUnknownKey )
                {
                    SW_LOG_WARNING( "Editor preferences: unknown key '%#.%#' skipped", sectionID.c_str(), key.c_str() );
                }
                if ( registration._pfnOnChanged != nullptr )
                    registration._pfnOnChanged();
            }
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    EditorUserFileResult EditorPreferencesStore::loadAll( string_view filePath )
    {
        using SettingsRegistry = EditorSettingsRegistryInternal::SettingsRegistry;
        for ( uint32 index = 0; index < SettingsRegistry::getCount(); ++index )
        {
            const EditorSettingsRegistration& registration = SettingsRegistry::getAt( index );
            if ( registration._pfnGetType() != nullptr )
                EditorSettingsRegistryInternal::markLoaded( registration );
        }
        if ( FileUtil::isRegularFile( filePath ) == false )
            return EditorUserFileResult::Missing;
        JSONDocument document;
        if ( document.loadFile( filePath ) == false || document.getRoot().isObject() == false )
        {
            // 깨진 사용자 파일은 치우고 새로 쓴다 — 그대로 두면 다음 저장이 사용자가 고친 값을 말없이 덮는다.
            if ( EditorUserFile::quarantineCorruptFile( filePath, "not a JSON object" ) )
                (void)saveAll( filePath ); // 실패는 saveAll 이 경고로 알린다
            return EditorUserFileResult::Recovered;
        }
        const JSONValue root = document.getRoot();
        for ( const string& sectionID : root.getMemberNames() )
        {
            const EditorSettingsRegistration* pRegistration = EditorSettingsRegistryInternal::SettingsRegistry::find( sectionID );
            if ( pRegistration == nullptr || pRegistration->_pfnGetType() == nullptr )
            {
                SW_LOG_WARNING( "Editor preferences: unknown section '%#' skipped (its module may be off)", sectionID.c_str() );
                continue;
            }
            EditorSettingsRegistryInternal::applySection( root, sectionID, *pRegistration );
        }
        return EditorUserFileResult::Loaded;
    }

    uint32 EditorPreferencesStore::loadNewSections( string_view filePath )
    {
        using SettingsRegistry = EditorSettingsRegistryInternal::SettingsRegistry;
        vector<const EditorSettingsRegistration*> listPending;
        for ( uint32 index = 0; index < SettingsRegistry::getCount(); ++index )
        {
            const EditorSettingsRegistration& registration = SettingsRegistry::getAt( index );
            if ( registration._pfnGetType() == nullptr || EditorSettingsRegistryInternal::isLoaded( registration ) )
                continue;
            EditorSettingsRegistryInternal::markLoaded( registration );
            listPending.push_back( &registration );
        }
        if ( listPending.empty() || FileUtil::isRegularFile( filePath ) == false )
            return 0;
        JSONDocument document;
        if ( document.loadFile( filePath ) == false || document.getRoot().isObject() == false )
        {
            // 실행 중에 깨졌다 — 기동 때와 같은 한 경로로 치운다. 새 파일은 다음 saveAll 이 쓴다(지금 쓰면 saveAll 이 이 함수를 다시 부른다).
            (void)EditorUserFile::quarantineCorruptFile( filePath, "not a JSON object" ); // 못 옮겼으면 이유를 오류로 남겼다
            return 0;
        }
        const JSONValue root = document.getRoot();
        uint32          appliedCount{ 0 };
        for ( const EditorSettingsRegistration* pRegistration : listPending )
        {
            const string sectionID{ pRegistration->_pID };
            if ( root.has( sectionID ) == false )
                continue;
            EditorSettingsRegistryInternal::applySection( root, sectionID, *pRegistration );
            ++appliedCount;
        }
        return appliedCount;
    }

    bool EditorPreferencesStore::hasNewSections()
    {
        using SettingsRegistry = EditorSettingsRegistryInternal::SettingsRegistry;
        for ( uint32 index = 0; index < SettingsRegistry::getCount(); ++index )
        {
            const EditorSettingsRegistration& registration = SettingsRegistry::getAt( index );
            if ( registration._pfnGetType() != nullptr && EditorSettingsRegistryInternal::isLoaded( registration ) == false )
                return true;
        }
        return false;
    }

    bool EditorPreferencesStore::saveAll( string_view filePath )
    {
        // 다시 올라온 확장 모듈의 섹션은 아직 기본값이다. 그대로 쓰면 파일에 있던 그 섹션 값이 지워지므로 먼저 읽어 입힌다.
        (void)EditorPreferencesStore::loadNewSections( filePath ); // 입힌 수는 쓰지 않는다
        JSONDocument            document;
        const JSONValue         root = document.makeObject();
        [[maybe_unused]] uint32 keyCount{ 0 }; // Shipping 은 SW_LOG_INFO 가 빠져 쓰는 곳이 없다
        using SettingsRegistry = EditorSettingsRegistryInternal::SettingsRegistry;
        // 지금 등록되지 않은 섹션(먼저 내려간 모듈 — 모듈이 내려갈 때마다 저장한다)은 파일에 있던 그대로 둔다. 등록된 섹션만으로 새로 쓰면
        // 종료 중 앞서 내려간 모듈의 값이 지워져 다시 켜면 기본값이었다(테마 액센트 — statedir/savedstate).
        JSONDocument previous;
        if ( FileUtil::isRegularFile( filePath ) && previous.loadFile( filePath ) && previous.getRoot().isObject() )
        {
            for ( const string& sectionID : previous.getRoot().getMemberNames() )
            {
                bool bRegistered = false;
                for ( uint32 index = 0; index < SettingsRegistry::getCount() && bRegistered == false; ++index )
                {
                    const EditorSettingsRegistration& registration = SettingsRegistry::getAt( index );
                    bRegistered                                    = registration._pfnGetType() != nullptr && sectionID == registration._pID;
                }
                if ( bRegistered == false )
                    root.set( sectionID ).assignFrom( previous.getRoot().get( sectionID ) );
            }
        }
        for ( uint32 index = 0; index < SettingsRegistry::getCount(); ++index )
        {
            const EditorSettingsRegistration& registration = SettingsRegistry::getAt( index );
            const TypeInfo*                   pType        = registration._pfnGetType();
            if ( pType == nullptr )
                continue;
            JSONDocument    sectionDocument;
            const JSONValue section      = sectionDocument.makeObject();
            const uint32    writtenCount = EditorSettingsRegistryInternal::writeDifference( section, *pType, registration._pInstance, registration._pDefault );
            if ( writtenCount == 0 )
                continue;
            root.set( registration._pID ).assignFrom( section );
            keyCount += writtenCount;
        }
        if ( FileUtil::ensureParentDirectoryExists( filePath ) == false || document.saveFile( filePath, 4 ) == false )
        {
            SW_LOG_WARNING( "Editor preferences could not be saved to %#", string( filePath ).c_str() );
            return false;
        }
        SW_LOG_INFO( "Editor preferences saved: %# key(s) -> %#", keyCount, string( filePath ).c_str() );
        return true;
    }

    void EditorSettingsRegistration::onDetaching( const EditorSettingsRegistration& registration )
    {
        if ( registration._pfnGetType() != nullptr )
            (void)EditorPreferencesStore::saveOpened(); // 실패는 saveAll 이 경고로 알린다
    }

    string EditorPreferencesStore::makeDifferenceJSON( const TypeInfo& type, const void* pCurrent, const void* pDefault )
    {
        JSONDocument    document;
        const JSONValue object = document.makeObject();
        (void)EditorSettingsRegistryInternal::writeDifference( object, type, pCurrent, pDefault ); // 수는 글에 그대로 드러난다
        return document.dump();
    }

    bool EditorPreferencesStore::applyJSON( const TypeInfo& type, void* pInstance, string_view jsonText, vector<string>& outListUnknownKey )
    {
        JSONDocument document;
        if ( document.parse( jsonText ) == false || document.getRoot().isObject() == false )
            return false;
        EditorSettingsRegistryInternal::applyObject( document.getRoot(), type, pInstance, outListUnknownKey );
        return true;
    }

    bool EditorPreferencesStore::isPropertyModified( const PropertyInfo& prop, const ObjectAddress current, const ObjectAddress defaultInstance )
    {
        const SerializeContext& context = SerializeContext::getDefault( SerializePurpose::Authoring );
        return SerializerUtil::formatPropertyText( prop, current, context ) != SerializerUtil::formatPropertyText( prop, defaultInstance, context );
    }

    void EditorPreferencesStore::resetSection( const EditorSettingsRegistration& registration )
    {
        const TypeInfo* pType = registration._pfnGetType();
        if ( pType == nullptr )
            return;
        void*                   pInstance = registration._pInstance;
        const void*             pDefault  = registration._pDefault;
        const SerializeContext& context   = SerializeContext::getDefault( SerializePurpose::Authoring );
        for ( const PropertyInfo& prop : pType->getPropertiesWithBase() )
        {
            if ( SerializerUtil::applyPropertyText( prop, MutableObjectAddress{ pInstance }, SerializerUtil::formatPropertyText( prop, ObjectAddress{ pDefault }, context ), context ) == false )
                SW_LOG_WARNING( "Reset Section could not restore %#.%#", pType->_name.c_str(), prop._name.c_str() );
        }
        if ( registration._pfnOnChanged != nullptr )
            registration._pfnOnChanged();
    }

    uint32 EditorPreferencesStore::countSavedKeys( string_view filePath )
    {
        JSONDocument document;
        const bool   bUnreadableSettingsFile = FileUtil::isRegularFile( filePath ) == false || document.loadFile( filePath ) == false || document.getRoot().isObject() == false;
        if ( bUnreadableSettingsFile )
            return 0;
        uint32 keyCount{ 0 };
        for ( const string& sectionID : document.getRoot().getMemberNames() )
        {
            keyCount += static_cast<uint32>( document.getRoot().get( sectionID ).size() );
        }
        return keyCount;
    }

    string EditorPreferencesStore::getDefaultFilePath()
    {
        return EditorPaths::resolveEditorStateFile( kFileName );
    }

    EditorUserFileResult EditorPreferencesStore::open()
    {
        EditorSettingsRegistryInternal::isStoreOpen() = true;
        return loadAll( getDefaultFilePath() );
    }

    void EditorPreferencesStore::close()
    {
        (void)saveOpened(); // 실패는 saveAll 이 경고로 알린다
        EditorSettingsRegistryInternal::isStoreOpen() = false;
    }

    bool EditorPreferencesStore::saveOpened()
    {
        if ( EditorSettingsRegistryInternal::isStoreOpen() == false )
            return true;
        return saveAll( getDefaultFilePath() );
    }

    const EditorSettingsRegistration& getSettingsRegistration( const TypeInfo* pType )
    {
        using SettingsRegistry = EditorSettingsRegistryInternal::SettingsRegistry;
        for ( uint32 index = 0; index < SettingsRegistry::getCount(); ++index )
        {
            const EditorSettingsRegistration& registration = SettingsRegistry::getAt( index );
            if ( pType != nullptr && registration._pfnGetType() == pType )
                return registration;
        }
        // 등록되지 않은 섹션을 읽었다 - 그 모듈이 꺼져 있거나 SW_EDITOR_SETTINGS 가 빠졌다. 기본값으로 대신하지 않고 모든 구성에서 이유를 남기고 멈춘다.
        SW_FATAL( "Editor preferences section for type '%#' is not registered (its module is off, or SW_EDITOR_SETTINGS is missing)", pType != nullptr ? pType->_name.c_str() : "(null)" );
    }
} // namespace sw::editor

namespace sw::editor
{
    void EditorConfig::saveToPreferences()
    {
        (void)EditorPreferencesStore::saveOpened(); // 실패는 saveAll 이 경고로 알린다
    }
} // namespace sw::editor
