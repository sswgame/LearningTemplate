#include "pch.h"

#include "Engine/Graphics/Material/RenderBlendPreset.h"

#include "Engine/Content/AssetID.h"
#include "Engine/Content/Pack/AssetCookRoot.h"
#include "Engine/Serialization/Format/ReflectedXMLFile.h"

namespace sw
{
    const RenderBlendPreset* RenderBlendPresetTable::find( string_view name )
    {
        // 함수 지역 정적 변수 — 첫 호출의 초기화가 스레드 안전하고, 프리셋을 쓰지 않는 실행(서버 · 쿠커의 일부)은 파일을 읽지 않는다.
        static const RenderBlendPresetTable table = []()
        {
            RenderBlendPresetTable loaded;
            if ( ReflectedXMLFile::loadDesc( AssetID{ string_view( kAssetPath ) }, loaded ) == false )
                SW_LOG_ERROR_ID( "RenderBlendPresetTable.LoadFailed", "The blend preset table '%#' could not be loaded - every _blendPreset is unknown", kAssetPath );
            return loaded;
        }();
        for ( const RenderBlendPreset& preset : table._listPreset )
        {
            if ( preset._name == name )
                return &preset;
        }
        return nullptr;
    }
} // namespace sw

// 블렌드 프리셋 표는 상수로 읽는다(서비스 없는 도구에서도) - 쓰는 것만 담는 팩 쿠킹의 뿌리다(`AssetCookRoot.h`).
SW_ASSET_COOK_ROOT( RenderBlendPresets, ::sw::RenderBlendPresetTable::kAssetPath );
