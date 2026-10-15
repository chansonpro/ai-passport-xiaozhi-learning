# 小智学堂英语交互原型 v1.2

面向 5 岁儿童，240×320 屏幕，三键交互。此目录是独立网页原型，不参与固件构建。

打开 index.html，或在本目录运行 `python3 -m http.server 8765` 后访问 http://localhost:8765。

## 当前范围

- 首页只有英语乐园入口；四张词卡为 apple / banana / cat / dog。
- 最后一词后按下进入四轮、两选一的听音找图；支持重试和答对后继续。
- 上下选择／换词，中间确认／听读，长按中间返回，上下长按调音量。
- 键盘上下、Enter、Esc；保留单词位置。旧存储中的诗句位置不再使用。
- 不含古诗、录音、发音评分或云端连接。设备首页长按聊天不在浏览器中模拟。

## 模型验证

```sh
cmake -S scripts/learning -B /tmp/learning-tests -DBUILD_PREVIEW=OFF
cmake --build /tmp/learning-tests
ctest --test-dir /tmp/learning-tests --output-on-failure
```

以上命令在仓库根目录运行，需要 C++17、Python 3、Node.js，不需要 ESP-IDF 或 LVGL。
`compare_prototype.py` 对照 20,032 次同序列按键事件，检查当前页面、词卡、题次、选择、答题/听题状态及请求音频。
语音命令和设备聊天切换由独立回归覆盖；状态对照不替代实机和视觉验收。

浏览器语音由 SpeechSynthesis 提供，不保证离线；表情图形代替插画。不要据此推断实机音质或性能。
