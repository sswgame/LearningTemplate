/**
 * @file ResourceID.h
 * @brief 리소스 id(도메인으로 시작하는 리소스 경로 — `engine/shaders/lit.hlsl`)를 담는 형식입니다. 리소스 입구(`ResourceIO`)는 이 형식만 받습니다.
 * @details 리소스 id 와 디스크 경로가 같은 `string_view` 로 오가면 디스크 경로를 리소스 입구에 넣는 호출이 컴파일을 지나고, 구성마다 다른 실행 규칙에서야
 *          드러났다(계획 `DiskPathVsResourceId` — 안 B 는 실행 중 거절, 이 형식은 안 C 의 컴파일 막기). 그래서 문자열에서 **암시적으로** 만들 수 없다 —
 *          `ResourceIO::readTextResource( path, … )` 는 컴파일되지 않고, 글을 id 로 쓰는 자리는 `ResourceID{ text }` 로 드러난다.
 *          디스크 경로는 리소스 루트 안이든 밖이든 id 가 아니다 — `ResourcePaths::toResourceID` 가 붙인 도메인 아래 경로를 id 로 바꾼다.
 *          담는 글을 고치지 않는다(대소문자 · 구분자). OS 경로 거절은 입구(`ResourceIO`)가 한 곳에서 한다.
 *          상용 엔진: 언리얼 `FSoftObjectPath`(문자열에서 명시적으로 만든다), 유니티는 GUID, Godot 은 `res://` 접두가 붙은 문자열이다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"

namespace sw
{
    /**
     * @class ResourceID
     * @brief 리소스 id 하나입니다. 빈 값은 "id 없음" 입니다.
     */
    class ResourceID
    {
    public:
        /** @brief id 없음입니다. */
        ResourceID() = default;
        /** @brief 글 @p id 를 리소스 id 로 씁니다. 암시적 변환은 없다 — 디스크 경로와 섞지 않으려고 부르는 자리에 형식을 적게 한다. */
        explicit ResourceID( string_view id )
            : _id{ id }
        {
        }

        /** @brief id 가 비었으면 true 입니다. */
        [[nodiscard]] bool isEmpty() const { return _id.empty(); }
        /** @brief id 글입니다. */
        [[nodiscard]] const string& str() const { return _id; }
        /** @brief id 글입니다. */
        [[nodiscard]] string_view view() const { return _id; }
        /** @brief id 글(끝에 0)입니다. */
        [[nodiscard]] const utf8* c_str() const { return _id.c_str(); }

        [[nodiscard]] bool operator==( const ResourceID& other ) const { return _id == other._id; }
        [[nodiscard]] bool operator!=( const ResourceID& other ) const { return _id != other._id; }

    private:
        string _id;
    };
} // namespace sw
