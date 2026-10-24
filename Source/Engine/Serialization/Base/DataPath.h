/**
 * @file DataPath.h
 * @brief 데이터 파일만 값을 넣을 수 있는 리소스 경로 칸입니다(엔진 기본 에셋 · 시스템 설정 · 게임 설정의 경로).
 * @details 값의 기준은 데이터 파일이고 헤더는 형식만 갖는다(결정 기록 5-4 "유지보수성을 위한 데이터화"). 그래서 이 형식은
 *          문자열 · 리터럴로 만들 수 없다 — `DataPath _shaderForwardLit{ "engine/shaders/…" }` 는 컴파일되지 않는다. 값은 리플렉션 데이터 읽기
 *          (`XMLSerializer` · `JSONSerializer` 가 부르는 텍스트 처리기, `SerializeContext` 의 "DataPath" 처리기)만 넣는다.
 *          만든 직후의 칸은 "읽지 않음" 이다. 칸을 가진 구조체는 `PROPERTY( Required )` 로 두고, 읽기가 빠진 칸을 오류로 알린다
 *          (예: `EngineDefaultAssets::load`). 읽지 않은 칸을 쓰면 Debug 에서 어서션이 난다.
 *
 *          쓰는 법:
 *            PROPERTY( Required )
 *            DataPath _shaderToon;                                  // 헤더 — 값 없음
 *            <EngineDefaultAssets _shaderToon="engine/shaders/toon.hlsl" />   // 데이터 — 값의 기준
 *            createPipelineState( assets._shaderToon.get() );       // 읽는 쪽
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"

namespace sw
{
    class SerializeContext;

    /**
     * @class DataPath
     * @brief 데이터 읽기만 채우는 리소스 경로(도메인으로 시작하는 리소스 id — `engine/shaders/toon.hlsl`)입니다.
     */
    class SW_API DataPath
    {
    public:
        /** @brief 읽지 않은 칸입니다. 값은 데이터 읽기만 넣는다. */
        DataPath()                = default;
        DataPath( const utf8* )   = delete;
        DataPath( const string& ) = delete;
        DataPath( string_view )   = delete;

        /** @brief 데이터가 값을 넣었으면 true 입니다. */
        [[nodiscard]] bool isSet() const { return _path.empty() == false; }
        /** @brief 경로입니다. 읽지 않은 칸이면 Debug 에서 어서션이 난다(빈 문자열을 다른 경로 대신 쓰지 않는다). */
        [[nodiscard]] const string& get() const
        {
            SW_ASSERT( isSet() );
            return _path;
        }
        [[nodiscard]] const utf8* c_str() const { return get().c_str(); }

        /** @brief 같은 경로인지 봅니다(읽지 않은 칸은 빈 문자열과 같다). */
        [[nodiscard]] bool operator==( string_view other ) const { return string_view( _path ) == other; }
        [[nodiscard]] bool operator!=( string_view other ) const { return string_view( _path ) != other; }

        /** @brief 리플렉션 텍스트 처리기("DataPath")를 직렬화 문맥에 등록합니다. 기본 문맥이 만들어질 때 한 번 부른다. */
        static void registerSerializeHandler( SerializeContext& context );

    private:
        string _path;
    };
} // namespace sw
