/**
 * @file PostProcessMaterialSet.h
 * @brief 뷰의 후처리 블렌더블 경로를 머티리얼로 푸는 게임 스레드 쪽 자리입니다 — 머티리얼 참조를 쥐고, 읽기를 마친 것만 렌더 뷰 설정에 싣습니다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Name/hashed_string.h"

namespace sw
{
    struct RenderViewSettings;

    class Material;

    /**
     * @class PostProcessMaterialSet
     * @brief 블렌더블이 가리키는 머티리얼의 참조(`MaterialCache::acquire`)를 프레임을 넘어 쥐고, 읽기를 마친(Ready) 것의 렌더 사본을 뷰에 싣습니다.
     * @details `RenderPacketViewSetup` 이 값으로 소유한다. 뷰마다 `resolve`, 프레임 끝에 `endFrame` 이 이번 프레임에 쓰이지 않은 참조를 놓는다(볼륨을 나가면 머티리얼도 놓인다).
     *          PostProcess 영역이 아닌 머티리얼(`kMaterialDomainPostProcessDefine` 없음)은 오류를 한 번 남기고 싣지 않는다 — 메시용 셰이더로 풀스크린을 그리지 않는다.
     *          언리얼은 블렌더블이 UMaterialInterface 를 직접 쥐지만, 여기서는 값(경로)을 들고 이 자리가 참조를 센다(볼륨 값이 데이터라 복사가 싸다).
     */
    class SW_API PostProcessMaterialSet
    {
    public:
        PostProcessMaterialSet();
        ~PostProcessMaterialSet();
        PostProcessMaterialSet( const PostProcessMaterialSet& )            = delete;
        PostProcessMaterialSet& operator=( const PostProcessMaterialSet& ) = delete;

        /** @brief @p inoutSettings 의 블렌더블(`_postProcess._listBlendable`)을 차례대로 풀어 `_listPostProcessMaterial` 을 채웁니다(지우고 채운다). 읽는 중인 머티리얼은 다음 프레임에 실린다. */
        void resolve( RenderViewSettings& inoutSettings );
        /** @brief 이번 프레임에 어느 뷰도 쓰지 않은 머티리얼 참조를 놓고 프레임 번호를 넘깁니다. 모든 뷰의 `resolve` 뒤에 부릅니다. */
        void endFrame();
        /** @brief 쥔 참조를 모두 놓습니다(소멸 · 엔진 서비스가 내려가기 전). */
        void releaseAll();

    private:
        /** @brief 참조를 쥔 머티리얼 하나입니다. */
        struct Entry
        {
            hashed_string _path;
            Material*     _pMaterial{ nullptr };
            uint64        _lastUsedFrame{ 0 };
            uint8         _bDomainReported{ SW_FALSE }; ///< PostProcess 영역이 아니라는 오류를 이미 남겼다
        };

        /** @brief @p path 의 항목을 찾거나 참조를 확보해 만듭니다. 확보하지 못하면 nullptr 입니다. */
        Entry* findOrAcquire( const hashed_string& path );

    private:
        vector<Entry> _listEntry;
        uint64        _frame;
    };
} // namespace sw
