#include "pch.h"

#include "Engine/Config/ConfigManager.h"

namespace sw
{
    namespace
    {
        /** @brief 호스트가 알린 표입니다(`ConfigManager::setPrimary`). */
        ConfigManager* s_pPrimaryConfigManager = nullptr;
    } // namespace
} // namespace sw

namespace sw
{
    bool ConfigManager::reloadConfigFile( string_view changedPath )
    {
        for ( const auto& [key, source] : _mapSource )
        {
            (void)key;
            if ( source._pReload != nullptr && FileUtil::pathsEqualNormalized( source._resolvedPath, changedPath ) )
                return source._pReload( *this, source._resolvedPath );
        }
        return false;
    }

    ConfigManager* ConfigManager::findPrimary()
    {
        return s_pPrimaryConfigManager;
    }

    void ConfigManager::setPrimary( ConfigManager* pManager )
    {
        s_pPrimaryConfigManager = pManager;
    }
} // namespace sw
