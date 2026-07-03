#!/usr/bin/env bash
# ==========================================================================
# 인포그래픽 PNG 재생성 (HTML -> 3x PNG, 헤드리스 Chrome/Edge, Chrome 149 대응)
#   사용법:  bash build.sh
# 논리크기 x DSF3 = 최종 해상도 (PPT용 고해상도, Pretendard 임베드):
#   02 : 1520x842 -> 4560x2526   (02_completion_order.png)
#   03 : 1520x888 -> 4560x2664   (03_page_lock.png)
#   04 : 1520x800 -> 4560x2400   (04_one_pending_pattern.png)
#   01_pending_limit.png : 기존 히어로(소스 없음) — 재빌드 대상 아님
# NOTE: 실제 렌더 검증은 PowerShell build.ps1 로 했다. 이 build.sh 는 동일 로직(bash)이나 미검증.
# ==========================================================================
set -euo pipefail
DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

CHROME="/c/Program Files/Google/Chrome/Application/chrome.exe"
[ -f "$CHROME" ] || CHROME="/c/Program Files (x86)/Microsoft/Edge/Application/msedge.exe"
[ -f "$CHROME" ] || { echo "Chrome/Edge not found"; exit 1; }

render () {  # <name> <W> <H> <상위폴더_최종파일명>
  local name="$1" w="$2" h="$3" out="$4"
  # 폴더 경로에 공백/한글이 섞여 Start-Process/인자 마샬링이 깨진다 -> 공백 없는 ASCII 임시폴더에
  # HTML/CSS/폰트를 스테이징한 뒤 렌더하고 결과 PNG 만 원위치로 가져온다. common.css 스테이징 필수.
  local work="${TEMP:-/tmp}/mmo-render-work"
  rm -rf "$work"; mkdir -p "$work/fonts"
  cp "$DIR/$name.html" "$work/in.html"
  cp "$DIR/common.css" "$work/common.css"
  cp "$DIR/fonts/"* "$work/fonts/"
  local ww; ww="$(cygpath -m "$work")"

  # --user-data-dir 필수: 평소 쓰는 Chrome이 떠 있으면 헤드리스가 그 창으로 명령을 넘기고 조용히 종료함.
  # --headless=new 필수(구형 --headless 는 Chrome 149에서 스크린샷이 조용히 실패).
  local profile="${TEMP:-/tmp}/chrome-headless-render"; rm -rf "$profile"
  "$CHROME" --headless=new --disable-gpu --hide-scrollbars --allow-file-access-from-files \
    --force-device-scale-factor=3 --user-data-dir="$(cygpath -m "$profile")" \
    --screenshot="$ww/out.png" --window-size="$w,$h" --virtual-time-budget=4000 \
    "file:///$ww/in.html"

  [ -f "$work/out.png" ] || { echo "screenshot failed: out.png not created"; exit 1; }
  cp "$work/out.png" "$DIR/$name.png"
  cp "$work/out.png" "$DIR/../$out"
  echo "  built  ../$out   (논리 ${w}x${h}, 3x 출력)"
}

echo "[build] using: $CHROME"
render 02 1520 842 "02_completion_order.png"
render 03 1520 888 "03_page_lock.png"
render 04 1520 800 "04_one_pending_pattern.png"
echo "[build] done."
