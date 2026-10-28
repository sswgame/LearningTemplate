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
 * 렌더러와 무관한 일이라 직렬화에 둡니다. `Resource/AssetPaths.h` 는 어느 티어에서든 쓸 수 있는 경로 헬퍼입니다(층 표의
 * 예외). 언리얼 `FJSONObjectConverter` 가 Core 의 직렬화에 있는 것과 같은 자리입니다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"

#include "Engine/Asset/AssetID.h"
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
         * @param assetID 리소스 id (`engine/renderpass/defaultrenderpass.xml` 등).
         * @param outDesc 읽어 채울 desc. **실패해도 부르는 쪽이 이미 비워 둔 상태를 그대로 둡니다.**
         * @return 성공 여부. TypeInfo 가 없거나(리플렉션 생성 누락) 파일을 못 읽으면 false.
         * @details 실패 로그는 이 함수가 남깁니다. 부르는 쪽마다 다른 문장을 적으면 무엇이 실패했는지
         *          로그만 보고는 알 수 없습니다. 타입 이름을 함께 찍으므로 어느 리소스인지 드러납니다.
         */
        template <typename DescType>
        [[nodiscard]] static bool loadDesc( const AssetID& assetID, DescType& outDesc )
        {
            return loadDescInternal( assetID, &outDesc, DescType::StaticType() );
        }

        /**
         * @brief 리플렉션 desc 를 **디스크 파일**에서 읽습니다. 리소스 id 로 풀지 않고 마운트된 팩도 보지 않습니다.
         * @param diskPath 디스크 경로(쿠커가 소스 트리를 훑어 얻은 경로 · 테스트의 임시 파일).
         * @details 소스 트리를 디스크로 훑은 쿠커 · 도구 · 테스트용 입구입니다. 같은 경로를 `loadDesc` 에 넣으면 리소스 루트 안 절대 경로라
         *          팩 전용 구성(Shipping)의 낱개 파일 거절에 걸립니다 — 디스크를 읽는다는 뜻을 이름으로 드러내 런타임 읽기와 가릅니다.
         *          게임 런타임 코드는 쓰지 않습니다.
         */
        template <typename DescType>
        [[nodiscard]] static bool loadDescFromDiskFile( string_view diskPath, DescType& outDesc )
        {
            return loadDescFromDiskFileInternal( diskPath, &outDesc, DescType::StaticType() );
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
        /**
         * @brief 템플릿을 얇게 두려고 타입을 지운 실제 구현입니다(`XMLSerializer` 는 .cpp 에서만 봅니다).
         * @details 리소스 id 와 디스크 경로는 입구를 형식으로 가른다 — 한 함수가 열거형으로 고르면 같은 `string_view` 가 어느 쪽으로도 흘러간다.
         */
        [[nodiscard]] static bool loadDescInternal( const AssetID& assetID, void* pDesc, const TypeInfo* pTypeInfo );
        /** @brief 디스크 파일 판입니다. */
        [[nodiscard]] static bool loadDescFromDiskFileInternal( string_view diskPath, void* pDesc, const TypeInfo* pTypeInfo );
        /** @brief 두 입구가 읽은 본문을 desc 로 풉니다. @p sourceName 은 로그에 찍는 이름입니다. */
        [[nodiscard]] static bool parseDesc( string_view sourceName, const string& text, void* pDesc, const TypeInfo* pTypeInfo );
        /** @brief 쓰기 쪽의 같은 구현입니다. */
        [[nodiscard]] static bool saveDescInternal( string_view assetRelativePath, const void* pDesc, const TypeInfo* pTypeInfo );
    };
} // namespace sw
