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
         * @brief @p registry 의 타입 · 프로퍼티 · 열거자 지문입니다. 이름순으로 늘어놓아 등록 순서(링크 순서)와 관계없다.
         * @details 쿠킹 결과를 정하는 표의 칸을 모두 든다 — 배치 해시(`TypeInfo::getSchemaLayoutHash`: 저작 칸의 이름 · 형식 · 상속), 옛 이름(읽기), 선언 기본값, 필수 · 열쇠(프리팹 합치기),
         *          에셋 경로 표시(프리로드 목록). 빠진 칸이 있으면 그 칸만 바꾼 빌드가 옛 쿠킹본을 다시 쓴다. C++ 생성자가 주는 기본값은 표에 없어 들지 못한다.
         */
        static uint64 compute( const TypeRegistry& registry );
    };
} // namespace sw
