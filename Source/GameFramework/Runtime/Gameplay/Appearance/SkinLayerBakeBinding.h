/**
 * @file SkinLayerBakeBinding.h
 * @brief 해석한 피부 층 목록(화장 · 문신 · 번짐)을 캐릭터마다 텍스처 한 장으로 굽고 몸 머티리얼에 겁니다(`TextureLayerBakeRequest` — 렌더 스레드가 굽는다).
 * @details 굽기는 층 목록이 바뀔 때만이다 — 열쇠(`computeStackKey`)는 층 값(전송 정밀도) · 층 종류 칸 · 밑 텍스처 · 해상도 · 번짐 번호의 해시다. 열쇠가 같으면 다시 굽지 않는다.
 *          결과 경로는 열쇠에서 나온다(`rendertarget/skinlayer/<열쇠>_…`) — 같은 프리셋의 NPC 들은 한 장을 나눠 쥔다(번짐이 있으면 주인 번호가 열쇠에 들어 따로다).
 *          머티리얼은 굽기가 기록된 뒤에 새 경로로 바꾼다(`TextureCache::takeLayerBaked`) — 그 사이에는 옛 결과를 그린다(빈 타깃이 한 프레임 비치지 않게).
 *          - 알베도: 머티리얼의 `_albedoMap` 을 덮는다(밑 텍스처는 부모 머티리얼의 같은 칸) — 어느 몸 머티리얼이든 따로 선언 없이 읽힌다.
 *          - 표면 덮어쓰기 `_skinLayerSurfaceMap`: r 거칠기 값 · g 거칠기 무게 · b 금속 값 · a 금속 무게(프리멀티플라이 — 원래 × (1 − 무게) + 값). 머티리얼이 선언해야 읽힌다(표면 마스크와 같은 규칙).
 *          - 번짐: 층마다 CPU 텍스처(`cpu/skinsmear/…`, `SkinLayerSmearState::makeTexturePixels`)로 올리고 굽기가 그 층을 옮겨 · 묽혀 읽는다(마스카라 번짐).
 *          언리얼 MetaHuman 은 굽지 않고 머티리얼 층으로, 검은사막 · 유니티 런타임 데칼은 렌더 타깃에 굽는다 — 층이 열 개를 넘는 화장은 굽는 쪽이 픽셀마다 싸다.
 */
#pragma once
#include "Core/Container/span.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Name/hashed_string.h"

#include "Engine/Graphics/Texture/TextureCache.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class MaterialInstance;
    class SkinLayerSmearState;

    struct ResolvedSkinLayer;

    /** @class SkinLayerBakeBinding @brief 파일 머리말 참고. 외형 컴포넌트 하나가 하나 가집니다(게임 스레드). */
    class SW_GF_API SkinLayerBakeBinding
    {
    public:
        /** @brief 알베도 텍스처 파라미터 이름입니다(덮는 칸이자 밑 텍스처를 읽는 칸). */
        static constexpr const utf8* kAlbedoParameter = "_albedoMap";
        /** @brief 표면 덮어쓰기 텍스처 파라미터 이름입니다. */
        static constexpr const utf8* kSurfaceParameter = "_skinLayerSurfaceMap";

        SkinLayerBakeBinding() = default;
        /** @brief 쥔 텍스처를 놓습니다. */
        ~SkinLayerBakeBinding();
        SkinLayerBakeBinding( const SkinLayerBakeBinding& )            = delete;
        SkinLayerBakeBinding& operator=( const SkinLayerBakeBinding& ) = delete;

        /**
         * @brief 굽기 열쇠입니다 — 같으면 다시 굽지 않는다. 층 값은 전송 정밀도로 맞춘 뒤 넣는다(`SkinLayerValue::quantize`).
         * @param listSmearRevision 층마다 쓰는 번짐 텍스처의 번호(번짐이 없는 층은 0)입니다. 하나라도 0 이 아니면 @p ownerKey 가 들어 다른 캐릭터와 나누지 않는다.
         */
        static uint64 computeStackKey( vector_reference<const ResolvedSkinLayer> listLayer, vector_reference<const uint32> listSmearRevision, string_view basePath, uint32 resolution, uint64 ownerKey );
        /** @brief 열쇠의 결과 경로(`rendertarget/skinlayer/<열쇠>_<이름>`)입니다. */
        static string makeTargetPath( uint64 stackKey, string_view name );

        /**
         * @brief 층 목록을 굽고(열쇠가 바뀌었거나 결과가 디바이스 교체로 비었으면) 굽기가 끝난 결과를 @p instance 에 겁니다. 층이 없으면 덮어쓰기를 지우고 모두 놓습니다.
         * @param listSmear 층마다의 번짐 상태(해석 순서 — 몸 동작 컴포넌트)입니다. 비면 번짐 없음.
         * @param ownerKey 이 캐릭터의 키(오브젝트 번호)입니다. 번짐 텍스처 경로와 번짐 굽기의 열쇠를 가른다.
         */
        void apply( uint64 ownerKey, vector_reference<const ResolvedSkinLayer> listLayer, vector_reference<const SkinLayerSmearState> listSmear, uint32 resolution, MaterialInstance& instance );
        /** @brief 덮어쓰기를 지우고 쥔 텍스처를 모두 놓습니다. */
        void release( MaterialInstance* pInstance );
        /** @brief 머티리얼에 건 결과의 열쇠입니다(0 이면 걸지 않았다 — 테스트 · 감시 값). */
        uint64 getAppliedKey() const { return _applied._key; }
        /** @brief 굽기를 맡기고 결과를 기다리는 열쇠입니다(0 이면 없음). */
        uint64 getPendingKey() const { return _pending._key; }

    private:
        /** @brief 번짐 텍스처 하나(층 번호마다)입니다. */
        struct SmearTexture
        {
            string        _path{};
            TextureHandle _load{};
            uint32        _revision{ 0 }; ///< 올린 번짐 번호
        };

        /** @brief 한 열쇠의 결과와 그 입력 핸들입니다(쥐는 동안 렌더 스레드가 `find` 로 읽는다). */
        struct BakeSet
        {
            uint64                  _key{ 0 };
            string                  _albedoPath{};
            string                  _surfacePath{};
            vector<TextureHandle>   _listHandle{}; ///< 결과 둘 · 작업 둘 · 밑 · 마스크들
            TextureLayerBakeRequest _request{};
            uint64                  _bakedHandle{ 0 };  ///< 구운 때의 결과 GPU 핸들 — 바뀌면(디바이스 교체) 다시 굽는다
            uint32                  _submitSerial{ 0 }; ///< 맡길 때의 굽기 번호(`TextureCache::wasLayerBakedAfter`)
            uint8                   _bSubmitted{ SW_FALSE };
        };

        /** @brief 층 @p layerIndex 의 번짐 텍스처를 만들거나 갈아 끼우고 그 번호를 돌려줍니다. 번짐이 없으면(깨끗) 0 이고 경로를 쓰지 않는다. */
        uint32 syncSmearTexture( TextureCache& cache, uint64 ownerKey, uint32 layerIndex, const SkinLayerSmearState& smear );
        /** @brief 결과의 GPU 핸들입니다(아직 없으면 0). */
        static uint64 findResultHandle( const BakeSet& set );
        /** @brief 모든 입력 핸들이 준비됐으면 true 입니다. 실패한 것이 있으면 오류를 한 번 남기고 false 입니다. */
        static bool areInputsReady( const BakeSet& set );

    private:
        vector<SmearTexture> _listSmearTexture{};
        BakeSet              _applied{};
        BakeSet              _pending{};
    };
} // namespace sw
