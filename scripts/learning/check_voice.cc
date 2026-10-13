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
    assert(VoiceCommand(m, "poem", 1, clip) && m.line == 1 && clip == 9);
    assert(VoiceCommand(m, "next", -1, clip) && m.line == 2 && clip == 10);
    assert(VoiceCommand(m, "recall", 2, clip) && m.page == Page::kRecall && clip == -1);
    assert(VoiceCommand(m, "play", -1, clip) && clip == 10 && m.page == Page::kRecall);
    assert(VoiceCommand(m, "previous", -1, clip) && m.line == 1 && clip == -1);
    assert(!VoiceCommand(m, "answer", 0, clip) && m.page == Page::kRecall && m.line == 1);
    assert(!VoiceCommand(m, "erase", -1, clip) && m.page == Page::kRecall);
    assert(!VoiceCommand(m, "english", 4, clip) && m.page == Page::kRecall);
    assert(VoiceCommand(m, "home", -1, clip) && m.page == Page::kHome);
    assert(!VoiceCommand(m, "play", -1, clip));
    puts(
        "PASS: voice commands, invalid inputs, word/poem selection, four quiz rounds, retry, "
        "hidden recall and hints.");
}
