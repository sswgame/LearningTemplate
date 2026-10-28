/**
 * @file MeshModifierRegistry.h
 * @brief 메시 모디파이어 종류의 등록 목록과 등록 매크로 `SW_MESH_MODIFIER` 를 둡니다.
 * @details 모디파이어 .cpp 마다 `SW_MESH_MODIFIER( 클래스, "id", "표시 이름", "카테고리" );` 한 줄이 정적 등록 객체를 만든다.
 *          등록 객체는 모듈이 내려갈 때 소멸하며 목록에서 빠진다(`EditorExtensionRegistrar` 와 같은 수명). 목록은 이 소스를 컴파일한
 *          바이너리마다 하나다 — 확장 DLL 과, 같은 소스를 직접 컴파일하는 EngineTest 가 각자 가진다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Memory/Memory.h"

#include "GameFramework/Plugin/Editor/MeshModifiers/Stack/MeshModifier.h"

namespace sw::editor
{
    /** @brief 모디파이어 하나를 만드는 함수입니다. */
    using MeshModifierCreateFn = unique_ptr<MeshModifier> ( * )();

    /**
     * @struct MeshModifierRegistration
     * @brief 모디파이어 종류 하나의 등록 항목입니다. 만들면 목록에 오르고, 소멸하면 빠집니다.
     */
    struct MeshModifierRegistration
    {
        MeshModifierRegistration( const utf8* pID, const utf8* pDisplayName, const utf8* pCategory, MeshModifierCreateFn pCreateFn );
        ~MeshModifierRegistration();
        MeshModifierRegistration( const MeshModifierRegistration& )            = delete;
        MeshModifierRegistration& operator=( const MeshModifierRegistration& ) = delete;

        const utf8*          _pID;          ///< 저장 문서의 종류 키입니다(바꾸면 옛 문서를 못 읽는다)
        const utf8*          _pDisplayName; ///< 패널 추가 메뉴의 이름(영문)
        const utf8*          _pCategory;    ///< 패널 추가 메뉴의 묶음(영문)
        MeshModifierCreateFn _pCreateFn;
    };
} // namespace sw::editor

namespace sw::editor
{
    /** @brief 등록 목록을 읽고 모디파이어를 만드는 도우미입니다. */
    struct MeshModifierRegistry
    {
        /** @brief 지금 올라 있는 등록을 (카테고리, id) 순으로 채웁니다. */
        static void getRegistrations( vector<const MeshModifierRegistration*>& outListRegistration );
        /** @brief id 의 등록을 찾습니다. 없으면 nullptr 입니다. */
        static const MeshModifierRegistration* findRegistration( string_view id );
        /** @brief id 로 기본값 모디파이어를 만들고 id · 이름(표시 이름)을 채웁니다. 모르는 id 면 nullptr 입니다. */
        static unique_ptr<MeshModifier> create( string_view id );

        /** @brief 등록 객체가 부릅니다. 같은 id 가 이미 있으면 경고하고 먼저 것을 둡니다. */
        static void add( MeshModifierRegistration* pRegistration );
        /** @brief 등록 객체가 소멸하며 부릅니다. */
        static void remove( const MeshModifierRegistration* pRegistration );
    };
} // namespace sw::editor

namespace sw::editor
{
    /** @brief `SW_MESH_MODIFIER` 가 쓰는 생성 함수입니다. */
    template <typename TModifier>
    unique_ptr<MeshModifier> createMeshModifier()
    {
        return make_unique<TModifier>();
    }
} // namespace sw::editor

/**
 * @brief 모디파이어 종류 하나를 등록합니다. 모디파이어 .cpp 의 `namespace sw::editor` 안, 파일 끝에 한 줄로 둡니다.
 * @param TModifier `MeshModifier` 파생 클래스(기본 생성자 필요)
 * @param pID 저장 문서 키(영문 소문자 · 숫자 · `_`, 전체에서 유일)
 * @param pDisplayName 패널 표시 이름
 * @param pCategory 패널 추가 메뉴 묶음
 */
#define SW_MESH_MODIFIER( TModifier, pID, pDisplayName, pCategory )                             \
    static const ::sw::editor::MeshModifierRegistration sw_meshModifierRegistration_##TModifier \
    {                                                                                           \
        pID, pDisplayName, pCategory, &::sw::editor::createMeshModifier<TModifier>              \
    }
