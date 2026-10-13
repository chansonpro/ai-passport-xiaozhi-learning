#pragma once
#include <string_view>
#include "learning_model.h"
namespace learning {
// Validated semantic commands. Invalid commands leave the model unchanged.
// Audio IDs describe what XiaoZhi should say; voice commands never enqueue local audio.
inline bool VoiceCommand(Model& m, std::string_view action, int index, int& clip) {
    clip = -1;
    if (index < -1 || index > 3)
        return false;
    Model next = m;
    next.message = "";
    if (action == "english") {
        next.page = Page::kWord;
        next.selected = 0;
        if (index >= 0)
            next.word = index;
        clip = next.word;
    } else if (action == "poem" || action == "recall") {
        next.recall_mode = action == "recall";
        next.page = next.recall_mode ? Page::kRecall : Page::kPoem;
        next.line = index >= 0 ? index : 0;
        next.selected = 0;
        if (!next.recall_mode)
            clip = 8 + next.line;
    } else if (action == "quiz") {
        next.page = Page::kQuiz;
        next.round = next.choice = next.selected = 0;
        next.answered = false;
        next.question_heard = true;
        clip = 4;
    } else if (action == "home") {
        next.page = Page::kHome;
        next.selected = 0;
    } else if (action == "next" || action == "previous") {
        if (next.page == Page::kHome || next.page == Page::kNone)
            return false;
        if (next.page == Page::kQuiz) {
            if (action != "next" || !next.answered)
                return false;
            clip = next.Act(Button::kOk, Press::kClick);
        } else {
            clip = next.Act(action == "next" ? Button::kDown : Button::kUp, Press::kClick);
            if (next.page == Page::kWord)
                clip = next.word;
            else if (next.page == Page::kPoem && next.selected == 0)
                clip = 8 + next.line;
        }
    } else if (action == "play") {
        if (next.page == Page::kWord)
            clip = next.word;
        else if (next.page == Page::kPoem || next.page == Page::kRecall)
            clip = 8 + next.line;
        else if (next.page == Page::kQuiz && !next.answered) {
            clip = 4 + next.round;
            next.question_heard = true;
        } else
            return false;
    } else if (action == "answer") {
        if (next.page != Page::kQuiz || next.answered || index < 0 || index > 1)
            return false;
        next.choice = index;
        clip = next.Act(Button::kOk, Press::kClick);
    } else
        return false;
    m = next;
    return true;
}
}  // namespace learning
