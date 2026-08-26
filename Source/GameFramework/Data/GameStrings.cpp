#include "pch.h"

#include "GameFramework/Data/GameStrings.h"

#include "Engine/Localization/LocalizationManager.h"

#include "RuntimeAPI/GameService.h"

#include <utility>

namespace sw
{
	namespace
	{
		static const string s_emptyString{};
	} // namespace

	bool GameStrings::loadFromResource( string_view assetRelativePath )
	{
		clear();

		LocalizationManager* pLoc = game::getService<LocalizationManager>();
		if ( pLoc == nullptr )
		{
			SW_LOG_ERROR( "[GameStrings] LocalizationManager service is not bound." );
			return false;
		}

		const bool bLoaded = pLoc->loadLanguageResource( "default", assetRelativePath );
		if ( bLoaded )
		{
			pLoc->setCurrentLanguage( "default" );
		}
		return bLoaded;
	}

	bool GameStrings::setupLocalization( string_view directoryOrResourcePath, string_view defaultLanguage, string_view fallbackLanguage )
	{
		clear();

		LocalizationManager* pLoc = game::getService<LocalizationManager>();
		if ( pLoc == nullptr )
		{
			SW_LOG_ERROR( "[GameStrings] LocalizationManager service is not bound." );
			return false;
		}

		return pLoc->setupLocalization( directoryOrResourcePath, defaultLanguage, fallbackLanguage );
	}

	bool GameStrings::loadLanguage( string_view languageCode, string_view assetRelativePath )
	{
		LocalizationManager* pLoc = game::getService<LocalizationManager>();
		if ( pLoc == nullptr )
		{
			SW_LOG_ERROR( "[GameStrings] LocalizationManager service is not bound." );
			return false;
		}

		return pLoc->loadLanguageResource( languageCode, assetRelativePath );
	}

	bool GameStrings::loadLanguageFile( string_view languageCode, string_view filePath )
	{
		LocalizationManager* pLoc = game::getService<LocalizationManager>();
		if ( pLoc == nullptr )
		{
			SW_LOG_ERROR( "[GameStrings] LocalizationManager service is not bound." );
			return false;
		}

		return pLoc->loadLanguageFile( languageCode, filePath );
	}

	bool GameStrings::loadLanguageDirectory( string_view directoryPath, string_view filterExtension, bool bRecursive )
	{
		LocalizationManager* pLoc = game::getService<LocalizationManager>();
		if ( pLoc == nullptr )
		{
			SW_LOG_ERROR( "[GameStrings] LocalizationManager service is not bound." );
			return false;
		}

		return pLoc->loadLanguageDirectory( directoryPath, filterExtension, bRecursive );
	}

	bool GameStrings::setLanguage( string_view languageCode )
	{
		LocalizationManager* pLoc = game::getService<LocalizationManager>();
		if ( pLoc == nullptr )
			return false;

		return pLoc->setCurrentLanguage( languageCode );
	}

	const string& GameStrings::getLanguage()
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

	const string& GameStrings::getFallbackLanguage()
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

	uint32 GameStrings::onLanguageChanged( LanguageChangedCallback callback )
	{
		LocalizationManager* pLoc = game::getService<LocalizationManager>();
		if ( pLoc == nullptr )
			return 0;

		return pLoc->registerLanguageChangedCallback( std::move( callback ) );
	}

	void GameStrings::removeLanguageChangedCallback( uint32 callbackId )
	{
		LocalizationManager* pLoc = game::getService<LocalizationManager>();
		if ( pLoc != nullptr )
			pLoc->unregisterLanguageChangedCallback( callbackId );
	}

	void GameStrings::clear()
	{
		LocalizationManager* pLoc = game::getService<LocalizationManager>();
		if ( pLoc != nullptr )
			pLoc->clear();
	}
} // namespace sw
