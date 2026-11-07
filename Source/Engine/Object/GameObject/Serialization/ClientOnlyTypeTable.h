/**
 * @file Engine/Object/GameObject/Serialization/ClientOnlyTypeTable.h
 * @brief 전용 서버가 짓지 않는 소스(런타임 UI · `Client/`)의 리플렉션 형식 이름 표입니다 — 서버가 씬에서 그 컴포넌트를 만나면 `MissingComponent` 가 아니라 조용히 건너뜁니다.
 * @details 표는 생성 파일 `Config/Engine/ClientOnlyTypes.generated.json`(`Scripts/generate/ClientOnlyTypes.py`, 커밋 훅이 낡음을 잡는다)이다. 서버는 그 형식을 모르므로
 *          이름 바꾸기 · 빠진 모듈(오류 — 경고를 남기고 원문을 지킨다)과 클라이언트 전용(정상 — 서버에는 할 일이 없다)을 이 표로 가른다.
 *          언리얼은 클래스를 서버도 지어 두고 HUD 를 띄우지 않지만, 여기는 클래스를 짓지 않으니 이름만 싣는다(기억). 클라이언트 구성은 표를 읽지 않는다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Macros.h"

namespace sw
{
    /** @struct ClientOnlyTypeTable @brief 표 읽기입니다. 처음 물을 때 한 번 읽고(스레드 안전) 프로세스가 끝날 때까지 둡니다. */
    struct SW_API ClientOnlyTypeTable
    {
        static constexpr const utf8* kTableFile = "Config/Engine/ClientOnlyTypes.generated.json";

        /** @brief @p typeName 이 이 구성이 짓지 않는 클라이언트 전용 형식이면 true 입니다. 클라이언트 구성(`build::kWithClientCode`)은 언제나 false 입니다. */
        [[nodiscard]] static bool isClientOnlyTypeSkipped( string_view typeName );
    };
} // namespace sw
