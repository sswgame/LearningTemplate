/**
 * @file CookReflectionFingerprint.h
 * @brief 리플렉션 표의 지문입니다 — 씬 · 프리로드 목록 · 프리팹 · 내비메시처럼 리플렉션으로 읽거나 쓰는 쿠킹 작업의 설정 해시(`CookJobDesc::_settingsHash`)입니다.
 */
#pragma once
#include "Core/Foundation/Types.h"

namespace sw
{
    class TypeRegistry;

    /** @struct CookReflectionFingerprint @brief 파일 머리말 참고. */
    struct CookReflectionFingerprint
    {
        /**
         * @brief @p registry 의 타입 · 프로퍼티 · 열거자와 타입마다의 기본 인스턴스 값의 지문입니다. 이름순으로 늘어놓아 등록 순서(링크 순서)와 관계없다.
         * @details 쿠킹 결과를 정하는 표의 칸을 모두 든다 — 배치 해시(`TypeInfo::getSchemaLayoutHash`: 저작 칸의 이름 · 형식 · 상속), 옛 이름(읽기), 선언 기본값, 필수 · 열쇠(프리팹 합치기),
         *          에셋 경로 표시(프리로드 목록). 빠진 칸이 있으면 그 칸만 바꾼 빌드가 옛 쿠킹본을 다시 쓴다.
         *          파일에 없는 칸은 C++ 생성자 · 게임 설정 `<Defaults>` 의 값이 쿠킹본에 실리므로 컴포넌트 · 구조체 · 에셋 타입을 기본으로 만들어 저작 칸의 값을 접는다(언리얼 증분 쿠킹이
         *          클래스 스키마와 함께 CDO 를 접는 자리). 만들 수 없는 타입(추상 · 데이터로 놓을 수 없는 컴포넌트 · 기본 생성자 없음)은 데이터가 만들 일이 없어 배치 해시만이다.
         *          두 번 만들어 같은 값만 접는다 — 만들 때마다 다른 값(GUID · 런타임 id)이 쿠킹마다 키를 바꾸지 않게. 게임 설정을 읽을 수 있으므로 쿠킹 세션(`CookSession`)을 열기 전에 한 번 부른다.
         */
        static uint64 compute( const TypeRegistry& registry );
    };
} // namespace sw
