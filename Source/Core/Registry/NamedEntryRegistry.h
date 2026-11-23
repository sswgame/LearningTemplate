/**
 * @file NamedEntryRegistry.h
 * @brief 이름 → 정적 등록 줄 표의 공통 함수 `NamedEntryRegistry<TEntry>` 입니다(동작 · 조건 · 지역 모듈 종류).
 * @details 저장소(잠금을 든 정적 목록)는 항목 형식의 이미지가 하나 정의하고(`TEntry::getEntries`), 올리기 · 내리기 · 찾기 · 이름 목록 · 세대는 여기 한 벌이다 — 표마다 손으로 쓰던
 *          등록 함수 묶음과 손 등록자(등록 자리를 남기지 않아 겹친 이름이 시작 검사를 지나갔다)를 대신한다.
 *          항목 형식이 갖출 것 셋: `static LockedRegistrationList<const TEntry>& getEntries()`(내보낸 함수), `kEntryKind`(거절 로그의 영문 이름), `getEntryName()`.
 */
#pragma once
#include "Core/Foundation/Macros.h"
#include "Core/Registry/RegistrationList.h"

namespace sw
{
    /** @struct NamedEntryRegistry @brief 파일 머리말 참고. 목록이 자기 잠금을 들어 등록(모듈을 올리는 스레드)과 찾기(데이터를 읽는 스레드)가 겹쳐도 된다. */
    template <typename TEntry>
    struct NamedEntryRegistry
    {
        /** @brief 올립니다. 이름이 비었거나 겹치면 공용 오류 로그를 남기고 false 입니다(먼저 것을 둔다). */
        [[nodiscard]] static bool registerEntry( const TEntry* pEntry )
        {
            return TEntry::getEntries().addLogged( pEntry, TEntry::kEntryKind, pEntry->getEntryName() ) == RegistrationResult::Added;
        }
        static void unregisterEntry( const TEntry* pEntry )
        {
            (void)TEntry::getEntries().remove( pEntry ); // 올라 있지 않은 줄을 빼는 것은 할 일이 없다
        }

        /** @brief 이름의 등록 줄입니다. 없으면 nullptr 입니다(대소문자 규칙은 목록이 정한다). */
        static const TEntry* find( string_view name ) { return TEntry::getEntries().findByName( name ); }
        /** @brief @p pEntry 가 지금 등록되어 있는지입니다(잠금 · 훑기 — 세대가 바뀐 뒤에만 부른다). */
        static bool isRegistered( const TEntry* pEntry ) { return TEntry::getEntries().contains( pEntry ); }
        /**
         * @brief 등록 줄이 빠질 때마다 오르는 세대입니다(모듈을 내리거나 바꿔 올릴 때). 컴파일한 호출은 읽을 때의 값을 들고, 다르면 부르기 전에 줄이 아직 있는지 본다.
         * @details 배포 빌드는 등록을 빼지 않아 늘 0 이다 — 부르기는 원자 읽기 하나로 끝난다.
         */
        static uint32 getRemovalGeneration() { return TEntry::getEntries().getRemovalGeneration(); }
        /** @brief 등록된 이름을 등록 순서대로 뒤에 붙입니다(편집기의 고르기 목록). */
        static void collectNames( vector<string>& outListName ) { TEntry::getEntries().collectNames( outListName ); }
        /** @brief 등록된 모든 이름을 ", " 로 이은 글입니다(모르는 이름 오류에 보인다). */
        static string joinNames() { return TEntry::getEntries().joinNames(); }
    };
} // namespace sw

/** @brief `NamedEntryRegistry<EntryType>` 에 올리는 정적 등록자 형식 @p RegistrarType 을 정의합니다(헤더, `namespace sw` 안). */
#define SW_DEFINE_NAMED_ENTRY_REGISTRAR( RegistrarType, EntryType ) \
    SW_DEFINE_STATIC_REGISTRAR( RegistrarType, const EntryType*, &::sw::NamedEntryRegistry<EntryType>::registerEntry, &::sw::NamedEntryRegistry<EntryType>::unregisterEntry )
