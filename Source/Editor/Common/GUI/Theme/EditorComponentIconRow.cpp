#include "pch.h"

#include "Editor/Common/GUI/Theme/EditorComponentIconRow.h"

#include "Core/Container/unordered_map.h"
#include "Core/String/StringUtil.h"

#include "Editor/Common/Config/EditorComponentIconTable.h"
#include "Editor/Common/Config/EditorIconGlyphTable.h"
#include "Editor/Common/GUI/Theme/EditorThemeUtil.h"
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
            /** @brief 타입 하나의 아이콘 이름 · 색 키입니다. 글리프 · 색 값은 담지 않는다 — 글꼴을 다시 세우면 표를 다시 읽고, 테마를 바꾸면 색이 바뀐다. */
            struct NamedRow
            {
                hashed_string _iconName;
                hashed_string _colorKey;
                bool          _bBillboard{ false }; ///< NOLINT(sw-bool-flag): 집합체다(중괄호 · 지정 초기화로 만든다) — C++17 비트필드는 기본값을 둘 수 없어 `T x;` 가 값을 남기지 않는다
            };

            /** @brief 타입 → 줄 캐시입니다. 뷰포트 빌보드가 프레임마다 모든 컴포넌트를 묻는다(오브젝트 8000 개면 메타 조회가 수백만 번). 타입 테이블 세대가 바뀌면 비운다. */
            struct RowCache
            {
                unordered_map<const TypeInfo*, NamedRow> _mapRow;
                NamedRow                                 _objectRow;           ///< 빌보드 컴포넌트가 없는 오브젝트 줄 — 계층 패널이 줄마다 묻는다(색 키를 줄마다 이어 붙이지 않게)
                bool                                     _bObjectRow{ false }; ///< NOLINT(sw-bool-flag): 집합체다(중괄호 · 지정 초기화로 만든다) — C++17 비트필드는 기본값을 둘 수 없어 `T x;` 가 값을 남기지 않는다
                uint32                                   _generation{ constant::invalid_index::kUint32 };
            };

            static const EditorComponentIconTable& table() { return EditorIconGlyphTable::getComponentIconTable(); }

            static NamedRow makeRow( string_view iconName, string_view colorName, bool bBillboard )
            {
                return NamedRow{ hashed_string{ iconName }, hashed_string{ string_view{ EditorComponentIconTable::makeColorKey( colorName ) } }, bBillboard };
            }

            static NamedRow findRowUncached( const TypeInfo* pType )
            {
                const EditorComponentIconTable& iconTable = table();
                for ( const TypeInfo* pCurrent : pType != nullptr ? pType->getTypeChain() : TypeChain{} )
                {
                    const string* pIcon = pCurrent->findCustomMeta( "Icon" );
                    if ( pIcon == nullptr )
                        continue;
                    const string* pColor = pCurrent->findCustomMeta( "IconColor" );
                    return makeRow( *pIcon, pColor != nullptr ? string_view{ *pColor } : string_view{ iconTable._neutralColor }, pCurrent->findCustomMeta( "Billboard" ) != nullptr );
                }
                if ( pType != nullptr && pType->getCategory().empty() == false )
                {
                    if ( const EditorComponentCategoryIcon* pRow = iconTable.findCategory( pType->getCategory() ) )
                        return makeRow( pRow->_icon, pRow->_color, false );
                }
                return makeRow( iconTable._defaultIcon, iconTable._neutralColor, false );
            }

            /** @brief 지금 타입 테이블 세대의 캐시입니다. 모듈 언로드 · 리로드면 같은 주소에 다른 타입이 올 수 있다 — 세대가 바뀌면 비운다. */
            static RowCache& validCache()
            {
                RowCache&    cache      = editor::getSubsystem<EditorStateStore>().get<RowCache>();
                const uint32 generation = editor::getSubsystem<TypeRegistry>().getGeneration();
                if ( cache._generation != generation )
                {
                    cache._mapRow.clear();
                    cache._bObjectRow = false;
                    cache._generation = generation;
                }
                return cache;
            }

            static EditorComponentIconRow resolve( const NamedRow& row )
            {
                return EditorComponentIconRow{ EditorIconGlyphTable::findPublishedGlyph( row._iconName ), EditorThemeUtil::findColor( row._colorKey ), row._bBillboard };
            }
        };
    } // namespace

    EditorComponentIconRow EditorComponentIconRow::findRow( const TypeInfo* pType )
    {
        if ( pType == nullptr )
            return EditorComponentIconInternal::resolve( EditorComponentIconInternal::findRowUncached( pType ) );
        EditorComponentIconInternal::RowCache& cache = EditorComponentIconInternal::validCache();
        auto                                   iter  = cache._mapRow.find( pType );
        if ( iter == cache._mapRow.end() )
            iter = cache._mapRow.emplace( pType, EditorComponentIconInternal::findRowUncached( pType ) ).first;
        return EditorComponentIconInternal::resolve( iter->second );
    }

    EditorComponentIconRow EditorComponentIconRow::findObjectRow( const GameObject& object )
    {
        for ( const Component* pComponent : object.getComponents() )
        {
            if ( pComponent == nullptr )
                continue;
            const EditorComponentIconRow row = findRow( pComponent->getTypeInfo() );
            if ( row._bBillboard )
                return row;
        }
        EditorComponentIconInternal::RowCache& cache = EditorComponentIconInternal::validCache();
        if ( cache._bObjectRow == false )
        {
            const EditorComponentIconTable& iconTable = EditorComponentIconInternal::table();
            cache._objectRow                          = EditorComponentIconInternal::makeRow( iconTable._objectIcon, iconTable._neutralColor, false );
            cache._bObjectRow                         = true;
        }
        return EditorComponentIconInternal::resolve( cache._objectRow );
    }

    uint32 EditorComponentIconRow::decodeGlyph( const utf8* pGlyph )
    {
        if ( pGlyph == nullptr )
            return 0;
        constexpr uint32 kMaxBasicPlaneCodepoint = 0xFFFF; // 아이콘 표의 코드포인트는 기본 다국어 평면이다(`EditorIconGlyphTable::parseCodepoint`)
        size_t           offset                  = 0;
        uint32           codepoint               = 0;
        if ( StringUtil::tryDecodeUTF8( string_view( pGlyph ), offset, codepoint ) == false || codepoint > kMaxBasicPlaneCodepoint )
            return 0;
        return codepoint;
    }
} // namespace sw::editor
