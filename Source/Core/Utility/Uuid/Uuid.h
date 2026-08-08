#pragma once
/**
 * @file Uuid.h
 * @brief 128-bit UUID (version 4) generate + string conversion.
 */
#include "Core/Common/CommonHeaders.h"
#include "Core/Common/CommonMacros.h"
#include "Core/Common/Types.h"

namespace sw
{
	/**
	 * @struct Uuid
	 * @brief RFC 4122 variant UUID stored as 16 bytes.
	 */
	struct SW_API Uuid
	{
		uint8 _bytes[16]{};

		/** @brief Generate a random UUID v4. */
		static Uuid generate();

		/** @brief Parse "xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx"; returns false on failure. */
		static bool tryParse( std::string_view text, Uuid& outUuid );

		/** @brief Canonical lowercase hex form with hyphens. */
		std::string toString() const;

		bool isNil() const;
		bool operator==( const Uuid& other ) const;
		bool operator!=( const Uuid& other ) const { return !( *this == other ); }
	};
} // namespace sw
