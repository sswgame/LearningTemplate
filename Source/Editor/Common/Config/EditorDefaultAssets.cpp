#include "pch.h"

#include "Editor/Common/Config/EditorDefaultAssets.h"

#include "Engine/Config/DefaultAssetTable.h"
#include "Engine/Content/AssetID.h"

namespace sw::editor
{
    bool EditorDefaultAssets::load( EditorDefaultAssets& outAssets )
    {
        return DefaultAssetTable::load( AssetID{ kAsset }, &outAssets, *StaticType() );
    }
} // namespace sw::editor
