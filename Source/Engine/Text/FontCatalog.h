/**
 * @file FontCatalog.h
 * @brief 글꼴 카탈로그입니다 — 가족 이름 → 면 파일(저장소 글꼴)과, 시스템 가족 이름 → 플랫폼별 파일 이름(OS 글꼴 폴더)입니다.
 * @details 기본 표는 `engine/fonts/fontcatalog.xml`(`EngineDefaultAssets::_fontCatalog`)입니다. 문화권 표(`engine.cultures.json`)의 `fonts` 가족 이름은
 *          이 표의 가족 · 시스템 가족에 있어야 쓰입니다(없으면 `FontSystem` 이 처음 한 번 경고하고 건너뛴다).
 *          저장소에는 CC0 라틴 글꼴만 둡니다 — 한글 · CJK · 아랍 문자는 시스템 가족이 맡습니다(런타임 UI 결정 R1).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/Text/TextTypes.h"

namespace sw
{
    /**
     * @brief 저장소 글꼴 면 하나 — 굵기 · 기울기 · 파일입니다.
     * @code
     *     <FontFaceDesc _weight="Regular" _slant="Upright" _path="engine/fonts/kenney_future.ttf" />
     * @endcode
     */
    REFLECT()
    struct SW_API FontFaceDesc
    {
        REFLECT_BODY();
        PROPERTY( Tooltip = "Weight this face draws" )
        FontWeight _weight{ FontWeight::Regular };
        PROPERTY( Tooltip = "Slant this face draws" )
        FontSlant _slant{ FontSlant::Upright };
        PROPERTY( Tooltip = "Resource path of the font file (.ttf, .otf, .ttc)" )
        string _path{};
        PROPERTY( Min = 0, Tooltip = "Face index inside a collection file (.ttc); 0 otherwise" )
        uint32 _faceIndex{ 0 };
    };
} // namespace sw

namespace sw
{
    /** @brief 저장소 글꼴 가족 하나 — 이름과 면들입니다(언리얼 `FCompositeFont` 의 서체 하나 · 유니티 Font Asset 하나). */
    REFLECT()
    struct SW_API FontFamilyDesc
    {
        REFLECT_BODY();
        PROPERTY( Tooltip = "Family name text styles and the culture table pick it by" )
        string _name{};
        PROPERTY( Tooltip = "Faces of this family (weights and slants)" )
        vector<FontFaceDesc> _listFace;
    };
} // namespace sw

namespace sw
{
    /**
     * @brief OS 시스템 글꼴 가족 하나 — 플랫폼마다 일반 · 굵은 면의 파일 이름입니다. 빈 칸은 그 플랫폼에 없음입니다.
     * @details 파일은 `SystemFontLocator` 가 OS 글꼴 폴더에서 찾습니다(리눅스는 하위 폴더까지). 기운 면은 적지 않습니다 — 기울임은 배치가 기울여 그린다.
     */
    REFLECT()
    struct SW_API SystemFontFamilyDesc
    {
        REFLECT_BODY();
        PROPERTY( Tooltip = "Family name the culture table lists" )
        string _name{};
        PROPERTY( Tooltip = "Regular face file name in the Windows font folder" )
        string _windowsRegular{};
        PROPERTY( Tooltip = "Bold face file name in the Windows font folder (empty = draw the regular face bold)" )
        string _windowsBold{};
        PROPERTY( Tooltip = "Regular face file name under the Linux font folders" )
        string _linuxRegular{};
        PROPERTY( Tooltip = "Bold face file name under the Linux font folders (empty = draw the regular face bold)" )
        string _linuxBold{};
        PROPERTY( Min = 0, Tooltip = "Face index inside a Windows collection file (.ttc)" )
        uint32 _windowsFaceIndex{ 0 };
        PROPERTY( Min = 0, Tooltip = "Face index inside a Linux collection file (.ttc)" )
        uint32 _linuxFaceIndex{ 0 };
    };
} // namespace sw

namespace sw
{
    /** @brief 글꼴 카탈로그 한 장입니다. */
    REFLECT()
    struct SW_API FontCatalogDesc
    {
        REFLECT_BODY();
        PROPERTY( Tooltip = "Family used when a style names none and at the end of every fallback chain" )
        string _defaultFamily{};
        PROPERTY( Tooltip = "Font families shipped in the resource packs" )
        vector<FontFamilyDesc> _listFamily;
        PROPERTY( Tooltip = "Families looked up in the operating system font folders" )
        vector<SystemFontFamilyDesc> _listSystemFamily;

        /** @brief 리소스 경로의 XML 을 읽고 검사합니다. 실패하면(파일 없음 · 모르는 키 · 검사 실패) 오류를 남기고 false 입니다. */
        [[nodiscard]] bool loadFromResource( string_view resourcePath );
        /** @brief XML 문자열을 읽고 검사합니다(시험용). */
        [[nodiscard]] bool loadFromXMLText( string_view xmlText );
        /** @brief 가족 이름이 비지 않고 겹치지 않고(저장소 · 시스템 합쳐), 저장소 가족마다 면이 있고 경로가 비지 않고, 기본 가족이 저장소 가족인지 봅니다. */
        [[nodiscard]] bool validate() const;

        /** @brief 이름의 저장소 가족입니다(대소문자 무시). 없으면 nullptr. */
        const FontFamilyDesc* findFamily( string_view name ) const;
        /** @brief 이름의 시스템 가족입니다(대소문자 무시). 없으면 nullptr. */
        const SystemFontFamilyDesc* findSystemFamily( string_view name ) const;
    };
} // namespace sw
