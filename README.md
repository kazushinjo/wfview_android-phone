# wfview4android

**wfview4android** は、Icom リグ用コントロールソフト **wfview** の Android 移植版（非公式フォーク）です（arm64-v8a、ネットワーク接続専用）。
IC-7300（Raspberry Pi 等の wfserver 経由）や IC-9700/IC-705 などの LAN 対応リグへ Wi-Fi で接続し、
スペクトラム／ウォーターフォール表示・同調・送受信・音声処理をタブレットから操作できます。

本リポジトリは [wfview](https://wfview.org/)（GPLv3）の非公式フォークです。本家プロジェクトとは無関係の個人移植であり、
不具合報告は本家ではなく本リポジトリへお願いします。

## 主な特徴（デスクトップ版からの変更点）

- **タッチ操作向けメイン画面**（横向き固定）
  - 大型の周波数表示＋桁タップで同調ステップ選択、▼／▲ステップボタン（長押しで連続）
  - 薄橙色の大型周波数ダイアル、Fine（1 Hz）／Lock（ロック中はステップボタンもグレーアウト）
  - レベルスライダー列に **WF**（ウォーターフォールの色レベル）を追加、AF スライダーは端末のメディア音量と連動（起動時は 50%）
  - 電源 ON/OFF・終了は誤操作防止の**ダブルタップ実行**（確認ダイアログなし）
  - すべてのポップアップ（ログ表示・無線機ステータス含む）に **← 戻る** ボタン。バンド選択／無線機選択はタップで自動クローズ。Freq テンキーの 1 桁削除キーは「Back」表記
  - 設定画面は基準 13pt・実寸 1.0 倍のフル幅表示（表示中ページ単位でフィット）
  - S メーター下に**送信音声レベルメーター（Tx dBfs・赤バー）**、送信ボタンは即時反応
  - マイクは内蔵マイク推奨（約6倍ブースト内蔵、Bluetooth マイクは SCO 未対応）。音声デバイス選択は即時保存
  - 画面最下段に受信遅延（rx latency）／rtt／ロス／リグ名を常時表示
- **接続プロファイル**: 複数リグの接続先（ホスト・ポート・CI-V・音声設定）を保存して切替
- **CI-V アドレスのモデル別プルダウン**（メーカー連動）
- **日本語操作説明書を同梱**（メイン画面の「ヘルプ」ボタン、`docs/help_ja.md`）
- **Android 固有の対策**
  - Qt 6.8 Android の QAudioSink プッシュモード不具合を回避する受信音声の**プルモード実装**（リングバッファ）
  - **レスポンシブ表示（このブランチ）**: メイン画面はネイティブレイアウトで任意の解像度・縦横比に追従。
    固定 px 値はすべて基準デザイン（2400x1378）→実画面の換算 `androidDp()` で算出（`include/androidcompat.h`）。
    2000x1200 / 2560x1600 / 1920x1080 / 2400x1080 のエミュレータで表示確認済み。
    ポップアップは従来どおり実行時の等比縮尺（もともと機種非依存）

## ビルド

macOS ホストでのビルド手順は [`ANDROID_BUILD_NOTES_MAC.md`](ANDROID_BUILD_NOTES_MAC.md)、
Windows ホストは [`ANDROID_BUILD_NOTES.md`](ANDROID_BUILD_NOTES.md) を参照してください。

- Qt 6.8.1（android_arm64_v8a）+ NDK r27（27.3.13750724）+ JDK 17
- 依存: eigen / opus（NDK ビルドの静的ライブラリ）/ qcustomplot 2.1.1 / r8brain-free-src（クローンの兄弟ディレクトリに配置）
- `qmake wfview.pro -spec android-clang` → `make` → `androiddeployqt6` で APK を生成

## 動作環境

- Android タブレット／スマートフォン（arm64-v8a、横向き。解像度・縦横比は自動追従。基準調整は 2000x1200）
- Icom LAN 対応リグ（IC-9700/IC-705/IC-7610 等）または wfserver（IC-7300 等）

## ドキュメント

- 操作説明書（日本語）: [`docs/help_ja.md`](docs/help_ja.md)（アプリ内「ヘルプ」からも閲覧可）
- Word 版: `docs/help_ja.docx`

## ライセンス

GNU GPLv3。Copyright 2017- Elliott H. Liggett (W6EL), Phil E. Taylor (M0VSE) ほか wfview 開発チーム、
および本 Android 移植の変更部分の著作者。詳細は [LICENSE](LICENSE) を参照してください。
