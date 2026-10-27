/**
 * @file FontCatalog.h
 * @brief 폰트 카탈로그를 정의합니다. 패밀리 이름에서 저장소 폰트 면 파일을 찾고, 시스템 패밀리 이름에서 플랫폼별 파일 이름(OS 폰트 폴더)을 찾습니다.
 * @details 기본 테이블은 `engine/fonts/fontcatalog.xml`(`EngineDefaultAssets::_fontCatalog`)이다. 문화권 테이블(`engine.cultures.xml`)의 `fonts` 패밀리 이름은
 *          이 테이블의 패밀리나 시스템 패밀리에 있어야 쓰인다(없으면 `FontService` 이 처음 한 번 경고하고 건너뛴다).
 *          저장소에는 CC0 라틴 폰트만 둔다. 한글, CJK, 아랍 문자는 시스템 패밀리가 맡는다(런타임 UI 결정 R1).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/Resource/ResourceID.h"
#include "Engine/Text/TextTypes.h"

namespace sw
{
    /**
     * @brief 저장소 폰트 면 하나입니다. 굵기, 기울기, 파일을 가집니다.
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
    /** @brief 저장소 폰트 패밀리 하나입니다. 이름과 면들을 가집니다(언리얼 `FCompositeFont` 의 서체 하나, 유니티 Font Asset 하나에 해당). */
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
     * @brief OS 시스템 폰트 패밀리 하나입니다. 플랫폼마다 일반 면과 굵은 면의 파일 이름을 가지며, 비어 있으면 그 플랫폼에 없다는 뜻입니다.
     * @details 파일은 `SystemFontLocator` 가 OS 폰트 폴더에서 찾는다(리눅스는 하위 폴더까지). 기운 면은 적지 않는다. 기울임은 배치가 기울여 그린다.
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
    /** @brief 폰트 카탈로그 한 벌입니다. */
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
        PROPERTY( Tooltip = "Distance field the glyph atlas holds: Sdf (single channel, cheap) or Msdf (multi channel RGBA, sharp corners on large text; costs more per new glyph and 4x atlas memory)" )
        GlyphFieldKind _glyphField{ GlyphFieldKind::Sdf };

        /** @brief 리소스 경로의 XML 을 읽고 검사합니다. 실패하면(파일 없음, 모르는 키, 검사 실패) 오류를 남기고 false 입니다. */
        [[nodiscard]] bool loadFromResource( const ResourceID& resourceID );
        /** @brief XML 문자열을 읽고 검사합니다(테스트용). */
        [[nodiscard]] bool loadFromXMLText( string_view xmlText );
        /** @brief 패밀리 이름이 비지 않고 겹치지 않는지(저장소와 시스템 합쳐), 저장소 패밀리마다 면이 있고 경로가 비지 않는지, 기본 패밀리가 저장소 패밀리인지 검사합니다. */
        [[nodiscard]] bool validate() const;

        /** @brief 이름의 저장소 가족입니다(대소문자 무시). 없으면 nullptr. */
        const FontFamilyDesc* findFamily( string_view name ) const;
        /** @brief 이름의 시스템 가족입니다(대소문자 무시). 없으면 nullptr. */
        const SystemFontFamilyDesc* findSystemFamily( string_view name ) const;
    };
} // namespace sw
