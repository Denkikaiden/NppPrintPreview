----------------------------------------------------------------
NppPrintPreview 1.0.1 (short name: NppPriPre)
=============================================

A print preview plugin for Notepad++.
It lays out pages the same way as Notepad++'s own Print (Ctrl+P),
so you can check them on screen before printing.


■ Usage

When you choose Print in Notepad++, the preview window opens.
(Ctrl+P, File → Print..., or the Print button on the toolbar)
You can also open it from Plugins → NppPrintPreview → Print Preview...

If you change nothing, the result is the same as Notepad++'s own Print.
On the left side of the window you can change:
- Printer, paper, orientation
- Font, font size (pt)
- Color: Black on white / Color on white / Same as screen / Inverted
- Margins (top, bottom, left, right, in mm)
- Whether to print line numbers (available only when line numbers are shown on screen)
- Copies

You can also change the number boxes with the mouse wheel after clicking in the box.

The plugin remembers these values. It does not change Notepad++'s own settings.
"Reset to Notepad++ settings" sets the font, font size and margins back to Notepad++'s values.
"Print" prints exactly what the preview shows.

The information area on the left shows the lines per page,
the characters per line and the total number of pages.
At the top right you can turn pages and choose the zoom
(Whole page / Fit width / Actual size).
You can also turn pages with Page Up / Page Down or the mouse wheel.

If text is selected, "Selection only" prints just the selection.
Header, footer and form feed page breaks follow Notepad++'s print settings.
Whether line numbers are printed also starts from Notepad++'s print settings.

To use Notepad++'s normal Print dialog:
- Click "Notepad++ Print..." in the preview window. The preview closes and the normal Print dialog opens.
- To always use the normal Print dialog, uncheck
  Plugins → NppPrintPreview → "Open Preview on Notepad++ Print".
  (Print Preview... still opens the preview when it is unchecked.)

"MulCoPre Columns..." in the preview window works only when MultiColumnPreview (MulCoPre)
is installed (otherwise it is grayed out).

The display language follows Notepad++ (Japanese or English).
You can change it with "Language" at the bottom left of the window.

In Plugins → NppPrintPreview → About, click "Check now" to see whether a newer version is available.
The plugin contacts GitHub only when you click it (it does not connect at any other time).
You can update it from Plugins Admin.


■ What's new in 1.0.1

- Choosing Print in Notepad++ now opens the preview window.
  You can turn this on or off with "Open Preview on Notepad++ Print" in the menu (on by default).
- Added the "Notepad++ Print..." and "MulCoPre Columns..." buttons to the preview window.
- The license is now the MIT License. The source code is available on GitHub.


■ Installation

With Plugins Admin:
Plugins → Plugins Admin... → check "NppPrintPreview" → Install

Manually:
1. Close Notepad++.
2. Create a folder named "NppPrintPreview" in the plugins folder of Notepad++.
   (e.g. C:\Program Files\Notepad++\plugins\NppPrintPreview)
3. Put NppPrintPreview.dll in that folder.
4. Start Notepad++.

If "NppPrintPreview" appears in the Plugins menu, the installation is complete.


■ Uninstallation

In Plugins → Plugins Admin..., open the "Installed" tab, check "NppPrintPreview" and click "Remove".
If you installed it manually, close Notepad++ and delete the "NppPrintPreview" folder in the plugins folder.

The remembered values stay in the following file. Delete it if you no longer need it.
%APPDATA%\Notepad++\plugins\config\NppPrintPreview.ini


■ Requirements

Notepad++ 8 or later (64-bit)
Windows 10 / 11


■ License

MIT License (see LICENSE).
You may use, copy, modify and redistribute this plugin,
as long as you keep the copyright notice and the license text.
There is no warranty. The author is not responsible for any damage caused by using this plugin.
(The readme.txt in the 1.0.0 zip still shows the earlier freeware terms. The MIT License replaces them.)

It also works together with MultiColumnPreview (MulCoPre).


■ Source code and building

The source code is in this repository. Comments in the source are in Japanese.
- src/NppPrintPreview.cpp : the whole plugin (one file)
- src/NppPrintPreview.rc  : version information
- build.ps1               : build script

Requirements: Visual Studio 2019 or later with the C++ x64 build tools, PowerShell 7
Build:  pwsh -File build.ps1
Output: build\x64\NppPrintPreview.dll

It does not use the Notepad++ source code or headers.
The few definitions it needs are written in NppPrintPreview.cpp.

■ Contact

denkikaiden
BOOTH:  https://denkikaiden.booth.pm/items/9163814
GitHub: https://github.com/Denkikaiden/NppPrintPreview

-----------------------------------------------------------------------------------------------------------------------

NppPrintPreview 1.0.1（短縮名 NppPriPre）
=========================================

Notepad++ に印刷プレビューを足すプラグインです。
Notepad++ の「印刷」（Ctrl+P）と同じ組み方で、印刷する前にページを画面で確かめられます。


■ 使い方

Notepad++ で「印刷」を選ぶと、プレビューの窓が開きます。
（Ctrl+P、「ファイル」→「印刷…」、ツールバーの印刷ボタンのどれでも開きます）
プラグイン → NppPrintPreview → 印刷プレビュー… からも開けます。

何も変えなければ、Notepad++ の印刷と同じ結果になります。
窓の左で、次のものを変えられます。
・プリンター、用紙、向き
・フォント、文字サイズ（pt）
・色：白地に黒／白地にカラー／画面と同じ色／明暗を反転
・余白（上下左右 mm）
・行番号を印刷するかどうか（画面に行番号が出ているときだけ選べます）
・部数

数値の欄は、欄をクリックしてからマウスのホイールを回しても増減できます。

変えた値はこのプラグインが覚えます。Notepad++ の設定は書き換えません。
「Notepad++ の設定に戻す」で、フォント・文字サイズ・余白が Notepad++ の値に戻ります。
「印刷」を押すと、プレビューに出ているとおりに印刷されます。

窓の左の情報欄に、1 ページの行数・1 行の字数・全体のページ数が出ます。
右上で、ページ送りと表示の大きさ（ページ全体／幅に合わせる／実寸）を選べます。
PageUp・PageDown やマウスのホイールでもページを送れます。

選択している部分があれば、「選択範囲だけ」でそこだけを印刷できます。
ヘッダー・フッター・改ページ文字は、Notepad++ の印刷設定に従います。
行番号を印刷するかどうかの初めの値も、Notepad++ の印刷設定に従います。

Notepad++ のふつうの印刷画面を使いたいとき：
・プレビューの窓の「Notepad++ で印刷…」を押すと、プレビューを閉じてふつうの印刷画面が開きます。
・いつもふつうの印刷画面にしたいときは、プラグイン → NppPrintPreview →
  「Notepad++ の印刷でプレビューを開く」のチェックを外します。
  （印刷プレビュー… からは、チェックを外していても開けます）

プレビューの窓の「MulCoPre で段組み…」は、MultiColumnPreview（MulCoPre）が
入っているときだけ使えます（入っていなければ灰色です）。

表示言語は Notepad++ に合わせて日本語か英語になります。
窓の左下の「表示言語」で切り替えられます。

プラグイン → NppPrintPreview → バージョン情報 の「確認する」を押すと、
新しい版が出ているかを確かめられます。
押したときだけ GitHub に問い合わせます（ほかのときは通信しません）。
新しい版は、プラグイン管理から更新できます。


■ 1.0.1 で変わったこと

・Notepad++ で「印刷」を選ぶと、プレビューの窓が開くようにしました。
  メニューの「Notepad++ の印刷でプレビューを開く」で切り替えられます（初めは入）。
・プレビューの窓に「Notepad++ で印刷…」「MulCoPre で段組み…」のボタンを足しました。
・ライセンスを MIT ライセンスにしました。ソースは GitHub で公開しています。


■ インストール

プラグイン管理から入れる場合：
プラグイン → プラグイン管理… → 「NppPrintPreview」にチェック → インストール

手で入れる場合：
1. Notepad++ を閉じます。
2. Notepad++ のフォルダの plugins の中に「NppPrintPreview」というフォルダを作ります。
   （例：C:\Program Files\Notepad++\plugins\NppPrintPreview）
3. その中に NppPrintPreview.dll を入れます。
4. Notepad++ を起動します。

メニューの「プラグイン」に「NppPrintPreview」が出ればインストールできています。


■ アンインストール

プラグイン → プラグイン管理… の「インストール済み」で「NppPrintPreview」にチェックを入れ、削除します。
手で入れた場合は、Notepad++ を閉じてから plugins の中の「NppPrintPreview」フォルダを消します。

覚えた値は次のファイルに残ります。要らなければ消してください。
%APPDATA%\Notepad++\plugins\config\NppPrintPreview.ini


■ 動く環境

Notepad++ 8 以降（64 ビット版）
Windows 10 / 11


■ ライセンス

MIT ライセンスです（LICENSE を見てください）。
著作権の表示とライセンスの文を残せば、使う・写す・変える・配ることが自由にできます。
無保証です。このプラグインを使ったことで起きた損害について、作者は責任を負いません。
（1.0.0 の zip の readme.txt には以前のフリーウェアの条件が書いてありますが、MIT ライセンスに置き換えました）

MultiColumnPreview（MulCoPre）との連動機能もあります。


■ ソースとビルド

ソースはこのリポジトリにあります。ソースのコメントは日本語です。
- src/NppPrintPreview.cpp：プラグインの本体（1 ファイル）
- src/NppPrintPreview.rc ：版の情報
- build.ps1              ：ビルド用のスクリプト

必要なもの：Visual Studio 2019 以降（C++ の x64 ビルドツール）、PowerShell 7
ビルド：pwsh -File build.ps1
出力：build\x64\NppPrintPreview.dll

Notepad++ のソースやヘッダーは使っていません。必要な定義だけを NppPrintPreview.cpp に書いています。

■ 連絡先

でん⚡きかいでん
BOOTH：https://denkikaiden.booth.pm/items/9163814
GitHub：https://github.com/Denkikaiden/NppPrintPreview
