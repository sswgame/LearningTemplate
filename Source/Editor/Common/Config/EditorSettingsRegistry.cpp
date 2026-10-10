#include "pch.h"

#include "Editor/Common/Config/EditorSettingsRegistry.h"

#include "Core/File/FileUtil.h"

#include "Editor/Common/EditorUtil.h"

#include "Engine/Reflection/ReflectionTypes.h"
#include "Engine/Serialization/Base/SerializerUtil.h"
#include "Engine/Serialization/JSON/JSONDocument.h"

namespace sw::editor
{
    SW_LOG_CALLER( "EditorPreferences" );

    namespace
    {
        struct EditorSettingsRegistryInternal
        {
            using SettingsRegistry = EditorRegistry<EditorSettingsRegistration>;

            /** @brief @p pCurrent 가 기본과 다른 프로퍼티를 @p object 에 담습니다. 담은 수입니다. */
            static uint32 writeDifference( const JSONValue& object, const TypeInfo& type, const void* pCurrent, const void* pDefault )
            {
                uint32 writtenCount{ 0 };
                for ( const PropertyInfo& prop : type.getPropertiesWithBase() )
                {
                    if ( EditorPreferencesStore::isPropertyModified( prop, pCurrent, pDefault ) == false )
                        continue;
                    object.set( prop._name.c_str() ).setString( SerializerUtil::formatPropertyText( prop, pCurrent, SerializeContext::getDefault() ) );
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
                    if ( SerializerUtil::applyPropertyText( *pProperty, pInstance, text, SerializeContext::getDefault() ) == false )
                        SW_LOG_WARNING( "Editor preference %#.%# = '%#' could not be applied - kept the current value", type._name.c_str(), key.c_str(), text.c_str() );
                }
            }
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    bool EditorPreferencesStore::loadAll( string_view filePath )
    {
        if ( FileUtil::isRegularFile( filePath ) == false )
            return false;
        JSONDocument document;
        if ( document.loadFile( filePath ) == false || document.getRoot().isObject() == false )
        {
            SW_LOG_WARNING( "Editor preferences %# could not be read - using defaults", string( filePath ).c_str() );
            return false;
        }
        const JSONValue root = document.getRoot();
        for ( const string& sectionID : root.getMemberNames() )
        {
            const EditorSettingsRegistration* pRegistration = EditorSettingsRegistryInternal::SettingsRegistry::find( sectionID );
            if ( pRegistration == nullptr )
            {
                SW_LOG_WARNING( "Editor preferences: unknown section '%#' skipped (its module may be off)", sectionID.c_str() );
                continue;
            }
            vector<string> listUnknownKey;
            EditorSettingsRegistryInternal::applyObject( root.get( sectionID ), *pRegistration->_pfnGetType(), pRegistration->_pfnGetInstance(), listUnknownKey );
            for ( const string& key : listUnknownKey )
            {
                SW_LOG_WARNING( "Editor preferences: unknown key '%#.%#' skipped", sectionID.c_str(), key.c_str() );
            }
            if ( pRegistration->_pfnOnChanged != nullptr )
                pRegistration->_pfnOnChanged();
        }
        return true;
    }

    bool EditorPreferencesStore::saveAll( string_view filePath )
    {
        JSONDocument    document;
        const JSONValue root = document.makeObject();
        uint32          keyCount{ 0 };
        using SettingsRegistry = EditorSettingsRegistryInternal::SettingsRegistry;
        for ( uint32 index = 0; index < SettingsRegistry::getCount(); ++index )
        {
            const EditorSettingsRegistration& registration = SettingsRegistry::getAt( index );
            const TypeInfo*                   pType        = registration._pfnGetType();
            if ( pType == nullptr )
                continue;
            JSONDocument    sectionDocument;
            const JSONValue section      = sectionDocument.makeObject();
            const uint32    writtenCount = EditorSettingsRegistryInternal::writeDifference( section, *pType, registration._pfnGetInstance(), registration._pfnGetDefault() );
            if ( writtenCount == 0 )
                continue;
            root.set( registration._pID ).assignFrom( section );
            keyCount += writtenCount;
        }
        FileUtil::ensureParentDirectoryExists( filePath );
        if ( document.saveFile( filePath, 4 ) == false )
        {
            SW_LOG_WARNING( "Editor preferences could not be saved to %#", string( filePath ).c_str() );
            return false;
        }
        SW_LOG_INFO( "Editor preferences saved: %# key(s) -> %#", keyCount, string( filePath ).c_str() );
        return true;
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

    bool EditorPreferencesStore::isPropertyModified( const PropertyInfo& prop, const void* pCurrent, const void* pDefault )
    {
        const SerializeContext& context = SerializeContext::getDefault();
        return SerializerUtil::formatPropertyText( prop, pCurrent, context ) != SerializerUtil::formatPropertyText( prop, pDefault, context );
    }

    void EditorPreferencesStore::resetSection( const EditorSettingsRegistration& registration )
    {
        const TypeInfo* pType = registration._pfnGetType();
        if ( pType == nullptr )
            return;
        void*                   pInstance = registration._pfnGetInstance();
        const void*             pDefault  = registration._pfnGetDefault();
        const SerializeContext& context   = SerializeContext::getDefault();
        for ( const PropertyInfo& prop : pType->getPropertiesWithBase() )
        {
            if ( SerializerUtil::applyPropertyText( prop, pInstance, SerializerUtil::formatPropertyText( prop, pDefault, context ), context ) == false )
                SW_LOG_WARNING( "Reset Section could not restore %#.%#", pType->_name.c_str(), prop._name.c_str() );
        }
        if ( registration._pfnOnChanged != nullptr )
            registration._pfnOnChanged();
    }

    uint32 EditorPreferencesStore::countSavedKeys( string_view filePath )
    {
        JSONDocument document;
        if ( FileUtil::isRegularFile( filePath ) == false || document.loadFile( filePath ) == false || document.getRoot().isObject() == false )
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
        return EditorUtil::resolveEditorStateFile( kFileName );
    }

    const EditorSettingsRegistration* findSettingsRegistration( const TypeInfo* pType )
    {
        using SettingsRegistry = EditorSettingsRegistryInternal::SettingsRegistry;
        for ( uint32 index = 0; index < SettingsRegistry::getCount(); ++index )
        {
            const EditorSettingsRegistration& registration = SettingsRegistry::getAt( index );
            if ( registration._pfnGetType() == pType )
                return &registration;
        }
        return nullptr;
    }
} // namespace sw::editor
