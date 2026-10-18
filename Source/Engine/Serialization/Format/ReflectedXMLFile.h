/**
 * @file ReflectedXMLFile.h
 * @brief 리플렉션 desc 하나를 리소스 상대 경로의 XML 파일로 읽고 씁니다(렌더 패스 · 파이프라인 리소스가 씁니다).
 *
 * [왜 있는가]
 * 렌더 리소스 둘(`RenderPassAsset` · `RenderPipelineAsset`)이 "리플렉션 desc 하나를 리소스 상대 경로의 XML 로 오간다" 는
 * **같은 일**을 합니다: `findType<Desc>()` · 널 검사 · 경로 해석 · `XMLSerializer` 호출 · 실패 로그. 둘이 따로 들면 한쪽만 고쳐
 * 다른 쪽이 조용히 다르게 동작합니다.
 *
 * [왜 여기인가]
 * 렌더러와 무관한 일이라 직렬화에 둡니다. `Resource/ResourcePaths.h` 는 어느 티어에서든 쓸 수 있는 경로 헬퍼입니다(`CheckEngineLayers` 의
 * 예외). 언리얼 `FJsonObjectConverter` 가 Core 의 직렬화에 있는 것과 같은 자리입니다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"

#include "Engine/Reflection/ReflectionCore.h"

namespace sw
{
    /**
     * @struct ReflectedXMLFile
     * @brief 리플렉션 desc 를 리소스 상대 경로의 XML 로 읽고 씁니다.
     */
    struct SW_API ReflectedXMLFile
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
            return loadDescInternal( assetRelativePath, ReadSource::Resource, &outDesc, DescType::StaticType() );
        }

        /**
         * @brief 리플렉션 desc 를 **디스크 파일**에서 읽습니다. 리소스 id 로 풀지 않고 마운트된 팩도 보지 않습니다.
         * @param diskPath 디스크 경로(쿠커가 소스 트리를 훑어 얻은 경로 · 테스트의 임시 파일).
         * @details 소스 트리를 디스크로 훑은 쿠커 · 도구 · 테스트용 입구입니다. 같은 경로를 `loadDesc` 에 넣으면 리소스 루트 안 절대 경로라
         *          팩 전용 구성(Shipping)의 낱개 파일 거절에 걸립니다 — 디스크를 읽는다는 뜻을 이름으로 드러내 런타임 읽기와 가릅니다.
         *          게임 런타임 코드는 쓰지 않습니다(`CheckRuntimeResourceDiskAccess`).
         */
        template <typename DescType>
        [[nodiscard]] static bool loadDescFromDiskFile( string_view diskPath, DescType& outDesc )
        {
            return loadDescInternal( diskPath, ReadSource::DiskFile, &outDesc, DescType::StaticType() );
        }

        /**
         * @brief 리플렉션 desc 를 XML 로 씁니다.
         * @param assetRelativePath 리소스 상대 경로. 아직 없는 파일이면 경로를 그대로 씁니다.
         * @return 성공 여부.
         */
        template <typename DescType>
        [[nodiscard]] static bool saveDesc( string_view assetRelativePath, const DescType& desc )
        {
            return saveDescInternal( assetRelativePath, &desc, DescType::StaticType() );
        }

    private:
        /** @brief 본문을 어디서 읽는지입니다. 리소스 id(낱개 파일 정책 · 팩) 또는 디스크 파일. */
        enum class ReadSource : uint8
        {
            Resource,
            DiskFile,
        };

        /** @brief 템플릿을 얇게 두려고 타입을 지운 실제 구현입니다(`XMLSerializer` 는 .cpp 에서만 봅니다). */
        [[nodiscard]] static bool loadDescInternal( string_view path, ReadSource source, void* pDesc, const TypeInfo* pTypeInfo );
        /** @brief 쓰기 쪽의 같은 구현입니다. */
        [[nodiscard]] static bool saveDescInternal( string_view assetRelativePath, const void* pDesc, const TypeInfo* pTypeInfo );
    };
} // namespace sw
