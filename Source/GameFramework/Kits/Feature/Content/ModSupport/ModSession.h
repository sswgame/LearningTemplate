/**
 * @file ModSession.h
 * @brief 한 게임 세션의 모드 구성입니다 — 시작 때 한 번 검증 · 마운트 · 캐시 갱신하고, 세션이 끝날 때 뗀다. 실행 중에 켜고 끄지 않는다.
 * @details 흐름: 모드 폴더(`<설치 폴더>/<id>/`)의 매니페스트를 읽고(`ModManifest`) 순서를 정하고(`ModResolver`), 팩을 검사하고(경로 · 크기 · 항목 수 · 덮어쓰기),
 *          허용 목록이 있으면 대조하고(서버, `ModAllowList`), 엔진 팩 VFS 에 패치 팩으로 기본 팩 위에 올리고(`PackManager::mountPatchPack`), 층을 밝힌 모드의
 *          층 뿌리(`mods/<id>/layer`)를 엔진 덮어쓰기 층 목록 끝에 더한 뒤(`ResourceLayers` — 모드 순서대로, 뒤가 나중에 얹는다) 덮어쓰거나 층을 얹은 리소스를 쥔
 *          캐시를 다시 읽힌다(`AssetManager::reloadChangedPaths`). 하나라도 실패하면 이 세션은 모드 없이 서지 않고 실패를 돌려준다 — 서버와 다른 구성으로 조용히 돌지 않는다.
 *
 *          팩 VFS 는 엔진의 것이고 이 키트는 조정자다(팩 기능을 복제하지 않는다). 수명은 게임 인스턴스와 같다 — 시나리오 체인 · 모듈 리로드처럼 인스턴스를 다시 세우는
 *          실행에서 앞 세션의 모드가 남지 않는다. 런타임 토글은 에셋 핸들 · 진행 중 로드의 안전 규칙이 필요해 넣지 않았다(`docs/DesignBacklog.md` "모드 지원 키트" 3).
 *
 *          모드 폴더는 바깥 사람이 쓴 신뢰하지 않는 입력이다: id 는 형식(`ModManifest::isValidID`)이라 경로 탈출이 없고, 파일 경로는 id 로만 만든다(매니페스트 글로 만들지 않는다).
 *          모든 크기는 읽기 전에 한계(`ModSupportSettings`)와 견준다. 검사와 마운트 사이에 파일을 바꾸는 틈은 설치 폴더 쓰기 권한이 곧 신뢰라는 가정이 막는다(패치 팩과 같다).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Feature/Content/ModSupport/ModSetDigest.h"
#include "GameFramework/Kits/Feature/Content/ModSupport/ModSupportSettings.h"
#include "GameFramework/Runtime/Foundation/Framework/Flow/IContentConfiguration.h"

namespace sw
{
    class AssetManager;
    struct ModAllowList;

    /** @struct ModSessionRequest @brief 한 세션에 켤 모드와 그 자리입니다. */
    struct ModSessionRequest
    {
        string              _installFolder;         ///< 모드 설치 폴더(절대 경로). 모드마다 `<id>/mod.xml` 과 `<id>/content.pack`(또는 개발 구성의 `<id>/content/`)
        string              _buildFolder;           ///< 소스 모드(`content/`)를 묶은 팩을 둘 폴더(절대 경로). 개발 구성만 쓴다
        vector<string>      _listEnabledID;         ///< 켤 모드 id(순서는 보지 않는다 — `ModResolver`)
        const ModAllowList* _pAllowList{ nullptr }; ///< 서버의 허용 목록. 클라이언트 · 싱글은 없다(nullptr)
    };

    /**
     * @class ModSession
     * @brief 세션의 모드 마운트를 소유합니다. 소멸하면 뗀다.
     */
    class SW_KIT_API ModSession final : public IContentConfiguration
    {
    public:
        /** @brief 모드 팩을 기본 팩 위에 올릴 때 모드 하나의 우선순위 칸입니다(`PackManager` 의 토큰 한 칸과 같은 폭). */
        static constexpr int32 kPriorityStep = 1000;

        explicit ModSession( AssetManager& assets );
        ~ModSession() override;

        ModSession( const ModSession& )            = delete;
        ModSession& operator=( const ModSession& ) = delete;

        /**
         * @brief @p request 의 모드를 검증하고 마운트합니다. 켤 모드가 없으면 아무것도 하지 않고 true 입니다(모드 없는 실행은 그대로다).
         * @return 어느 하나라도 실패하면 이유를 오류로 남기고, 이 호출이 올린 것을 모두 내린 뒤 false 입니다. 세션마다 한 번만 부른다(두 번째는 오류).
         * @details 게임 스레드, 틱 밖(게임 시작)에서 부른다 — 캐시의 제자리 다시 읽기가 틱 밖에서만 된다.
         */
        [[nodiscard]] bool apply( const ModSessionRequest& request, const ModSupportSettings& settings );

        /**
         * @brief 게임의 표준 자리로 `apply` 를 부릅니다 — 켤 모드는 `-gv_modList=a,b`, 설치 폴더는 저장소 `Test/Mods` · 패키지 실행 파일 옆 `Mods`, 정책은 게임 팩의 `modsupport` 데이터(@p packRoot 아래).
         * @details 켤 모드가 없으면 정책 데이터도 읽지 않고 true 다 — 모드를 쓰지 않는 실행은 이 키트가 있어도 그대로다. 언리얼 UGC 예제가 프로젝트의 `Mods/` 를 훑는 자리다(기억에 의존).
         */
        [[nodiscard]] bool applyForGame( string_view packRoot );

        /** @brief 더한 층을 되돌리고 올린 모드 팩을 내리고, 덮어썼거나 층을 얹었던 리소스를 쥔 캐시를 다시 읽힙니다. 올린 것이 없으면 아무것도 하지 않는다. */
        void release();

        /** @brief 지금 켠 구성입니다(켜지 않았으면 빈 구성). */
        const ModSetDigest& getDigest() const { return _digest; }

        // IContentConfiguration — 게임 인스턴스가 넷 와이어 버전 · 세이브 머리에 쓴다(게임이 `bind<IContentConfiguration>` 으로 건다).
        /** @brief 켠 구성의 해시입니다(빈 구성은 0). */
        uint64 getContentConfigHash() const override { return _digest._configHash; }
        /** @brief 세이브 머리에 적는 구성 글(`ModSetDigest::toText`)입니다. */
        string makeContentConfigText() const override { return _digest.toText(); }
        /** @brief 세이브 머리의 구성 글을 읽어 지금 구성과 견줍니다(`ModSetDigest::checkSaveCompatible`). 글이 틀렸으면 거절한다. */
        [[nodiscard]] bool checkSaveCompatible( string_view savedText ) const override;

        /** @brief 모드 설치 폴더입니다 — 패키지는 실행 파일 옆 `Mods`, 저장소는 `Test/Mods`(시연 모드는 시나리오 · 테스트 자료). */
        static string getInstallFolder();
        /** @brief 배포할 묶은 모드를 두는 폴더(`<Saved>/ModPacks`)입니다. 배포 패키지 스크립트(`Scripts/package/pack.py --mods`)가 여기서 베낀다. */
        static string getExportFolder();
        /**
         * @brief 설치 폴더의 소스 모드 @p id(`<id>/content/`)를 배포 형태(`<내보내기 폴더>/<id>/mod.xml` + `content.pack`)로 묶습니다 — 배포 게임은 묶은 모드만 읽는다.
         * @details @p id 는 바깥 입력이라 형식(`ModManifest::isValidID`)부터 본다(경로를 id 로만 만든다). 매니페스트를 읽어 id 가 폴더와 같은지 보고, 팩은 같은 한계 · 경로 검사
         *          (`ModPackBuilder` · 마운트 전 검사와 같은 정책 데이터 @p packRoot 의 `modsupport`)를 지난다. 실패하면 이유를 오류로 남기고 false 이며, 쓴 것은 지운다.
         */
        [[nodiscard]] static bool exportPackedMod( string_view id, string_view packRoot, string& outFolder );

    private:
        /** @brief 이 세션이 바꾼 기본 리소스 id(덮어쓰기와 층, 겹치지 않게)입니다. 캐시를 다시 읽힐 목록이다. */
        vector<string> makeChangedPaths() const;
        /** @brief 켤 모드의 매니페스트를 읽습니다(id 형식 · 파일 크기 · id 와 폴더 이름의 일치). */
        [[nodiscard]] static bool loadManifests( const ModSessionRequest& request, const ModSupportSettings& settings, vector<ModManifest>& outListManifest );
        /** @brief 모드 하나의 팩 경로를 정합니다 — 묶은 팩이면 그것, 소스 폴더면(개발 구성만) 묶어서 그 경로. 둘 다 있거나 없으면 오류입니다. */
        [[nodiscard]] bool preparePack( const ModSessionRequest& request, const ModManifest& manifest, const ModSupportSettings& settings, string& outPackPath );
        /**
         * @brief 팩을 검사하고 내용 해시를 구합니다(크기 · 도메인 없음 · 항목 수 · 항목 크기 · 경로 꼴 · 모드 접두 또는 밝힌 덮어쓰기 · 밝힌 덮어쓰기와 층이 모두 들었고 기본 리소스가 있음).
         * @details 층 폴더(`mods/<id>/layer/`)의 항목은 밝힌 층이어야 한다 — 엔진 층은 그 뿌리 아래 파일이면 무엇이든 얹으므로, 밝히지 않은 층 파일은 정책(`<Overridable>`)을 지나지 않고 기본 리소스를 바꾼다.
         */
        [[nodiscard]] static bool checkPack( string_view packPath, const ModManifest& manifest, const ModSupportSettings& settings, uint64& outContentHash );

    private:
        AssetManager&          _assets;
        vector<string>         _listMountedPackPath;   ///< 이 세션이 올린 팩(마운트 순서)
        vector<string>         _listBuiltPackPath;     ///< 이 세션이 소스 모드에서 묶은 팩 파일(내린 뒤 지운다)
        vector<string>         _listOverridePath;      ///< 켠 모드가 덮어쓴 기본 리소스 id
        vector<string>         _listLayerPath;         ///< 켠 모드가 층을 얹은 기본 리소스 id
        vector<string>         _listPreviousLayerRoot; ///< 모드 층을 더하기 전의 엔진 층 목록(`ResourceLayers` — 내릴 때 되돌린다)
        ModSetDigest           _digest;
        uint8                  _bApplied   : 1; ///< `apply` 를 불렀다(성공 여부와 상관없이 세션마다 한 번)
        uint8                  _bLayersSet : 1; ///< 이 세션이 엔진 층 목록에 모드 층을 더했다
        [[maybe_unused]] uint8 _reserved   : 6;
    };
} // namespace sw
