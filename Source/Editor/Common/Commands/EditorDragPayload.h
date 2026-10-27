/**
 * @file EditorDragPayload.h
 * @brief 에디터 끌어 놓기 페이로드(`{종류, id}`)와 받는 쪽의 종류 집합입니다.
 * @details 지금 페이로드가 세 형식(에셋 경로 문자열 · 오브젝트 uint64 · 위젯 타입 이름)이고 받는 쪽이 이름 문자열을 하나씩 비교한다(EditorUX 10-2).
 *          형식을 하나로 두고 받는 쪽이 종류 집합을 넘기면 집합 밖 종류는 어댑터(`Common/Input`)가 거절한다. ImGui 를 쓰지 않는다.
 *          언리얼의 타입 있는 `FDragDropOperation` 과 같은 선이다(기억).
 */
#pragma once
#include "Core/Common/StdHeaders.h"
#include "Core/Common/Types.h"

namespace sw::editor
{
    /** @brief 끌어 놓기로 옮기는 것의 종류입니다. */
    enum class EditorPayloadKind : uint8
    {
        Asset = 0,  ///< 에셋(id 는 에셋 id)
        GameObject, ///< 씬 오브젝트(id 는 게임 오브젝트 id)
        WidgetType, ///< UI 위젯 타입(id 는 타입 번호)
        Count
    };
    static_assert( static_cast<uint32>( EditorPayloadKind::Count ) <= 32, "EditorPayloadKindSet 은 uint32 비트 하나에 종류 하나를 담는다" );
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @struct EditorDragPayload
     * @brief 끄는 것 하나입니다. 문자열 · 포인터를 담지 않는 POD 라 ImGui 페이로드 버퍼에 그대로 복사된다.
     */
    struct EditorDragPayload
    {
        EditorPayloadKind _kind{ EditorPayloadKind::Asset };
        uint64            _id{ 0 };
    };
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @struct EditorPayloadKindSet
     * @brief 받는 쪽이 받을 페이로드 종류의 집합입니다. 집합 밖 종류는 놓을 자리로 보이지도 않는다.
     */
    struct EditorPayloadKindSet
    {
        uint32 _bits{ 0 };

        constexpr EditorPayloadKindSet() = default;
        constexpr EditorPayloadKindSet( std::initializer_list<EditorPayloadKind> listKind )
        {
            for ( const EditorPayloadKind kind : listKind )
            {
                _bits |= makeBit( kind );
            }
        }
        /** @brief @p kind 를 받으면 true 입니다. */
        constexpr bool contains( EditorPayloadKind kind ) const { return ( _bits & makeBit( kind ) ) != 0; }
        /** @brief 받는 종류가 하나도 없으면 true 입니다. */
        constexpr bool isEmpty() const { return _bits == 0; }

    private:
        static constexpr uint32 makeBit( EditorPayloadKind kind ) { return 1u << static_cast<uint32>( kind ); }
    };
} // namespace sw::editor
