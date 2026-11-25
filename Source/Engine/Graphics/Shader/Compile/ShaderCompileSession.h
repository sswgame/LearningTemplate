/**
 * @file ShaderCompileSession.h
 * @brief 셰이더 컴파일 묶음 하나(쿠킹 · 데우기 · 쿠킹본 판정)가 함께 쓰는 표입니다 — 소스 풀기 · include 바이트 · 입력의 지금 해시를 변형마다 되풀이하지 않습니다.
 */
#pragma once
#include "Core/Concurrency/mutex.h"
#include "Core/Container/unordered_map.h"

#include "Engine/EngineMinimal.h"
#include "Engine/Graphics/Shader/Compile/ShaderSource.h"

namespace sw
{
    struct CookJobDesc;
    struct CookRecord;
} // namespace sw

namespace sw
{
    /**
     * @class ShaderCompileSession
     * @brief 묶음 동안 소스 파일은 바뀌지 않는다고 보고 파일마다 한 번만 읽고 해시합니다. 변형 수만 개가 같은 헤더 25 개와 없는 후보를 저마다 열고 해시하던 일을 파일마다 한 번으로 줄입니다.
     * @details 유니티 캐싱 전처리기(include 를 한 번 읽어 변형이 함께 쓴다)와 언리얼 셰이더 컴파일의 include 글 캐시(`GetShaderFileHash` · `LoadShaderSourceFile`)가 같은 자리다.
     *          실행 중 컴파일(핫 리로드)은 쓰지 않는다 — 파일이 바뀌는 중이라 요청마다 디스크를 본다. 여러 워커가 동시에 부른다.
     */
    class SW_API ShaderCompileSession
    {
    public:
        explicit ShaderCompileSession( const AssetPaths& assetPaths );
        ShaderCompileSession( const ShaderCompileSession& )            = delete;
        ShaderCompileSession& operator=( const ShaderCompileSession& ) = delete;

        /** @brief 소스 해석기입니다(include 를 묶음의 표로 읽는다). include 루트를 풀지 못했으면 nullptr 이고 오류는 만들 때 남았습니다. */
        [[nodiscard]] const shared_ptr<const ShaderSourceResolver>& getResolver() const { return _pResolver; }
        /** @brief @p source 를 풉니다(`ShaderSourceResolver::resolve` 를 소스마다 한 번). 해석기가 있을 때만 부릅니다. */
        [[nodiscard]] expected<ShaderCompileInput> resolve( const ShaderSourceID& source );
        /** @brief `ShaderIncludeResolver::findShadowedHeaders` 를 폴더마다 한 번 부른 결과입니다. 해석기가 있을 때만 부릅니다. */
        [[nodiscard]] bool findShadowedHeaders( const AbsolutePath& shadersFolder, vector<string>& outListProblem );
        /** @brief `CookRecord::isFresh` 와 같되 입력마다 지금 해시를 묶음에서 한 번만 구합니다. */
        [[nodiscard]] bool isRecordFresh( const CookRecord& record, const CookJobDesc& desc );
        /** @brief 입력 모두가 지금도 기록과 같은 내용이면 true 입니다. */
        [[nodiscard]] bool areInputsFresh( const vector<CookInput>& listInput );

    private:
        /** @brief 가림 검사 하나의 결과입니다. */
        struct ShadowedHeaders
        {
            vector<string> _listProblem;
            bool           _bClean{ false };
        };

        /** @brief 입력의 지금 해시입니다(표에 없으면 구해 넣는다). */
        [[nodiscard]] uint64 computeInputHash( const CookInput& input );

    private:
        shared_ptr<const ShaderSourceResolver>              _pResolver;
        mutex                                               _mutex;
        unordered_map<string, expected<ShaderCompileInput>> _mapInput;
        unordered_map<string, ShadowedHeaders>              _mapShadowedHeaders;
        unordered_map<string, uint64>                       _mapInputHash; ///< 열쇠는 `CookInput::makeSourceKey`
    };
} // namespace sw
