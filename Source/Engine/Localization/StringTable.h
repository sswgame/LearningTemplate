#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/String/hashed_string.h"

#include <shared_mutex>

namespace sw
{
	class SW_API StringTable
	{
	public:
		bool		loadFromFile( const string& filePath );
		bool		loadFromJsonText( string_view jsonText );
		bool		loadFromXmlText( string_view xmlText );
		bool		loadFromKeyValueText( string_view kvText );
		bool		loadFromResource( string_view assetRelativePath );
		bool		saveToBinaryFile( string_view filePath ) const;
		bool		loadFromBinaryFile( string_view filePath );
		const utf8* getString( const hashed_string& key ) const;
		const utf8* getString( const hashed_string& key, const utf8* pDefaultText ) const;
		bool		contains( const hashed_string& key ) const;
		void		setString( const hashed_string& key, const string& value );
		void		clear();
		size_t		size() const;
		bool		empty() const;

	private:
		mutable std::shared_mutex	  _mutex;
		unordered_map<uint64, string> _mapTable;
	};
} // namespace sw
