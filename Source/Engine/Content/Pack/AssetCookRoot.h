/**
 * @file AssetCookRoot.h
 * @brief 코드가 이름(리터럴 경로)으로 읽는 엔진 에셋의 등록표입니다 — 팩 쿠킹의 뿌리가 됩니다(`PackReachability`).
 * @details 쓰는 것만 담는 쿠킹은 참조 칸을 따라가므로, 칸이 아니라 코드 상수로 읽는 파일(`IAudioService::kDefaultMixerPath` 등)은 그 길로 보이지 않는다.
 *          그 상수를 가진 기능이 자기 .cpp 에 `SW_ASSET_COOK_ROOT` 한 줄로 같은 상수를 올린다 — 경로가 상수 한 곳에만 있어 규칙이 한 벌이다.
 *          데이터 표(`EngineDefaultAssets`)로 옮기지 않은 까닭: 이 넷(믹서 · 2D 설정 · 블렌드 프리셋 · 물리 설정)은 서비스 없이도 읽힌다 — 이름 검증(`PhysicsNameValidation`)과
 *          정적 표(`RenderBlendPresetTable`)는 엔진 서비스가 없는 도구 · 테스트에서도 돌고, 테스트 하네스의 기본 에셋 표는 비어 있다. 표로 옮기면 서비스가 없을 때의 대체 길이 생긴다.
 *          엔진 기본 에셋 표 자체(`constant::path::kEngineDefaultAssets`)도 이 등록으로 뿌리가 되고, 그 칸들이 나머지 엔진 에셋을 끌고 온다.
 *          등록은 엔진 안에만 둔다 — 팩 쿠킹은 게임 모듈을 올리기 전에 돈다. 게임 코드가 리터럴로 읽는 것은 `PackCookRules::_listAlwaysCook` 데이터에 적는다.
 *          언리얼은 코드가 쓰는 에셋을 설정의 소프트 경로(`DefaultEngine.ini`)나 Directories to Always Cook 으로 쿠커에 알린다 — 여기는 상수 곁의 등록이다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

namespace sw
{
    /**
     * @struct AssetCookRootRegistry
     * @brief 등록된 코드 뿌리(리소스 id)의 표입니다. 표는 프로세스 값이고 정적 등록자가 채웁니다.
     */
    struct SW_API AssetCookRootRegistry
    {
        /** @brief 등록된 뿌리를 @p outListAssetID 끝에 더합니다(등록 순서). */
        static void appendRoots( vector<string>& outListAssetID );
        /** @brief @p assetID 가 등록돼 있으면 true 입니다(테스트). */
        static bool hasRoot( string_view assetID );

    private:
        friend class AssetCookRootRegistrar;
        static vector<string_view>& getRoots();
    };

    /** @brief `SW_ASSET_COOK_ROOT` 가 두는 정적 등록자입니다. 만들 때 올리고 사라질 때 내립니다. 경로는 정적 글(상수)이어야 한다. */
    class SW_API AssetCookRootRegistrar
    {
    public:
        explicit AssetCookRootRegistrar( string_view assetID );
        ~AssetCookRootRegistrar();
        AssetCookRootRegistrar( const AssetCookRootRegistrar& )            = delete;
        AssetCookRootRegistrar& operator=( const AssetCookRootRegistrar& ) = delete;

    private:
        string_view _assetID;
    };
} // namespace sw

/**
 * @brief 코드가 리터럴로 읽는 에셋을 팩 쿠킹 뿌리로 등록합니다(상수를 가진 기능의 .cpp, 파일 범위). @p assetID 는 그 상수입니다.
 * @code
 *     SW_ASSET_COOK_ROOT( DefaultAudioMixer, ::sw::IAudioService::kDefaultMixerPath );
 * @endcode
 */
#define SW_ASSET_COOK_ROOT( id, assetID )                                    \
    static const ::sw::AssetCookRootRegistrar sw_assetCookRootRegistrar_##id \
    {                                                                        \
        ::sw::string_view( assetID )                                         \
    }
