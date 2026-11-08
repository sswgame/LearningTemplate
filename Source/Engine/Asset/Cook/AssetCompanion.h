/**
 * @file AssetCompanion.h
 * @brief 곁 파일 규칙의 등록표입니다 — 런타임이 칸이 아니라 이름 규칙으로 찾는 옆 파일(`a/b.mesh` → `a/b.fracture`, `a/line.wav` → `a/line.visemes.xml`).
 * @details 팩 쿠킹은 참조 칸(`AssetReferenceFields`)을 따라 쓰는 것만 담는다(`PackReachResult`). 칸이 없는 옆 파일은 그 길로 보이지 않으므로, 규칙을 가진
 *          기능이 자기 .cpp 에 `SW_ASSET_COMPANION` 한 줄로 경로 함수를 올린다 — 런타임이 쓰는 함수(`FractureAsset::makePathForMesh` 등)를 그대로 넘겨 규칙이 한 벌이다.
 *          곁 파일은 있으면 담고 없으면 넘어간다(런타임도 있으면 쓰는 선택 파일이다). 빠진 참조와 달리 오류가 아니다.
 *          등록은 엔진 안에만 둔다 — 팩 쿠킹은 게임 모듈을 올리기 전에 돈다(`HeadlessSubsystem::runCookTask`). 게임 · 키트가 곁 규칙을 쓰려면 칸으로 적는다.
 *          언리얼은 패키지가 의존을 스스로 적어(에셋 레지스트리의 소프트 의존) 쿠커가 따라간다 — 여기는 이름 규칙이 코드에 있어 그 코드가 등록한다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

namespace sw
{
    /** @brief 리소스 id 에서 곁 파일의 리소스 id 를 만드는 함수입니다. 이 규칙이 다루지 않는 파일이면 빈 글을 돌려줍니다. */
    using AssetCompanionPathFunction = string ( * )( string_view assetID );

    /** @struct AssetCompanionRule @brief 곁 파일 규칙 하나입니다. */
    struct AssetCompanionRule
    {
        const utf8*                _pName{ nullptr };     ///< 규칙 이름(로그 · 테스트)
        AssetCompanionPathFunction _pMakePath{ nullptr }; ///< 경로 함수
    };
} // namespace sw

namespace sw
{
    class AssetCompanionRegistrar;
    template <typename T>
    class RegistrationList;

    /**
     * @struct AssetCompanionRegistry
     * @brief 등록된 곁 파일 규칙의 표입니다. 표는 프로세스 값이고 정적 등록자가 채웁니다.
     */
    struct SW_API AssetCompanionRegistry
    {
        /** @brief @p assetID 의 곁 파일 후보(규칙마다 하나, 빈 글은 뺀다)를 @p outListPath 끝에 더합니다. 있는지는 부르는 쪽이 본다. */
        static void appendCompanionPaths( string_view assetID, vector<string>& outListPath );
        /** @brief 이름이 @p name 인 규칙이 있으면 true 입니다(테스트). */
        static bool hasRule( string_view name );

    private:
        friend class AssetCompanionRegistrar;
        /** @brief 정적 등록 표입니다(공통 틀 `SW_DEFINE_REGISTRATION_LIST`). */
        static RegistrationList<const AssetCompanionRegistrar>& getRegistrars();
    };

    /** @brief `SW_ASSET_COMPANION` 이 두는 정적 등록자입니다. 만들 때 올리고 사라질 때 내립니다. */
    class SW_API AssetCompanionRegistrar
    {
    public:
        explicit AssetCompanionRegistrar( const AssetCompanionRule& rule );
        ~AssetCompanionRegistrar();
        AssetCompanionRegistrar( const AssetCompanionRegistrar& )            = delete;
        AssetCompanionRegistrar& operator=( const AssetCompanionRegistrar& ) = delete;

        /** @brief 등록한 규칙입니다. */
        const AssetCompanionRule& getRule() const { return _rule; }

    private:
        AssetCompanionRule _rule;
    };
} // namespace sw

/**
 * @brief 곁 파일 규칙을 등록합니다(규칙을 가진 기능의 .cpp, 파일 범위). @p id 는 정적 이름이자 규칙 이름이고, @p makePath 는 `AssetCompanionPathFunction` 입니다.
 * @code
 *     SW_ASSET_COMPANION( MeshFracture, &FractureAsset::makeCompanionPath );
 * @endcode
 */
#define SW_ASSET_COMPANION( id, makePath )                                     \
    static const ::sw::AssetCompanionRegistrar sw_assetCompanionRegistrar_##id \
    {                                                                          \
        ::sw::AssetCompanionRule                                               \
        {                                                                      \
            #id, makePath                                                      \
        }                                                                      \
    }
