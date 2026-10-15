#include <cassert>
#include <cstdio>
#include "learning_voice.h"
using namespace learning;
int main() {
    Model m;
    int clip;
    assert(VoiceCommand(m, "english", 2, clip) && m.word == 2 && clip == 2);
    assert(VoiceCommand(m, "next", -1, clip) && m.word == 3 && clip == 3);
    assert(VoiceCommand(m, "next", -1, clip) && m.page == Page::kQuiz && clip == 4);
    assert(!VoiceCommand(m, "next", -1, clip) && m.round == 0);
    assert(VoiceCommand(m, "answer", 1, clip) && !m.answered && clip == 4);
    assert(VoiceCommand(m, "answer", 0, clip) && m.answered && clip == -1);
    assert(!VoiceCommand(m, "answer", 0, clip));
    for (int round = 1; round < 4; round++) {
        assert(VoiceCommand(m, "next", -1, clip) && m.round == round && clip == 4 + round);
        assert(VoiceCommand(m, "answer", round % 2, clip) && m.answered);
    }
    assert(VoiceCommand(m, "next", -1, clip) && m.page == Page::kWord && m.word == 0);
    // 已移除的命令和越界索引必须拒绝，且不改变当前词卡。
    for (const char* action : {"poem", "recall", "erase"}) {
        assert(!VoiceCommand(m, action, 1, clip));
        assert(m.page == Page::kWord && m.word == 0 && clip == -1);
    }
    for (int index : {-2, 4}) {
        assert(!VoiceCommand(m, "english", index, clip));
        assert(m.page == Page::kWord && m.word == 0);
    }
    assert(VoiceCommand(m, "previous", -1, clip) && m.word == 3 && clip == 3);
    assert(VoiceCommand(m, "play", -1, clip) && clip == 3);
    assert(!VoiceCommand(m, "answer", 0, clip) && m.page == Page::kWord);
    assert(VoiceCommand(m, "quiz", -1, clip) && clip == 4 && m.round == 0);
    assert(!VoiceCommand(m, "answer", 2, clip) && !m.answered && m.choice == 0);
    assert(VoiceCommand(m, "home", -1, clip) && m.page == Page::kHome);
    assert(!VoiceCommand(m, "play", -1, clip));
    puts(
        "PASS: English voice commands, rejected legacy/invalid commands, four quiz rounds and "
        "retry.");
}
