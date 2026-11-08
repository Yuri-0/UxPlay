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

## 실행 후 창이 바로 닫히는 경우

최신 포터블 런처는 명령어 없이 더블클릭했는데 UxPlay가 약 12초 이내에 종료되거나 오류 종료하면 콘솔 창을 자동으로 열어 둡니다. 종료 코드를 확인한 뒤 Enter로 닫을 수 있습니다.

실행 폴더에 **UxPlay-last-run.log** 파일을 자동으로 기록하므로, 오류가 발생하면 이 파일을 열어 마지막 줄을 확인하세요. 단, 로그에는 사용 PC의 네트워크 정보가 포함될 수 있어 공유할 때 개인 정보를 확인하세요.

- 마지막 줄이 \`using system MAC address ...\`이라면, MAC 표시 자체가 오류는 아닙니다. 이후 mDNS 시작, 네트워크 소켓 초기화, DLL 로딩 등에 문제가 생겼을 수 있으므로 종료 코드 및 로그를 확인해야 합니다.
- 종료 코드 \`0xC0000005\`는 Windows 접근 위반(충돌)을 나타냅니다.
- 방화벽, UDP 5353 포트 충돌, 기존 AirPlay 수신 프로그램 실행 여부를 점검하세요.
- \`Initialized server socket(s)\` 메시지가 이어지고 프로그램이 계속 실행된다면 기본 네트워크 수신 대기에는 진입한 상태입니다.

현재 런처는 **오류를 숨기지 않고 표시·기록하는 진단 기능**이며, 기기별 충돌이나 네트워크 충돌을 무조건 자동으로 수정하는 기능은 아닙니다.

## LAN/mDNS + Bluetooth LE 동시 검색 (자동)

- 새 포터블 패키지는 **mDNS와 BLE AirPlay 기기 검색을 함께** 제공합니다.
- 압축 해제 후 **UxPlay.exe를 더블클릭**하면 UxPlay가 기본 내부 mDNS를 시작하고, 별도 Windows BLE 비콘도 자동 시작합니다. BAT, Python, 패키지 또는 명령줄을 사용자가 따로 설치할 필요는 없습니다.
- BLE 비콘은 **컴퓨터에 Bluetooth LE 광고(advertising)를 지원하는 동글이 있을 때만** 작동합니다. Bluetooth 장치가 켜져 있고 아이폰의 Bluetooth도 켜져 있어야 합니다. 일부 오래된 동글은 BLE 수신만 가능하고 광고가 불가능할 수 있습니다.
- **UxPlay-BLE.log**를 열어 "AirPlay Service_Discovery Advertisement" 및 "Publisher status change" 메시지로 광고 시작 여부를 확인합니다. "Publisher was aborted" 등 오류가 나더라도 **mDNS는 계속 동작합니다**.
- **UxPlay-last-run.log**는 UxPlay 실행 로그입니다. **UxPlay-beacon.dat**은 UxPlay가 자동 생성/삭제하는 임시 비콘 정보입니다.
- Windows 방화벽에 runtime/uxplay.exe의 **개인 네트워크** 접근을 허용해야 할 수 있습니다. BLE로 방화벽을 우회할 수는 없습니다.
- BLE는 **미러링 대상 장치의 IP/포트를 알리는 검색 수단**입니다. 실제 화면·소리는 iPhone의 Wi-Fi와 PC의 LAN을 통해 전송합니다. 중계기의 NAT/격리 규칙이 휴대폰→PC IP 연결을 차단한다면 블루투스로 기기 검색이 되어도 미러링에 연결할 수 없습니다.
- PC가 VPN이나 가상 어댑터 등 여러 네트워크에 연결됐다면 BLE 비콘이 잘못된 IPv4 주소를 알릴 수 있습니다. 현재 기본 경로의 PC IPv4 주소를 사용합니다.
- 기본 더블클릭 모드에서는 두 검색 방식을 켜고, -h 같은 명령줄 옵션을 직접 전달하면 원본 UxPlay 명령줄 동작을 유지합니다. 명령줄 모드에서는 비콘을 자동 시작하지 않습니다.

### 친구 PC 테스트

1. Windows Bluetooth 켜기, PC의 Bluetooth 동글 연결
2. 패키지 압축 해제 및 UxPlay.exe 더블클릭
3. iPhone Bluetooth와 Wi-Fi 켜기
4. iPhone을 PC 근처에 두고 제어 센터 → 화면 미러링 확인
5. 안 보이면 UxPlay-BLE.log의 오류 메시지 확인
6. 보이지만 연결되지 않는다면 IP 네트워크 도달성/방화벽 문제 확인

**CI의 한계:** GitHub Actions는 BLE 비콘 실행 파일의 Python/WinRT 모듈 로딩과 UxPlay 서버 시작 여부를 검사할 수 있지만, 실제 블루투스 동글과 iPhone이 없으므로 BLE 광고와 미러링 연결까지는 검증하지 못합니다.

## 최소 용량 패키징

포터블 빌드는 더 이상 MSYS2의 모든 GStreamer 모듈을 복사하지 않습니다. **UxPlay 기본 H.264 화면 미러링, AAC/ALAC 오디오, JPEG 이미지, Windows 음성·영상 출력**에 필요한 GStreamer 요소를 검사하여 관련 플러그인 DLL만 선택합니다. 추가 Windows 영상 출력·H.265·일부 HLS/RTP 요소는 설치돼 있을 경우 보존합니다.

선택된 플러그인과 DLL의 의존성은 재귀적으로 추적하며, CI에서는 MSYS2 라이브러리 경로를 제외하고 필수 요소와 서버 시작을 검사합니다.

- 실제 포함된 플러그인 목록은 ZIP 안의 **PACKAGED-GSTREAMER-PLUGINS.txt**를 참고하세요.
- 패키지의 **runtime/** 또는 **ble/** 폴더에서 임의로 파일을 더 삭제하지 마세요.
- 별도의 GStreamer 파이프라인 사용자 옵션, 특수 코덱 또는 HLS 앱 조합은 사용하지 않은 플러그인을 필요로 할 수 있습니다. **CI 성공은 실제 아이폰의 모든 미러링 및 스트리밍 형식을 보장하지 않습니다.**
- DLL·GStreamer 플러그인과 해당 오픈소스 라이선스의 재배포 요건은 유지됩니다.
