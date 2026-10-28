#include "pch.h"

#include "Editor/Common/GUI/EditorComponentIcon.h"

#include "Core/Container/StringUtil.h"
#include "Core/Container/unordered_map.h"
#include "Core/Log/LogOnce.h"

#include "Editor/Common/GUI/EditorIconGlyphs.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/EditorStateStore.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Reflection/ReflectionTypes.h"
#include "Engine/Reflection/TypeRegistry.h"

namespace sw::editor
{
    namespace
    {
        struct EditorComponentIconInternal
        {
            static constexpr Color kNeutral{ 0.85f, 0.87f, 0.90f, 1.0f };

            /** @brief `IconColor` 메타 이름 → 색입니다. */
            struct NamedColor
            {
                const utf8* _pName;
                Color       _color;
            };

            static constexpr NamedColor kArrNamedColor[] = {
                {      "Light", { 1.00f, 0.83f, 0.30f, 1.0f }},
                {     "Camera", { 0.45f, 0.70f, 1.00f, 1.0f }},
                {      "Audio", { 0.45f, 0.90f, 0.60f, 1.0f }},
                {    "Physics", { 1.00f, 0.60f, 0.30f, 1.0f }},
                { "Navigation", { 0.35f, 0.85f, 0.85f, 1.0f }},
                {"Environment", { 0.55f, 0.80f, 0.95f, 1.0f }},
            };

            /** @brief 리플렉션 Category 테이블 — `Icon` 메타가 없는 컴포넌트(키트 · 게임)가 여기서 맞는다. */
            struct CategoryRow
            {
                const utf8* _pCategory;
                const utf8* _pGlyph;
                const utf8* _pColorName;
            };

            static constexpr CategoryRow kArrCategoryRow[] = {
                {      "Camera",     editoricon::kCamera,      "Camera"},
                {       "Audio",      editoricon::kAudio,       "Audio"},
                {     "Physics",    editoricon::kPhysics,     "Physics"},
                {  "Physics 2D",    editoricon::kPhysics,     "Physics"},
                {  "Navigation", editoricon::kNavigation,  "Navigation"},
                {   "Character",  editoricon::kCharacter,            ""},
                {   "Animation",  editoricon::kAnimation,            ""},
                {"Animation 2D",  editoricon::kAnimation,            ""},
                {"Animation 3D",  editoricon::kAnimation,            ""},
                {"Rendering 3D",       editoricon::kCube,            ""},
                {"Rendering 2D",     editoricon::kSprite,            ""},
                {          "UI",     editoricon::kWidget,            ""},
                {      "Layout",     editoricon::kWidget,            ""},
                {     "Control", editoricon::kController,            ""},
                { "Environment",      editoricon::kGlobe, "Environment"},
                {     "Vehicle",    editoricon::kVehicle,            ""},
                { "Interaction",    editoricon::kTrigger,            ""},
                {     "Gimmick",    editoricon::kTrigger,            ""},
                {  "Cinematics",   editoricon::kSequence,            ""},
                {    "Gameplay",    editoricon::kAbility,            ""},
            };

            static constexpr EditorComponentIconRow kDefaultRow{ editoricon::kComponent, kNeutral, false };
            static constexpr EditorComponentIconRow kObjectRow{ editoricon::kGameObject, kNeutral, false };

            /** @brief 타입 → 줄 캐시입니다. 뷰포트 빌보드가 프레임마다 모든 컴포넌트를 묻는다(오브젝트 8000 개면 메타 조회가 수백만 번). 타입 테이블 세대가 바뀌면 비운다. */
            struct RowCache
            {
                unordered_map<const TypeInfo*, EditorComponentIconRow> _mapRow;
                uint32                                                 _generation{ invalid_index::kUint32 };
            };

            static Color findColor( string_view colorName )
            {
                for ( const NamedColor& namedColor : kArrNamedColor )
                {
                    if ( colorName == namedColor._pName )
                        return namedColor._color;
                }
                return kNeutral;
            }

            static EditorComponentIconRow findRowUncached( const TypeInfo* pType )
            {
                for ( const TypeInfo* pCurrent = pType; pCurrent != nullptr; pCurrent = pCurrent->getParentType() )
                {
                    const string* pIcon = pCurrent->findCustomMeta( "Icon" );
                    if ( pIcon == nullptr )
                        continue;
                    const utf8* pGlyph = EditorComponentIcon::findGlyph( *pIcon );
                    if ( pGlyph == nullptr )
                        continue; // 모르는 이름은 테스트가 막는다 — 여기서는 부모 · Category 로 넘어간다
                    const string* pColor     = pCurrent->findCustomMeta( "IconColor" );
                    const bool    bBillboard = pCurrent->findCustomMeta( "Billboard" ) != nullptr;
                    const Color   iconColor  = pColor != nullptr ? findColor( *pColor ) : kNeutral;
                    return EditorComponentIconRow{ pGlyph, iconColor, bBillboard };
                }
                if ( pType != nullptr && pType->getCategory().empty() == false )
                {
                    for ( const CategoryRow& row : kArrCategoryRow )
                    {
                        if ( pType->getCategory() == row._pCategory )
                            return EditorComponentIconRow{ row._pGlyph, findColor( row._pColorName ), false };
                    }
                }
                return kDefaultRow;
            }
        };
    } // namespace

    EditorComponentIconRow EditorComponentIcon::findRow( const TypeInfo* pType )
    {
        if ( pType == nullptr )
            return EditorComponentIconInternal::findRowUncached( pType );
        // 모듈 언로드 · 리로드면 같은 주소에 다른 타입이 올 수 있다 — 타입 테이블 세대가 바뀌면 캐시를 비운다.
        EditorComponentIconInternal::RowCache& cache      = editor::getSubsystem<EditorStateStore>().get<EditorComponentIconInternal::RowCache>();
        const uint32                           generation = editor::getSubsystem<TypeRegistry>().getGeneration();
        if ( cache._generation != generation )
        {
            cache._mapRow.clear();
            cache._generation = generation;
        }
        const auto iter = cache._mapRow.find( pType );
        if ( iter != cache._mapRow.end() )
            return iter->second;
        const EditorComponentIconRow row = EditorComponentIconInternal::findRowUncached( pType );
        cache._mapRow.emplace( pType, row );
        return row;
    }

    EditorComponentIconRow EditorComponentIcon::findObjectRow( const GameObject& object )
    {
        for ( const Component* pComponent : object.getComponents() )
        {
            if ( pComponent == nullptr )
                continue;
            const EditorComponentIconRow row = findRow( pComponent->getTypeInfo() );
            if ( row._bBillboard )
                return row;
        }
        return EditorComponentIconInternal::kObjectRow;
    }

    const utf8* EditorComponentIcon::findGlyph( string_view iconName )
    {
        for ( const editoricon::NamedIcon& namedIcon : editoricon::kArrNamedIcon )
        {
            if ( iconName == namedIcon._pName )
                return namedIcon._pGlyph;
        }
        return nullptr;
    }

    bool EditorComponentIcon::isKnownColorName( string_view colorName )
    {
        for ( const EditorComponentIconInternal::NamedColor& namedColor : EditorComponentIconInternal::kArrNamedColor )
        {
            if ( colorName == namedColor._pName )
                return true;
        }
        return false;
    }

    uint32 EditorComponentIcon::decodeGlyph( const utf8* pGlyph )
    {
        if ( pGlyph == nullptr )
            return 0;
        constexpr uint32 kMaxBasicPlaneCodepoint = 0xFFFF; // 아이콘 글리프 범위(`kArrGlyphRange`)는 기본 다국어 평면이다
        size_t           offset                  = 0;
        uint32           codepoint               = 0;
        if ( StringUtil::tryDecodeUTF8( string_view( pGlyph ), offset, codepoint ) == false || codepoint > kMaxBasicPlaneCodepoint )
            return 0;
        return codepoint;
    }
} // namespace sw::editor
