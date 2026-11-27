#include "pch.h"

#include "Engine/Content/AssetIDKey.h"

#include "Core/String/StringUtil.h"

namespace sw
{
    AssetIDKey::AssetIDKey( const string_view assetID )
        : _folded{ StringUtil::toLower( assetID ) }
    {
    }
} // namespace sw
