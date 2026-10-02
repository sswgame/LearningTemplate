/**
 * @file ReflectedXmlFile.h
 * @brief 리플렉션 desc 하나를 리소스 상대 경로의 XML 파일로 읽고 씁니다(렌더 패스 · 파이프라인 리소스가 씁니다).
 *
 * [왜 있는가]
 * 렌더 리소스 둘(`RenderPassResource` · `RenderPipelineResource`)이 "리플렉션 desc 하나를 리소스 상대 경로의 XML 로 오간다" 는
 * **같은 일**을 각자 적고 있었습니다: `findType<Desc>()` · 널 검사 · 경로 해석 · `XmlSerializer` 호출 · 실패 로그. 한쪽만 고치면
 * 다른 쪽이 조용히 다르게 동작했습니다(경로 해석 규칙을 바꿨는데 한 곳만 반영되는 경우).
 *
 * [왜 여기인가]
 * 렌더러 옆(`Graphics/Renderer/Pipeline/RenderResourceXml`)에 있었습니다 — 만들 때 직렬화(티어 2)가 `Resource`(티어 4)를 볼 수
 * 없었기 때문입니다. 지금은 `Resource/ResourceUtil.h` 가 어느 티어에서든 쓸 수 있는 경로 헬퍼라(`CheckEngineLayers` 의 예외) 그
 * 이유가 없어졌고, 렌더러와 무관한 일이라 직렬화로 옮겼습니다(언리얼 `FJsonObjectConverter` 가 Core 의 직렬화에 있는 것과 같은 자리).
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Reflection/ReflectionCore.h"

namespace sw
{
    /**
     * @struct ReflectedXmlFile
     * @brief 리플렉션 desc 를 리소스 상대 경로의 XML 로 읽고 씁니다.
     */
    struct SW_API ReflectedXmlFile
    {
        /**
         * @brief 리플렉션 desc 를 XML 에서 읽습니다.
         * @param assetRelativePath 리소스 상대 경로 (`engine/renderpass/defaultrenderpass.xml` 등).
         * @param outDesc 읽어 채울 desc. **실패해도 부르는 쪽이 이미 비워 둔 상태를 그대로 둡니다.**
         * @return 성공 여부. TypeInfo 가 없거나(리플렉션 생성 누락) 파일을 못 읽으면 false.
         * @details 실패 로그는 이 함수가 남깁니다. 부르는 쪽마다 다른 문장을 적으면 무엇이 실패했는지
         *          로그만 보고는 알 수 없습니다. 타입 이름을 함께 찍으므로 어느 리소스인지 드러납니다.
         */
        template <typename DescType>
        [[nodiscard]] static bool loadDesc( string_view assetRelativePath, DescType& outDesc )
        {
            return loadDescInternal( assetRelativePath, &outDesc, engine::getTypeRegistry().findType<DescType>() );
        }

        /**
         * @brief 리플렉션 desc 를 XML 로 씁니다.
         * @param assetRelativePath 리소스 상대 경로. 아직 없는 파일이면 경로를 그대로 씁니다.
         * @return 성공 여부.
         */
        template <typename DescType>
        [[nodiscard]] static bool saveDesc( string_view assetRelativePath, const DescType& desc )
        {
            return saveDescInternal( assetRelativePath, &desc, engine::getTypeRegistry().findType<DescType>() );
        }

    private:
        /** @brief 템플릿을 얇게 두려고 타입을 지운 실제 구현입니다(`XmlSerializer` 는 .cpp 에서만 봅니다). */
        [[nodiscard]] static bool loadDescInternal( string_view assetRelativePath, void* pDesc, const TypeInfo* pTypeInfo );
        /** @brief 쓰기 쪽의 같은 구현입니다. */
        [[nodiscard]] static bool saveDescInternal( string_view assetRelativePath, const void* pDesc, const TypeInfo* pTypeInfo );
    };
} // namespace sw
