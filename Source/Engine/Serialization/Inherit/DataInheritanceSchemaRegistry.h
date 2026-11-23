/**
 * @file DataInheritanceSchemaRegistry.h
 * @brief 손 XML 형식이 부모를 가리킬 수 있게 루트 이름마다 규칙(합치기 스키마 또는 합치기 함수)을 올리는 정적 등록입니다. 팩 쿠커 · 읽기가 루트 이름으로 찾습니다.
 * @details 팩 쿠킹은 게임 모듈을 올리기 전에 돌아 모듈의 정적 등록을 보지 못하므로 Engine 만 쓴다. 게임 · 키트 형식은 `Content/engine/settings/datacataloginheritance.xml` 의 `<Document>` 줄에 적는다.
 */
#pragma once
#include "Core/Foundation/Macros.h"
#include "Core/Registry/NamedEntryRegistry.h"

#include "Engine/Serialization/Inherit/DataInheritance.h"

namespace sw
{
    /** @brief 루트 이름 하나의 등록 줄입니다. 스키마 함수와 합치기 함수 가운데 하나만 있다(한 루트에 두 규칙은 없다 — 이름이 겹치면 거절한다). */
    struct DataInheritanceSchemaEntry
    {
        static constexpr const utf8* kEntryKind = "Data inheritance schema";

        const utf8*                         _pRootName;
        DataInheritance::MakeSchemaFunction _pfnMakeSchema;
        DataInheritance::MergeFunction      _pfnMerge;

        const utf8* getEntryName() const { return _pRootName; }
        /** @brief 등록 목록입니다(Engine 이미지의 함수 정적 — 다른 TU 의 정적 등록자가 먼저 와도 서 있다). */
        SW_API static LockedRegistrationList<const DataInheritanceSchemaEntry>& getEntries();
    };

    /** @brief 루트 이름 → 규칙 표입니다(철자 그대로). */
    using DataInheritanceSchemaRegistry = NamedEntryRegistry<DataInheritanceSchemaEntry>;

    /** @brief 아래 두 매크로가 두는 정적 등록자입니다. */
    SW_DEFINE_NAMED_ENTRY_REGISTRAR( DataInheritanceSchemaRegistrar, DataInheritanceSchemaEntry );
} // namespace sw

/** @brief 루트 이름 @p RootName 의 손 XML 형식이 부모를 가리킬 수 있게 올립니다. 팩 쿠커가 이 규칙으로 펼칩니다. */
#define SW_DATA_INHERITANCE_SCHEMA( RootName, pfnMakeSchema )                                                                                                                           \
    static_assert( ::sw::string_view( SW_REGISTRAR_MODULE ) == ::sw::string_view( "Engine" ), "the pack cook cannot see a module's registrar - use a Document row of the rule table" ); \
    SW_STATIC_REGISTRATION( dataInheritanceSchema, ::sw::DataInheritanceSchemaEntry, ::sw::DataInheritanceSchemaRegistrar, RootName, #RootName, pfnMakeSchema, nullptr )

/**
 * @brief 칸 규칙으로 풀리지 않는 Engine 형식의 합치기 함수를 올립니다. 읽기(`ResourceContext::forMerge`)와 팩 쿠커가 같은 함수로 부모 없는 한 벌을 만듭니다.
 * @details 예외 길이 아니다 — 결과는 다른 형식과 똑같이 펼친 글이고, 런타임은 그 글만 읽는다. 게임 · 키트 형식은 쓸 수 없다(`SW_DATA_INHERITANCE_SCHEMA` 와 같은 까닭).
 */
#define SW_DATA_INHERITANCE_MERGE( RootName, pfnMerge )                                                                                          \
    static_assert( ::sw::string_view( SW_REGISTRAR_MODULE ) == ::sw::string_view( "Engine" ), "the pack cook cannot see a module's registrar" ); \
    SW_STATIC_REGISTRATION( dataInheritanceMerge, ::sw::DataInheritanceSchemaEntry, ::sw::DataInheritanceSchemaRegistrar, RootName, #RootName, nullptr, pfnMerge )
