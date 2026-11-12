# Scripts/common/Git.sh — 셸 스크립트의 git 입구(파이썬 `common/Git.py` 의 runGit 과 같은 뜻). `source` 해서 `sw_git <인자…>` 로 부른다.
# 물려받은 저장소 환경 변수(`git rev-parse --local-env-vars` — GIT_DIR · GIT_INDEX_FILE · GIT_WORK_TREE …)를 서브셸에서 비우고 부른다.
# 커밋 훅 안에서 부른 스크립트가 그 값을 물려받으면 다른 저장소를 가리킨 git 이 훅의 저장소를 건드린다. 셸에서 git 을 직접 부르는 줄은 platform-chains 가 막는다.
sw_git() {
	(
		# shellcheck disable=SC2046 — 이름 목록을 낱말로 가른다
		unset $(git rev-parse --local-env-vars)
		git "$@"
	)
}
