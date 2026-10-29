#include "pch.h"

#include "Editor/Common/Commands/Dev/EditorModuleOverrides.h"

#include "Core/Container/VectorUtil.h"
#include "Core/Container/pair.h"

#include "Engine/Module/ModuleCatalog.h"
#include "Engine/Serialization/JSON/JSONDocument.h"

#include <algorithm>

namespace sw::editor
{
    bool EditorModuleOverrideUtil::setOverride( string_view manifestJSON, string_view moduleName, bool bEnabled, bool bEnabledByDefault, string& outManifestJSON )
    {
        JSONDocument document;
        if ( document.parse( manifestJSON ) == false || document.getRoot().isObject() == false )
            return false;
        const JSONValue root = document.getRoot();
        // 남길 줄을 모은다 — 그 모듈의 옛 줄은 빼고, 기본과 다르면 새 줄 하나.
        vector<pair<string, bool>> listEntry;
        const JSONValue            listOld = root.get( "_listModuleOverride" );
        if ( listOld.isValid() && listOld.isArray() )
        {
            for ( size_t index = 0; index < listOld.size(); ++index )
            {
                const JSONValue entry = listOld.at( index );
                const string    name  = entry.get( "_name" ).asString();
                if ( name == moduleName )
                    continue;
                // 다른 모듈의 줄은 그대로 옮긴다. 켬 · 끔이 불리언이 아닌 줄은 고쳐 쓰지 않고 실패한다(읽기가 알린다).
                const std::optional<bool> bOtherEnabled = entry.readRequired<bool>( "_bEnabled" );
                if ( bOtherEnabled.has_value() == false )
                    return false;
                listEntry.emplace_back( name, *bOtherEnabled );
            }
        }
        if ( bEnabled != bEnabledByDefault )
            listEntry.emplace_back( string( moduleName ), bEnabled );

        const JSONValue listNew = root.set( "_listModuleOverride" );
        listNew.setArray();
        for ( const pair<string, bool>& entry : listEntry )
        {
            const JSONValue item = listNew.pushBack();
            item.setObject();
            item.set( "_name" ).setString( entry.first );
            item.set( "_bEnabled" ).setBool( entry.second );
        }
        outManifestJSON = document.dump( 4 );
        return true;
    }

    bool EditorModuleOverrideUtil::previewToggle( const ModuleCatalog& catalog, const ModuleResolveContext& context, string_view moduleName, bool bEnabled,
                                                  vector<ModuleInactiveEntry>& outListNewlyInactive, vector<string>& outListNewlyActive, string& outError )
    {
        outListNewlyInactive.clear();
        outListNewlyActive.clear();
        ModuleResolution before;
        if ( catalog.resolve( context, before, outError ) == false )
            return false;
        ModuleCatalog changed = catalog;
        if ( changed.setProjectOverride( context._projectModule, moduleName, bEnabled ) == false )
        {
            outError = "the project module " + context._projectModule + " has no manifest";
            return false;
        }
        ModuleResolution after;
        if ( changed.resolve( context, after, outError ) == false )
            return false;
        for ( const ModuleInactiveEntry& entry : after._listInactive )
        {
            if ( before.isActive( entry._name ) )
                outListNewlyInactive.push_back( entry );
        }
        for ( const string& name : after._listLoadOrder )
        {
            if ( before.isActive( name ) == false )
                outListNewlyActive.push_back( name );
        }
        return true;
    }

    bool EditorModuleOverrideUtil::makeDetail( const ModuleCatalog& catalog, string_view projectModule, string_view moduleName, EditorModuleDetail& outDetail )
    {
        outDetail                       = EditorModuleDetail{};
        const ModuleManifest* pManifest = catalog.findManifest( moduleName );
        if ( pManifest == nullptr )
            return false;
        outDetail._name              = pManifest->_name;
        outDetail._description       = pManifest->_description;
        outDetail._version           = pManifest->_version.toString();
        outDetail._bEnabledByDefault = pManifest->_bEnabledByDefault;
        for ( const ModuleDependency& dependency : pManifest->_listDependency )
        {
            outDetail._listDependency.push_back( dependency._name );
        }
        for ( const ModuleManifest& other : catalog.getManifests() )
        {
            for ( const ModuleDependency& dependency : other._listDependency )
            {
                if ( dependency._name == pManifest->_name )
                {
                    outDetail._listDependent.push_back( other._name );
                    break;
                }
            }
        }
        VectorUtil::sort( outDetail._listDependent );
        const ModuleManifest* pProject = catalog.findManifest( projectModule );
        if ( pProject != nullptr )
        {
            for ( const ModuleOverride& entry : pProject->_listModuleOverride )
            {
                if ( entry._name == pManifest->_name )
                {
                    outDetail._bHasOverride     = true;
                    outDetail._bOverrideEnabled = entry._bEnabled;
                    break;
                }
            }
        }
        return true;
    }
} // namespace sw::editor
