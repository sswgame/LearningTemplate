/**
 * @file WorldWarpModifier.h
 * @brief 씬의 워프 오브젝트 하나에 묶인 공유 모디파이어 스택(월드 공간 워프)을 이 메시에 거는 모디파이어입니다(MegaFiers World Space Warp, 블렌더 변형자의 기준 오브젝트).
 */
#pragma once
#include "GameFramework/Plugin/Editor/MeshModifiers/Stack/MeshModifier.h"
#include "GameFramework/Plugin/Editor/MeshModifiers/Stack/MeshModifierStack.h"

namespace sw::editor
{
    /**
     * @class WorldWarpModifier
     * @brief 워프 정의 파일(`.mfwarp`, 스택 저장 문서 글)의 스택을 워프 오브젝트의 공간에서 겁니다. 여러 메시가 같은 파일 · 같은 오브젝트를 가리키면 한 워프를 함께 받습니다.
     * @details 워프는 두 조각으로 나뉜다 — **어디에**(씬 오브젝트, 모디파이어의 공간 기준 `Space Reference`)와 **무엇을**(정의 파일 하나). 오브젝트를 옮기면
     *          묶인 메시가 모두 그 자리에서 휘고, 파일을 고치면 모두 같이 바뀐다. 이 모디파이어는 늘 월드 공간으로 시작한다(`setWorldSpace`) — 기준 오브젝트가
     *          없으면 월드 원점이 워프 공간이다. 하위 스택에게는 워프 공간이 "메시 로컬" 이다(`DeformableModifier` 와 같은 규칙).
     *          파일 경로가 비면 항등이고, 못 읽거나 형식이 틀리면 경고 한 번 뒤 항등이다. 읽은 스택은 경로가 같은 동안 붙잡아 둔다(다시 읽으려면 경로를 바꾸거나
     *          `onParamsChanged`). 스택은 게임 스레드 한 곳에서만 건다.
     */
    class WorldWarpModifier final : public MeshModifier
    {
    public:
        /** @brief 워프 정의 파일의 확장자입니다(에셋 슬롯 필터). */
        static constexpr const utf8* kFileExtension = ".mfwarp";

        WorldWarpModifier();

        /** @brief 스택 @p stack 을 워프 정의 파일 @p path 로 씁니다(스택 저장 문서 글). 묶음(`DeformableModifier`)의 하위 스택을 워프로 내보낼 때 씁니다. */
        [[nodiscard]] static bool writeWarpFile( const MeshModifierStack& stack, string_view path );
        /** @brief 워프 정의 파일 @p path 를 @p outStack 으로 읽습니다. 못 읽거나 형식이 틀리면 false 입니다. */
        [[nodiscard]] static bool readWarpFile( string_view path, MeshModifierStack& outStack );

        const utf8* getDescription() const override { return "Applies a shared warp definition file in the space of a scene object, so many meshes bend together."; }
        void        applyToMesh( MeshModifierMesh& inoutMesh, const MeshModifierContext& context ) const override;
        void        onParamsChanged() override;
        bool        supportsGPU() const override { return true; }
        /** @brief 읽은 워프 스택입니다(경로가 비었거나 못 읽었으면 빈 스택 - CPU 와 같이 항등). */
        const MeshModifierStack* getGPUChildStack() const override;

    private:
        /** @brief 경로의 워프 스택을 (필요하면 읽어) 냅니다. 못 읽으면 경고 한 번 뒤 nullptr 입니다. */
        const MeshModifierStack* findWarp( const string& path ) const;

        mutable MeshModifierStack _warpStack;
        mutable string            _loadedPath;
        mutable bool              _bLoadTried;
        mutable bool              _bLoaded;
        uint32                    _fileParam;
    };
} // namespace sw::editor
