# UxPlay Windows 포터블 (최신 원본) 빌드

이 포크에 추가한 Windows 전용 자동 빌드 방식입니다. **원본 FDH2/UxPlay 소스는 변경하지 않습니다.**

## 한 번의 클릭으로 빌드하기

1. 저장소 [Actions](https://github.com/Yuri-0/UxPlay/actions) 탭을 엽니다.
2. Actions를 사용할 수 없다면 포크에서 **Enable workflows**를 누릅니다.
3. **Build latest UxPlay Windows Portable** 워크플로를 선택합니다.
4. **Run workflow**를 클릭하여 수동 빌드를 시작합니다.
5. 성공한 실행 화면 아래 **Artifacts → UxPlay-Windows-Portable-x64**를 다운로드합니다.
6. GitHub 아티팩트 ZIP을 풀고 그 안의 **UxPlay-Portable.zip**도 압축 해제합니다.
7. **UxPlay-Portable/UxPlay.exe를 더블클릭**하면 AirPlay 수신 서버가 실행됩니다.

추가 명령어, BAT 파일, MSYS2/GStreamer 설치가 필요 없도록 실행기와 종속 DLL들을 같은 폴더에 묶습니다. **단일 EXE 한 파일만 실행하는 방식은 아닙니다.** 폴더 전체가 필요합니다.

## 산출물

```text
UxPlay-Portable/
  UxPlay.exe                # 더블클릭 실행기
  README-PORTABLE.txt
  BUILD-INFO.txt            # 실제 빌드된 원본 커밋
  LICENSE-UxPlay.txt
  runtime/
    uxplay.exe              # 원본 FDH2/UxPlay 코드를 그대로 컴파일한 EXE
    *.dll                   # 종속 라이브러리
    gst-plugin-scanner.exe
    gst-inspect-1.0.exe
    plugins/*.dll           # GStreamer 플러그인
    gio/*.dll               # GIO 모듈(있는 경우)
```

빌드 시 포크 코드 대신 **FDH2/UxPlay/master의 최신 코드**를 별도 체크아웃하여 컴파일합니다. 빌드된 정확한 원본 커밋은 BUILD-INFO.txt에 기록됩니다. 내부 mDNS 구현을 사용하므로 Bonjour 설치가 필수는 아닙니다.

## Windows에서 실행

64비트 Windows 10/11에서 전체 폴더를 압축 해제하고 **UxPlay.exe 더블클릭**합니다. 검은색 로그 콘솔 창이 열려 있어야 정상입니다. 종료하려면 해당 창을 닫습니다.

iPhone과 Windows PC를 같은 네트워크에 연결하고 iPhone의 **제어 센터 → 화면 미러링**에서 UxPlay 장치를 선택합니다. 처음 실행 시 Windows Defender 방화벽이 물어보면 **runtime/uxplay.exe**를 **개인 네트워크**에 허용해 주세요. 공유기의 AP isolation 또는 방화벽 때문에 장치가 검색되지 않을 수 있습니다.

## 빌드 테스트의 의미

Actions는 원본 UxPlay 컴파일, 실행기 실행 및 도움말 테스트, GStreamer H.264 디코더/영상/음성 플러그인 존재 여부를 검사합니다. **실제 iPhone 화면 미러링은 Actions 가상 머신에서 확인되지 않으므로** Windows PC에서 한 번 테스트가 필요합니다. 필요한 구성요소가 누락되면 빌드가 실패하게 구성했습니다.

## 구성 파일

- `.github/workflows/build-portable-windows.yml` — 수동 또는 관련 파일 변경 시 빌드
- `packaging/launcher.c` — 환경변수와 DLL 탐색경로를 구성하고 원본 실행 파일을 실행
- `packaging/package_portable.py` — GStreamer 및 DLL 의존성을 수집하고 ZIP 생성

이 배포물은 UxPlay의 GPLv3 및 포함된 종속 라이브러리의 라이선스가 적용됩니다. 공개 재배포 시 각 종속성의 라이선스와 대응 소스 제공 요건을 확인하세요.
