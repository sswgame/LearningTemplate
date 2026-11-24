/**
 * @file EditorListedName.h
 * @brief 코드가 쓸 수 있는 이름을 목록으로 정해 둔 이름 형(`EditorListedName`)입니다 — 아이콘 이름(`EditorIconName`) · 테마 키(`EditorThemeColorKey` · `EditorThemeMetricKey`)가 씁니다.
 */
#pragma once
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

namespace sw::editor
{
    /**
     * @class EditorListedName
     * @brief 목록 @p TList(`kArrName`)에 있는 이름 하나입니다. 이름마다의 매크로(`SW_EDITOR_ICON_NAME` · `SW_THEME_COLOR_KEY` …)만 만든다.
     * @details 목록에 없는 리터럴은 컴파일되지 않는다. 목록이 데이터 표에 모두 있는지는 표를 읽을 때 본다(없으면 에디터가 서지 않는다). 예전에는 소스를 훑는 단위 테스트만 봐서,
     *          그 테스트가 돌지 않은 묶음에서 편집기 플러그인의 없는 이름 하나로 에디터 시나리오 25 개가 졌다. 데이터가 쓰는 이름은 글로 들고 표에서 찾는다(기동 때 대조한다).
     */
    template <typename TList>
    class EditorListedName
    {
    public:
        using List = TList;

        static constexpr uint32 kCount = static_cast<uint32>( SW_COUNT_OF( TList::kArrName ) );

        constexpr EditorListedName() = default;

        /** @brief @p pName 의 번호입니다. 없으면 `kCount` 입니다. */
        static constexpr uint32 findIndex( const utf8* pName )
        {
            for ( uint32 index = 0; index < kCount; ++index )
            {
                const utf8* pListed = TList::kArrName[index];
                uint32      cursor{ 0 };
                while ( pListed[cursor] != 0 && pListed[cursor] == pName[cursor] )
                    ++cursor;
                if ( pListed[cursor] == pName[cursor] )
                    return index;
            }
            return kCount;
        }

        template <uint32 kIndex>
        static constexpr EditorListedName makeAt()
        {
            static_assert( kIndex < kCount, "the name is not in the list of names code may use (TList::kArrName) - add it to the data table and to the list" );
            return EditorListedName{ TList::kArrName[kIndex] };
        }

        /** @brief 이름 글입니다. 비었으면 nullptr 입니다. */
        constexpr const utf8* c_str() const { return _pName; }
        constexpr bool        isEmpty() const { return _pName == nullptr; }

    private:
        constexpr explicit EditorListedName( const utf8* pName )
            : _pName{ pName }
        {
        }

    private:
        const utf8* _pName{ nullptr };
    };
} // namespace sw::editor
