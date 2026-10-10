#include "pch.h"

#include "GameFramework/Base/Foundation/Framework/Presentation/GameStrings.h"

#include "Core/Common/StdHeaders.h"

#include "Engine/Localization/LocalizationManager.h"

#include "GameFramework/Base/Foundation/Framework/GameService.h"

namespace sw
{
    SW_LOG_CALLER( "GameStrings" );

    namespace
    {
        static const string s_emptyString{};
    } // namespace

    bool GameStrings::initialize( string_view projectPath, string_view defaultLanguage, string_view fallbackLanguage )
    {
        LocalizationManager* pLoc = game::getService<LocalizationManager>();
        if ( pLoc == nullptr )
        {
            SW_LOG_ERROR( "LocalizationManager service is not bound." );
            return false;
        }

        return pLoc->initialize( projectPath, defaultLanguage, fallbackLanguage );
    }

    bool GameStrings::setLanguage( string_view languageCode )
    {
        LocalizationManager* pLoc = game::getService<LocalizationManager>();
        if ( pLoc == nullptr )
            return false;

        return pLoc->setCurrentLanguage( languageCode );
    }

    string GameStrings::getLanguage()
    {
        LocalizationManager* pLoc = game::getService<LocalizationManager>();
        if ( pLoc == nullptr )
            return s_emptyString;

        return pLoc->getCurrentLanguage();
    }

    void GameStrings::setFallbackLanguage( string_view languageCode )
    {
        LocalizationManager* pLoc = game::getService<LocalizationManager>();
        if ( pLoc != nullptr )
            pLoc->setFallbackLanguage( languageCode );
    }

    string GameStrings::getFallbackLanguage()
    {
        LocalizationManager* pLoc = game::getService<LocalizationManager>();
        if ( pLoc == nullptr )
            return s_emptyString;

        return pLoc->getFallbackLanguage();
    }

    bool GameStrings::hasLanguage( string_view languageCode )
    {
        LocalizationManager* pLoc = game::getService<LocalizationManager>();
        if ( pLoc == nullptr )
            return false;

        return pLoc->hasLanguage( languageCode );
    }

    vector<string> GameStrings::getAvailableLanguages()
    {
        LocalizationManager* pLoc = game::getService<LocalizationManager>();
        if ( pLoc == nullptr )
            return {};

        return pLoc->getAvailableLanguages();
    }

    const utf8* GameStrings::get( const utf8* pKey, const utf8* pFallback )
    {
        if ( pKey == nullptr )
            return pFallback;

        LocalizationManager* pLoc = game::getService<LocalizationManager>();
        if ( pLoc == nullptr )
            return pFallback;

        return pLoc->getString( hashed_string( pKey ), pFallback );
    }

    const utf8* GameStrings::getFromLanguage( string_view languageCode, const utf8* pKey, const utf8* pFallback )
    {
        if ( pKey == nullptr )
            return pFallback;

        LocalizationManager* pLoc = game::getService<LocalizationManager>();
        if ( pLoc == nullptr )
            return pFallback;

        return pLoc->getStringFromLanguage( languageCode, hashed_string( pKey ), pFallback );
    }

    uint32 GameStrings::registerLanguageChangedCallback( LanguageChangedCallback callback )
    {
        LocalizationManager* pLoc = game::getService<LocalizationManager>();
        if ( pLoc == nullptr )
            return 0;

        return pLoc->registerLanguageChangedCallback( std::move( callback ) );
    }

    void GameStrings::unregisterLanguageChangedCallback( uint32 callbackID )
    {
        LocalizationManager* pLoc = game::getService<LocalizationManager>();
        if ( pLoc != nullptr )
            pLoc->unregisterLanguageChangedCallback( callbackID );
    }

    void GameStrings::clear()
    {
        LocalizationManager* pLoc = game::getService<LocalizationManager>();
        if ( pLoc != nullptr )
            pLoc->unmountProjects( LocalizationScope::Game );
    }
} // namespace sw
