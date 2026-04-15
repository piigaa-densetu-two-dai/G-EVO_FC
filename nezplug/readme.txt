NEZplug++ VGM mod改造版

質問１：NEZplug++って何ですか？
回答：NSFファイル等をWindows PCで再生する為のプラグインソフトウェアです。NEZplug++と一緒に配布されているnezplay.exeやWinampと組み合わせて使用することが出来ます。

質問２：NEZplug++ VGM modって何ですか？
回答：NEZplug++にVGMファイル出力機能が付加されたものです。再生開始～再生停止までの一連の演奏情報がVGMファイルとして出力されます。

質問３：NEZplug++ VGM mod改造版って何ですか？
回答：NEZplug++ VGM modとNEZplug++ VGM mod改造版の違い

* MMC5/VRC6/N106のVGMデータ出力対応(VRC7は既にYM2413として対応されています)
* ベースとなるNEZplug++を0.9.4.8 + 3 + 24.10へ更新
* VGMLogging(VGM出力機能の有効・無効の設定)のデフォルト設定値を1(有効)に変更

使い方

要「Visual Studio 2013 の Visual C++ 再頒布可能パッケージ」

* nezplay.exeを使用する場合。
NEZplug++(http://offgao.net/program/nezplug++.html)からnezplug++.zipをダウンロード、解凍し、npnez.dllをここにあるもので上書きします。
nezplay.exeを起動し、NSFファイルを再生するとNSFファイルと同じ場所にVGMファイルが作成されます。再生開始～再生停止までの一連の演奏情報がファイルに出力されます。

* Winampを使用する場合。

Winamp LEGACY PLAYER(https://winamp.com/)をインストールし、インストール先のPluginsディレクトリにここにあるin_nez.dllをコピーします。
Winampを起動し、NSFファイルを再生するとNSFファイルと同じ場所にVGMファイルが作成されます。再生開始～再生停止までの一連の演奏情報がファイルに出力されます。

※ NEZplug++にはKbMedia Playerのプラグインが含まれますがVGMファイル出力機能が正常動作しない場合があるので端折りました💦

オリジナル配布元

* NEZplug++
http://offgao.net/program/nezplug++.html

* NEZplug++ VGM mod
https://vgmrips.net/forum/viewtopic.php?t=998
