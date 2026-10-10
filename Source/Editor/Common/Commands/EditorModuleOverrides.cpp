#include "pch.h"

#include "Editor/Common/Commands/EditorModuleOverrides.h"

#include "Engine/Module/ModuleCatalog.h"
#include "Engine/Serialization/JSON/JSONDocument.h"

namespace sw::editor
{
    bool EditorModuleOverrideUtil::setOverride( string_view manifestJSON, string_view moduleName, bool bEnabled, bool bEnabledByDefault, string& outManifestJSON )
    {
        JSONDocument document;
        if ( document.parse( manifestJSON ) == false || document.getRoot().isObject() == false )
            return false;
        const JSONValue root = document.getRoot();
        // 남길 줄을 모은다 — 그 모듈의 옛 줄은 빼고, 기본과 다르면 새 줄 하나.
        vector<std::pair<string, bool>> listEntry;
        const JSONValue                 listOld = root.get( "_listModuleOverride", false );
        if ( listOld.isValid() && listOld.isArray() )
        {
            for ( size_t index = 0; index < listOld.size(); ++index )
            {
                const JSONValue entry = listOld.at( index );
                const string    name  = entry.get( "_name", false ).asString();
                if ( name != moduleName )
                    listEntry.emplace_back( name, entry.get( "_bEnabled", false ).asBool() );
            }
        }
        if ( bEnabled != bEnabledByDefault )
            listEntry.emplace_back( string( moduleName ), bEnabled );

        const JSONValue listNew = root.set( "_listModuleOverride", false );
        listNew.setArray();
        for ( const std::pair<string, bool>& entry : listEntry )
        {
            const JSONValue item = listNew.pushBack();
            item.setObject();
            item.set( "_name", false ).setString( entry.first );
            item.set( "_bEnabled", false ).setBool( entry.second );
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
} // namespace sw::editor
