#include "pch.h"

#include "GameFramework/Base/Gameplay/Appearance/AppearancePreload.h"

#include "Core/Container/VectorUtil.h"

#include "GameFramework/Base/Gameplay/Appearance/AppearanceDatabase.h"
#include "GameFramework/Base/Gameplay/Appearance/AppearanceResolver.h"
#include "GameFramework/Base/Gameplay/Appearance/CharacterAppearance.h"

namespace sw
{
    SW_LOG_CALLER( "AppearancePreload" );

    namespace
    {
        void addPreloadPath( const hashed_string& path, vector<string>& inoutListPath )
        {
            if ( path.empty() )
                return;
            string text( path.c_str() );
            if ( VectorUtil::contains( inoutListPath, text ) == false )
                inoutListPath.push_back( std::move( text ) );
        }
    } // namespace
} // namespace sw

namespace sw
{
    bool AppearancePreloadUtil::collectPresetAssets( const AppearanceDatabase& database, const hashed_string& presetID, uint32 seed, vector<string>& inoutListPath )
    {
        CharacterAppearanceSpec spec;
        if ( database.getPresets().expand( presetID, seed, database.getSlotTable(), database.getSets(), database.getSchemas(), spec ) == false )
        {
            SW_LOG_ERROR( "Appearance preset '%#' could not be expanded - its assets are not collected", presetID.c_str() );
            return false;
        }
        ResolvedAppearance resolved;
        AppearanceResolver::resolve( database, spec, resolved );
        for ( const ResolvedPart& part : resolved._listPart )
        {
            addPreloadPath( part._asset, inoutListPath );
            addPreloadPath( part._material, inoutListPath );
            addPreloadPath( part._skeleton, inoutListPath );
            addPreloadPath( part._socketSet, inoutListPath );
            addPreloadPath( part._fit, inoutListPath );
            addPreloadPath( part._fitTables, inoutListPath );
            addPreloadPath( part._bodyShapes, inoutListPath );
        }
        for ( const ResolvedAttachment& attachment : resolved._listAttachment )
        {
            addPreloadPath( attachment._asset, inoutListPath );
        }
        return true;
    }
} // namespace sw
