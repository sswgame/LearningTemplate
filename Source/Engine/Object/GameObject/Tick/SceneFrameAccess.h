/**
 * @file SceneFrameAccess.h
 * @brief 프레임 시스템(`ISceneFrameSystem`)이 씬 데이터의 어떤 종류를 읽고 쓰는지 말하는 선언의 형식입니다. 종류는 표(`SceneDataKindList.xxx`)의 줄입니다.
 * @details 지금 프레임 시스템은 붙인 순서대로 직렬로 돕니다. 그 순서가 뜻을 갖는 곳(쓰는 시스템이 먼저, 읽는 시스템이 나중)을 코드로 말하는 것이 이 선언이고,
 *          `GameObjectManager::addFrameSystem` 이 읽는 시스템 뒤에 쓰는 시스템을 붙이는 등록을 거절합니다. 같은 선언이 나중에 시스템을 병렬로 돌릴 때
 *          "서로 겹치는 접근이 없는 것끼리만 같이 돈다" 의 입력이 됩니다(지금 병렬 실행기는 없습니다).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"

#include <initializer_list>

namespace sw
{
    /** @brief 프레임 시스템이 읽고 쓰는 씬 데이터의 종류입니다. 값은 표의 줄 순서입니다. */
    enum class SceneDataKind : uint8
    {
#define SW_SCENE_DATA_KIND( Name ) Name,
#include "Engine/Object/GameObject/Tick/SceneDataKindList.xxx"
#undef SW_SCENE_DATA_KIND
        Count
    };

    /** @brief 종류 이름(표의 식별자)입니다. 범위 밖이면 "Unknown" 입니다. */
    constexpr const utf8* getSceneDataKindName( SceneDataKind kind )
    {
        constexpr const utf8* kArrName[] = {
#define SW_SCENE_DATA_KIND( Name ) #Name,
#include "Engine/Object/GameObject/Tick/SceneDataKindList.xxx"
#undef SW_SCENE_DATA_KIND
        };
        static_assert( sizeof( kArrName ) / sizeof( kArrName[0] ) == static_cast<size_t>( SceneDataKind::Count ), "kind name table must cover every SceneDataKind" );
        const size_t index = static_cast<size_t>( kind );
        return index < static_cast<size_t>( SceneDataKind::Count ) ? kArrName[index] : "Unknown";
    }

    /** @brief `SceneDataKind` 의 집합입니다(비트 하나가 종류 하나). */
    class SceneDataKindSet
    {
    public:
        static_assert( static_cast<uint32>( SceneDataKind::Count ) <= 32, "SceneDataKindSet holds 32 kinds - widen _bits before adding a 33rd line to SceneDataKindList.xxx" );

        constexpr SceneDataKindSet()
            : _bits{ 0 }
        {
        }

        constexpr SceneDataKindSet( std::initializer_list<SceneDataKind> listKind )
            : _bits{ 0 }
        {
            for ( const SceneDataKind kind : listKind )
            {
                _bits |= toBit( kind );
            }
        }

        constexpr bool contains( SceneDataKind kind ) const { return ( _bits & toBit( kind ) ) != 0; }
        constexpr bool isEmpty() const { return _bits == 0; }
        /** @brief 두 집합에 함께 든 종류만 모읍니다. */
        constexpr SceneDataKindSet intersect( const SceneDataKindSet& other ) const
        {
            SceneDataKindSet result;
            result._bits = _bits & other._bits;
            return result;
        }
        /** @brief 집합에 든 값이 가장 작은 종류입니다. 비어 있으면 `Count` 입니다. */
        constexpr SceneDataKind getFirstKind() const
        {
            for ( uint32 index = 0; index < static_cast<uint32>( SceneDataKind::Count ); ++index )
            {
                if ( ( _bits & ( 1u << index ) ) != 0 )
                    return static_cast<SceneDataKind>( index );
            }
            return SceneDataKind::Count;
        }

    private:
        static constexpr uint32 toBit( SceneDataKind kind ) { return 1u << static_cast<uint32>( kind ); }

        uint32 _bits;
    };
} // namespace sw

namespace sw
{
    /**
     * @struct SceneFrameAccess
     * @brief 프레임 시스템 하나가 `runBeforeTick` 에서 하는 일의 접근 선언입니다. 시스템 밖(컴포넌트 틱 · 다른 단계)이 하는 일은 적지 않습니다.
     * @details 세 집합은 이렇게 가릅니다. 어느 집합에도 없는 종류는 그 시스템이 건드리지 않는다는 선언입니다.
     *          - `_writes`: 바꾸는 종류. 만들고 지우는 것(`Structure`)도 쓰기입니다. 확신이 없으면 쓰기로 적습니다(읽기보다 보수적).
     *          - `_reads`: **이번 프레임에 앞서 붙은 시스템이 쓴 값을 본 뒤여야 하는** 읽기. 이 종류를 쓰는 시스템이 자기보다 **뒤에** 붙으면 등록을 거절합니다.
     *          - `_readsPrevious`: 지난 프레임의 값이어도 맞는 읽기(다른 단계가 만든 값을 읽거나, 한 프레임 늦어도 되는 색인 · 위치). 순서를 따지지 않지만
     *            병렬로 돌릴 때는 `_reads` 와 같이 쓰기와 겹치면 같이 돌 수 없습니다.
     *          같은 종류를 쓰는 시스템이 둘 이상이면 붙인 순서가 뜻이므로 거절하지 않습니다(어느 쪽이 나중에 쓰는지가 결과입니다).
     */
    struct SceneFrameAccess
    {
        SceneDataKindSet _writes{};
        SceneDataKindSet _reads{};
        SceneDataKindSet _readsPrevious{};
    };
} // namespace sw
