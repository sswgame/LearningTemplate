/**
 * @file UIResolutionPresetTable.h
 * @brief 화면 해상도 견본 표(`engine/settings/uiresolutions.xml`)입니다. 에디터 UI 미리보기가 견본 목록으로 씁니다.
 * @details 유니티 Game 뷰의 해상도 목록 · Device Simulator, UMG 디자이너의 화면 크기 목록과 같은 자리입니다. 코드에 내장 견본이 없습니다 —
 *          표를 읽지 못하면 오류이고 목록이 빕니다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"

#include "Engine/Asset/AssetID.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    /** @brief 해상도 견본 하나입니다(물리 픽셀). */
    REFLECT()
    struct SW_API UIResolutionPreset
    {
        REFLECT_BODY();

        PROPERTY( Meta = "NotLocalizable" )
        string _name{}; ///< 목록에 보이는 이름(에디터 UI 미리보기의 개발자 목록이라 번역하지 않는다)

        PROPERTY( Min = 1 )
        uint32 _width{};

        PROPERTY( Min = 1 )
        uint32 _height{};
    };
} // namespace sw

namespace sw
{
    /** @brief 해상도 견본 표입니다. 첫 줄이 목록을 처음 열 때의 견본입니다. */
    REFLECT()
    struct SW_API UIResolutionPresetTable
    {
        REFLECT_BODY();

        PROPERTY()
        vector<UIResolutionPreset> _listResolution;

        /**
         * @brief @p path 의 표를 읽습니다. 모르는 이름 · 범위 밖 값 · 빈 표면 오류를 남기고 false 이며 표가 빕니다.
         * @details 엔진 팩의 표 경로는 엔진 기본 에셋 표(`EngineDefaultAssets::_uiResolutionPresets`)가 기준이다 — 코드에 경로를 두지 않는다.
         */
        [[nodiscard]] bool loadFromAsset( const AssetID& assetID );
    };
} // namespace sw
